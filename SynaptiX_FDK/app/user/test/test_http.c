#include "test_http.h"
#include "sx_board.h"
#include "a7677s_http.h"
#include "modem_ops.h"
#include "logger.h"
#include <string.h>
#include <stdbool.h>

static const char *TAG = "TEST_HTTP";

/* Real test file pushed by the user to the WS_v1 repo itself (public repo,
 * confirmed by the user) - see TrackingFirmWare.bin at the repo root on
 * branch ft/fota_ws. Using GitHub's raw content CDN, which is confirmed
 * (per public documentation - see a7677s_http.h and a7677s_http.c's
 * comments on why SNI matters for CDN-fronted hosts) to support HTTP Range
 * requests, which this test's chunked download depends on. https:// -
 * a7677s_http_ssl_configure() below is required before the first range
 * call reaches this URL (see test_http_init()/test_http_poll()). */
#define TEST_HTTP_URL          "https://raw.githubusercontent.com/logan123synaptix/WS_v1/ft/fota_ws/TrackingFirmWare.bin"

/* One HTTP Range request size - independent of A7677S_HTTP_READ_CHUNK_SIZE
 * (a7677s_http.h), see that file's comment on the two-level chunking.
 * Kept below A7677S_HTTP_RANGE_SIZE (4096, a7677s_http.h). 2048 chosen so
 * the READ loop inside a7677s_http_get_range() runs multiple times per
 * range (2048 / 400-ish byte reads = ~5-6 AT+HTTPREAD calls per range),
 * exercising the "read same range at increasing offsets" path (point #2 in
 * test_http.h's doc-comment) more than once before this test's own outer
 * loop even advances to the next range. */
#define TEST_HTTP_RANGE_LEN    2048U

/* How many ranges to pull in total before stopping and reporting a
 * summary - deliberately small (a handful of ranges = a few KB), just
 * enough to exercise the state machine multiple times end-to-end without
 * needing a full multi-hundred-KB run for this first bring-up pass. */
#define TEST_HTTP_MAX_RANGES   5U

typedef enum {
    TEST_HTTP_IDLE = 0,
    TEST_HTTP_WAIT_READY,
    TEST_HTTP_SSL_CONFIGURING,  /* a7677s_http_ssl_configure() in flight - see test_http_poll() */
    TEST_HTTP_RUNNING,
    TEST_HTTP_DONE,
} test_http_state_t;

static test_http_state_t s_state = TEST_HTTP_IDLE;
static uint32_t s_offset = 0;
static uint32_t s_range_count = 0;
static uint32_t s_total_bytes = 0;
static uint32_t s_fail_count = 0;
static bool s_ssl_ok = false;   /* set by on_ssl_configured() below */

static void on_range_done(a7677s_http_range_result_t result,
                           int status_code,
                           const uint8_t *data,
                           uint32_t data_len,
                           void *ctx)
{
    (void)ctx;

    switch (result) {
    case A7677S_HTTP_RANGE_OK:
        s_total_bytes += data_len;
        log_info(TAG, "[range %lu] OK status=%d data_len=%lu (offset was %lu)",
                 (unsigned long)s_range_count, status_code,
                 (unsigned long)data_len, (unsigned long)s_offset);

        /* Dump the first and last few bytes as hex so a human can sanity
         * check this actually looks like file content (not an HTML error
         * page, not garbage from a parsing bug) without flooding the log
         * with the whole chunk. */
        {
            uint32_t head_n = (data_len < 16) ? data_len : 16;
            char hexbuf[16 * 3 + 1];
            uint32_t i;
            for (i = 0; i < head_n; i++) {
                snprintf(hexbuf + i * 3, 4, "%02X ", data[i]);
            }
            log_info(TAG, "  head: %s", hexbuf);
        }
        break;

    case A7677S_HTTP_RANGE_HTTP_ERROR:
        s_fail_count++;
        log_error(TAG, "[range %lu] HTTP_ERROR status=%d",
                  (unsigned long)s_range_count, status_code);
        break;

    case A7677S_HTTP_RANGE_AT_ERROR:
        s_fail_count++;
        log_error(TAG, "[range %lu] AT_ERROR (AT command sequence failed - check earlier "
                       "log lines for which step, and the exact raw response text)",
                  (unsigned long)s_range_count);
        break;

    case A7677S_HTTP_RANGE_BUSY:
        /* Should not happen here - this test never calls get_range() again
         * before the previous one's callback fired. Logged in case it
         * somehow does, since that would indicate a bug in this test
         * itself, not in a7677s_http.c. */
        s_fail_count++;
        log_error(TAG, "[range %lu] unexpected BUSY result", (unsigned long)s_range_count);
        break;
    }

    s_offset += TEST_HTTP_RANGE_LEN;
    s_range_count++;

    if (s_range_count >= TEST_HTTP_MAX_RANGES) {
        s_state = TEST_HTTP_DONE;
        log_info(TAG, "=== TEST DONE: %lu/%lu ranges OK, %lu bytes total, %lu failures ===",
                 (unsigned long)(s_range_count - s_fail_count),
                 (unsigned long)s_range_count,
                 (unsigned long)s_total_bytes,
                 (unsigned long)s_fail_count);
    }
    /* else: stay in TEST_HTTP_RUNNING, test_http_poll() issues the next
     * range on its next tick (see below) - deliberately not chained
     * directly from this callback, to keep this test's own state
     * transitions visible/steppable in poll() rather than recursing
     * straight back into another get_range() from inside a callback. */
}

static void on_ssl_configured(modem_ops_result_t result, void *ctx)
{
    (void)ctx;
    if (result == MODEM_OPS_OK) {
        log_info(TAG, "SSL context configured OK (authmode=0, SNI=1)");
        s_ssl_ok = true;
    } else {
        log_error(TAG, "a7677s_http_ssl_configure() failed - HTTPS range calls will "
                       "likely fail their TLS handshake. Check AT+CSSLCFG response text "
                       "in the log above against a7677s_http.c's cb_ssl_authmode()/"
                       "cb_ssl_sni() assumptions.");
        s_ssl_ok = false;
    }
    /* Either way, move on to RUNNING - test_http_poll() will surface the
     * consequence (AT_ERROR or HTTP_ERROR from the first get_range() call)
     * if SSL setup actually failed, rather than this test silently
     * refusing to try at all. */
    s_state = TEST_HTTP_RUNNING;
}

void test_http_init(void)
{
    log_info(TAG, "=== TEST HTTP (a7677s_http.c bring-up) ===");
    log_info(TAG, "URL: %s", TEST_HTTP_URL);
    if (strstr(TEST_HTTP_URL, "REPLACE_ME")) {
        log_error(TAG, "TEST_HTTP_URL is still the placeholder - edit test_http.c "
                       "with a real reachable HTTP file URL before running this test");
    }
    s_state       = TEST_HTTP_WAIT_READY;
    s_offset      = 0;
    s_range_count = 0;
    s_total_bytes = 0;
    s_fail_count  = 0;
    s_ssl_ok      = false;
}

void test_http_poll(uint32_t delta_ms)
{
    (void)delta_ms;

    switch (s_state) {
    case TEST_HTTP_IDLE:
        return;

    case TEST_HTTP_WAIT_READY:
        /* Deliberately NOT driving power-on/network-attach here - see
         * test_http.h's doc-comment. Only checks a7677s_ops.is_ready(),
         * assumed already true because the modem was brought up some
         * other way (e.g. test_lte_mqtt run first) before this test
         * started. */
        if (!a7677s_ops.is_ready(&board.a7677s)) {
            return; /* keep waiting, poll() will be called again next tick */
        }
        if (a7677s_http_is_busy(&board.a7677s)) {
            return; /* modem command channel occupied by something else - wait */
        }
        log_info(TAG, "Modem ready, configuring SSL context (TEST_HTTP_URL is https)...");
        {
            int ret = a7677s_http_ssl_configure(&board.a7677s, on_ssl_configured, NULL);
            if (ret != 0) {
                log_error(TAG, "a7677s_http_ssl_configure() rejected immediately (ret=%d) - "
                               "modem busy with something else?", ret);
                s_state = TEST_HTTP_DONE;
                return;
            }
        }
        s_state = TEST_HTTP_SSL_CONFIGURING;
        return;

    case TEST_HTTP_SSL_CONFIGURING:
        return; /* waiting for on_ssl_configured() to fire and advance s_state */

    case TEST_HTTP_RUNNING:
        if (a7677s_http_is_busy(&board.a7677s)) {
            return; /* previous range still in flight, wait for its callback */
        }
        {
            int ret = a7677s_http_get_range(&board.a7677s, TEST_HTTP_URL,
                                             s_offset, TEST_HTTP_RANGE_LEN,
                                             on_range_done, NULL);
            if (ret != 0) {
                log_error(TAG, "a7677s_http_get_range() rejected immediately "
                               "(ret=%d) - modem busy with something else?", ret);
                s_fail_count++;
                s_state = TEST_HTTP_DONE;
            }
        }
        return;

    case TEST_HTTP_DONE:
        return; /* test finished, see log for summary - nothing more to do */
    }
}