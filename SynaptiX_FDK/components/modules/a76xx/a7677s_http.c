/* Implements a7677s_http.h - see that file's header comment for the full
 * design rationale (why chunking is capped near 400 bytes, why this module
 * has its own command[] instead of sharing a7677s.c's CMD_DYNAMIC slot).
 *
 * Sequence driven per a7677s_http_get_range() call, one AT command at a
 * time via modem_send_command()/modem_poll() (modem.c) - mirrors the
 * cb_mqtt_pub_topic -> cb_mqtt_pub_topic_data -> ... chaining pattern in
 * a7677s.c exactly, just with a private state enum/command[] instead of
 * reusing a7677s.c's static ones (neither is exposed outside that file -
 * confirmed by grep, only a7677s_init/a7677s_set_full_apn/
 * a7677s_mqtt_register_callbacks are public there).
 *
 * State lives in a single static struct here, NOT inside a7677s_t (a7677s.h
 * was deliberately not touched - see file header comment in the .h) keyed
 * implicitly by there only ever being one a7677s_t instance on this board
 * (same single-instance assumption the rest of a7677s.c already makes
 * everywhere, e.g. s_mqtt_dyn_cmd_buf being a single static scratch buffer
 * rather than per-instance). If a second modem instance is ever added, this
 * (and a7677s.c's own statics) both need revisiting together, not just
 * this file. */

#include "a7677s_http.h"
#include "modem.h"
#include "logger.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const char *TAG = "A7677S_HTTP";

/* --- Private AT command table, entirely separate from a7677s.c's command[]
 * / CMD_DYNAMIC (see file header comment - deliberate, not an oversight). */
#define HTTP_CMD_INIT      0   /* AT+HTTPINIT */
#define HTTP_CMD_PARA_URL  1   /* AT+HTTPPARA="URL",<url> */
#define HTTP_CMD_PARA_SSL  2   /* AT+HTTPPARA="SSLCFG",<ctx> - only sent when url is https:// */
#define HTTP_CMD_PARA_HDR  3   /* AT+HTTPPARA="USERDATA",<range header> */
#define HTTP_CMD_ACTION    4   /* AT+HTTPACTION=0 (GET) */
#define HTTP_CMD_READ      5   /* AT+HTTPREAD=<offset>,<size> */
#define HTTP_CMD_TERM      6   /* AT+HTTPTERM */
#define HTTP_CMD_COUNT     7

/* --- SSL context one-time setup, entirely separate command slots/state
 * from the per-range HTTP_CMD_* above (a7677s_http_ssl_configure() is
 * called once at boot, not per range - see a7677s_http.h doc-comment). */
#define SSL_CMD_AUTHMODE   0   /* AT+CSSLCFG="authmode",<ctx>,0 */
#define SSL_CMD_SNI        1   /* AT+CSSLCFG="enableSNI",<ctx>,1 */
#define SSL_CMD_COUNT      2

static modem_command_t s_http_command[HTTP_CMD_COUNT];
static modem_command_t s_ssl_command[SSL_CMD_COUNT];

/* Scratch buffer for dynamically-built AT command strings (URL/Range/READ
 * params vary per call). Sized to comfortably hold
 * "AT+HTTPPARA=\"USERDATA\",\"Range: bytes=4294967295-4294967295\"\r\n"
 * (the longest line this module builds) plus margin - mirrors
 * A7677S_MQTT_DYN_CMD_MAX's role in a7677s.c, sized separately here since
 * this module does not touch that constant or buffer. */
#define HTTP_DYN_CMD_MAX   320U
static char s_http_dyn_cmd_buf[HTTP_DYN_CMD_MAX];

/* --- Per-call state (single in-flight call at a time - see
 * a7677s_http_is_busy()). Reset at the start of every
 * a7677s_http_get_range() call, not persisted across calls. */
typedef enum {
    HTTP_STATE_IDLE = 0,
    HTTP_STATE_INIT,
    HTTP_STATE_PARA_URL,
    HTTP_STATE_PARA_SSL,     /* only entered when s_http.is_https - see cb_http_para_url() */
    HTTP_STATE_PARA_HDR,
    HTTP_STATE_ACTION,
    HTTP_STATE_READ,
    HTTP_STATE_TERM,
} http_state_t;

static struct {
    http_state_t state;
    a7677s_t *dce;

    char     url[A7677S_HTTP_URL_MAX];
    bool     is_https;       /* true if url starts with "https://" - decides whether the
                               * PARA_SSL step below is sent at all (see cb_http_para_url()) */
    uint32_t start_offset;   /* absolute offset into the remote file, first byte of this range */
    uint32_t range_len;      /* requested length of this range */

    int      http_status;    /* parsed from +HTTPACTION:<method>,<statuscode>,<datalen> */
    uint32_t http_datalen;   /* <datalen> from the same line - actual bytes available to read,
                               * may be less than range_len (see a7677s_http.h's cb doc-comment) */
    uint32_t read_offset;    /* how many bytes of http_datalen have been read so far via
                               * AT+HTTPREAD, drives the read loop below */

    uint8_t  data[A7677S_HTTP_RANGE_SIZE];  /* accumulates HTTPREAD output across this range */
    uint32_t data_len;       /* bytes actually written into data[] so far */

    a7677s_http_range_cb_t cb;
    void *ctx;
} s_http;

/* Fire the caller's callback exactly once and reset state to IDLE -
 * mirrors mqtt_op_done()'s "fire once, clear" contract in a7677s.c. */
static void http_op_done(a7677s_http_range_result_t result, int status_code)
{
    a7677s_http_range_cb_t cb = s_http.cb;
    void *ctx = s_http.ctx;
    const uint8_t *data = s_http.data;
    uint32_t data_len = s_http.data_len;

    s_http.cb = NULL;
    s_http.state = HTTP_STATE_IDLE;

    if (cb) cb(result, status_code, data, data_len, ctx);
}

static void http_send_dynamic(int cmd_idx, const char *cmd_str,
                               const char *res_success, const char *res_fail,
                               modem_command_response_callback_t cb,
                               uint32_t timeout_ms)
{
    s_http_command[cmd_idx].cmd         = cmd_str;
    s_http_command[cmd_idx].res_success = res_success;
    s_http_command[cmd_idx].res_fail    = res_fail;
    s_http_command[cmd_idx].callback    = cb;
    s_http_command[cmd_idx].arg         = s_http.dce;
    log_debug(TAG, "HTTP CMD: %s", cmd_str);
    modem_send_command(pModem(s_http.dce), &s_http_command[cmd_idx], timeout_ms);
}

/* Forward declarations - chained in send order (INIT -> PARA_URL ->
 * PARA_HDR -> ACTION -> READ (repeats) -> TERM). */
static void cb_http_init(modem_t *modem, const char *response, modem_response_st_t res, void *arg);
static void cb_http_para_url(modem_t *modem, const char *response, modem_response_st_t res, void *arg);
static void cb_http_para_hdr(modem_t *modem, const char *response, modem_response_st_t res, void *arg);
static void cb_http_action(modem_t *modem, const char *response, modem_response_st_t res, void *arg);
static void cb_http_read(modem_t *modem, const char *response, modem_response_st_t res, void *arg);
static void cb_http_term(modem_t *modem, const char *response, modem_response_st_t res, void *arg);

/* AT+HTTPINIT MaxResponseTime is not separately documented beyond the
 * generic "short command" default in a76xx_at_cmd.md; reusing
 * A7677S_TIMEOUT_AT (2500ms, a7677s.h) matches how a7677s.c treats other
 * undocumented-timeout short commands (e.g. CMD_CPOF). */
#define HTTP_TIMEOUT_SHORT_MS   2500U
/* AT+HTTPACTION=0's MaxResponseTime is 120000ms per a76xx_at_cmd.md - see
 * a7677s_http.h file header comment. Give the AT layer a small margin over
 * the modem's own documented worst case, same pattern as
 * A7677S_TIMEOUT_MQTT_DISC's "+10s over datasheet" margin in a7677s.h. */
#define HTTP_TIMEOUT_ACTION_MS  121000U

int a7677s_http_get_range(a7677s_t *dce,
                           const char *url,
                           uint32_t start_offset,
                           uint32_t range_len,
                           a7677s_http_range_cb_t cb,
                           void *ctx)
{
    if (!dce || !url || !cb) {
        log_error(TAG, "get_range(): invalid arguments");
        return -1;
    }
    if (range_len == 0 || range_len > A7677S_HTTP_RANGE_SIZE) {
        log_error(TAG, "get_range(): range_len %lu out of bounds (max %u)",
                  (unsigned long)range_len, A7677S_HTTP_RANGE_SIZE);
        return -1;
    }
    if (strlen(url) >= A7677S_HTTP_URL_MAX) {
        log_error(TAG, "get_range(): url too long");
        return -1;
    }
    if (s_http.state != HTTP_STATE_IDLE) {
        log_warn(TAG, "get_range(): already busy (state=%d)", (int)s_http.state);
        return -1;
    }
    if (modem_is_busy(pModem(dce))) {
        /* Not our own state, but the shared modem_t's command channel is
         * occupied by something else (MQTT, init sequence, etc) - see
         * a7677s_http.h's A7677S_HTTP_RANGE_BUSY doc-comment. Reject now
         * rather than silently queuing, since modem_send_command() itself
         * would just return -1 and never call us back. */
        log_warn(TAG, "get_range(): modem command channel busy with another operation");
        return -1;
    }

    memset(&s_http, 0, sizeof(s_http));
    s_http.dce          = dce;
    s_http.start_offset = start_offset;
    s_http.range_len    = range_len;
    s_http.cb           = cb;
    s_http.ctx          = ctx;
    strncpy(s_http.url, url, sizeof(s_http.url) - 1);
    /* strncasecmp is not always available (not standard C, POSIX-only) -
     * this codebase's style elsewhere (a7677s.c) sticks to plain strstr/
     * strchr from <string.h>, so match that instead of assuming a
     * case-insensitive compare function exists in this toolchain. URLs are
     * expected lowercase scheme per RFC 3986 convention (and every example
     * in a76xx_at_cmd.md uses lowercase "http"/"https"), so a plain
     * strncmp is sufficient here without pulling in a portability risk for
     * a case that should not occur in practice. */
    s_http.is_https = (strncmp(url, "https://", 8) == 0);

    s_http.state = HTTP_STATE_INIT;
    http_send_dynamic(HTTP_CMD_INIT, "AT+HTTPINIT\r\n",
                       "\r\nOK\r\n", "\r\nERROR\r\n",
                       cb_http_init, HTTP_TIMEOUT_SHORT_MS);
    return 0;
}

bool a7677s_http_is_busy(a7677s_t *dce)
{
    (void)dce; /* single-instance assumption, see file header comment */
    return s_http.state != HTTP_STATE_IDLE;
}

static void cb_http_init(modem_t *modem, const char *response,
                          modem_response_st_t res, void *arg)
{
    (void)modem; (void)arg;
    if (res != MODEM_RESPONSE_SUCCESS) {
        log_error(TAG, "AT+HTTPINIT failed (res=%d)", res);
        http_op_done(A7677S_HTTP_RANGE_AT_ERROR, 0);
        return;
    }

    s_http.state = HTTP_STATE_PARA_URL;
    /* Per a76xx_at_cmd.md's AT+HTTPPARA syntax: AT+HTTPPARA="URL","<url>" */
    snprintf(s_http_dyn_cmd_buf, sizeof(s_http_dyn_cmd_buf),
             "AT+HTTPPARA=\"URL\",\"%s\"\r\n", s_http.url);
    http_send_dynamic(HTTP_CMD_PARA_URL, s_http_dyn_cmd_buf,
                       "\r\nOK\r\n", "\r\nERROR\r\n",
                       cb_http_para_url, HTTP_TIMEOUT_SHORT_MS);
}

static void cb_http_para_url(modem_t *modem, const char *response,
                              modem_response_st_t res, void *arg)
{
    (void)modem; (void)arg;
    if (res != MODEM_RESPONSE_SUCCESS) {
        log_error(TAG, "AT+HTTPPARA=URL failed (res=%d)", res);
        /* AT+HTTPINIT already succeeded - must HTTPTERM before giving up,
         * otherwise the next get_range() call's HTTPINIT will fail because
         * an HTTP session is still open. See cb_http_read()'s error path
         * for the same reasoning applied after ACTION/READ failures. */
        s_http.state = HTTP_STATE_TERM;
        http_send_dynamic(HTTP_CMD_TERM, "AT+HTTPTERM\r\n",
                           "\r\nOK\r\n", "\r\nERROR\r\n",
                           cb_http_term, HTTP_TIMEOUT_SHORT_MS);
        return;
    }

    s_http.state = HTTP_STATE_PARA_HDR;
    /* USERDATA carries our own Range header - AT+HTTPPARA has no dedicated
     * Range parameter (confirmed against a76xx_at_cmd.md - see
     * a7677s_http.h). end_offset is inclusive per RFC 7233 Range syntax. */
    snprintf(s_http_dyn_cmd_buf, sizeof(s_http_dyn_cmd_buf),
             "AT+HTTPPARA=\"USERDATA\",\"Range: bytes=%lu-%lu\"\r\n",
             (unsigned long)s_http.start_offset,
             (unsigned long)(s_http.start_offset + s_http.range_len - 1));
    http_send_dynamic(HTTP_CMD_PARA_HDR, s_http_dyn_cmd_buf,
                       "\r\nOK\r\n", "\r\nERROR\r\n",
                       cb_http_para_hdr, HTTP_TIMEOUT_SHORT_MS);
}

static void cb_http_para_hdr(modem_t *modem, const char *response,
                              modem_response_st_t res, void *arg)
{
    (void)modem; (void)arg;
    if (res != MODEM_RESPONSE_SUCCESS) {
        log_error(TAG, "AT+HTTPPARA=USERDATA failed (res=%d)", res);
        s_http.state = HTTP_STATE_TERM;
        http_send_dynamic(HTTP_CMD_TERM, "AT+HTTPTERM\r\n",
                           "\r\nOK\r\n", "\r\nERROR\r\n",
                           cb_http_term, HTTP_TIMEOUT_SHORT_MS);
        return;
    }

    s_http.state = HTTP_STATE_ACTION;
    /* AT+HTTPACTION=0 -> GET. Per a76xx_at_cmd.md, "OK" comes back
     * immediately (command accepted), the REAL result is a later URC-style
     * line "+HTTPACTION:<method>,<statuscode>,<datalen>" - same two-stage
     * shape as +CMQTTSTART/+CMQTTCONNECT etc in a7677s.c (see that file's
     * NOTE on res_success needing the full URC, not bare OK). We therefore
     * set res_success to the "+HTTPACTION:0," prefix (method=0 is GET),
     * not "\r\nOK\r\n" - matching that established pattern exactly, so this
     * callback only fires once the real result line has arrived, not on
     * the earlier bare OK. NOT verified against a real module yet (see
     * a7677s_http.h note that a real download must be tested against
     * hardware) - if the module's actual response text differs (e.g. a
     * space after the colon, mirroring the CMQTTSTART fix noted in
     * a7677s.c), this must be corrected against real hardware log, not
     * assumed. */
    http_send_dynamic(HTTP_CMD_ACTION, "AT+HTTPACTION=0\r\n",
                       "+HTTPACTION:0,", "\r\nERROR\r\n",
                       cb_http_action, HTTP_TIMEOUT_ACTION_MS);
}

static void cb_http_action(modem_t *modem, const char *response,
                            modem_response_st_t res, void *arg)
{
    (void)arg;
    if (res != MODEM_RESPONSE_SUCCESS) {
        log_error(TAG, "AT+HTTPACTION failed/timed out (res=%d)", res);
        s_http.state = HTTP_STATE_TERM;
        http_send_dynamic(HTTP_CMD_TERM, "AT+HTTPTERM\r\n",
                           "\r\nOK\r\n", "\r\nERROR\r\n",
                           cb_http_term, HTTP_TIMEOUT_SHORT_MS);
        return;
    }

    /* Parse "+HTTPACTION:0,<statuscode>,<datalen>" out of modem->buff
     * (response points into it). Not using response directly with sscanf's
     * %d on the whole buffer in case of leading noise - locate the marker
     * first, same defensive style as urc_process_header_line()'s
     * strchr(line, ':') + sscanf(p+1, ...) in a7677s.c. */
    const char *p = strstr(response, "+HTTPACTION:0,");
    int status = 0;
    unsigned long datalen = 0;
    if (!p || sscanf(p, "+HTTPACTION:0,%d,%lu", &status, &datalen) != 2) {
        log_error(TAG, "AT+HTTPACTION: failed to parse response [%s]", modem->buff);
        s_http.state = HTTP_STATE_TERM;
        http_send_dynamic(HTTP_CMD_TERM, "AT+HTTPTERM\r\n",
                           "\r\nOK\r\n", "\r\nERROR\r\n",
                           cb_http_term, HTTP_TIMEOUT_SHORT_MS);
        return;
    }

    s_http.http_status  = status;
    s_http.http_datalen = (uint32_t)datalen;
    log_info(TAG, "AT+HTTPACTION: status=%d datalen=%lu", status, datalen);

    if (status != 200 && status != 206) {
        log_error(TAG, "AT+HTTPACTION: unexpected HTTP status %d", status);
        s_http.state = HTTP_STATE_TERM;
        http_send_dynamic(HTTP_CMD_TERM, "AT+HTTPTERM\r\n",
                           "\r\nOK\r\n", "\r\nERROR\r\n",
                           cb_http_term, HTTP_TIMEOUT_SHORT_MS);
        return;
    }

    if (s_http.http_datalen == 0) {
        /* Nothing to read (e.g. server returned an empty range) - go
         * straight to TERM with what we have (0 bytes), let fota.c decide
         * whether that's an error given the file's remaining size. */
        s_http.state = HTTP_STATE_TERM;
        http_send_dynamic(HTTP_CMD_TERM, "AT+HTTPTERM\r\n",
                           "\r\nOK\r\n", "\r\nERROR\r\n",
                           cb_http_term, HTTP_TIMEOUT_SHORT_MS);
        return;
    }
    if (s_http.http_datalen > sizeof(s_http.data)) {
        /* Server/modem reported more data than our buffer (sized to
         * A7677S_HTTP_RANGE_SIZE, which the caller was told is the max
         * range_len) - should not happen if the Range header was honored,
         * but do not overflow s_http.data if it does. */
        log_error(TAG, "AT+HTTPACTION: datalen %lu exceeds buffer %u",
                  (unsigned long)s_http.http_datalen, (unsigned)sizeof(s_http.data));
        s_http.state = HTTP_STATE_TERM;
        http_send_dynamic(HTTP_CMD_TERM, "AT+HTTPTERM\r\n",
                           "\r\nOK\r\n", "\r\nERROR\r\n",
                           cb_http_term, HTTP_TIMEOUT_SHORT_MS);
        return;
    }

    /* Begin the read loop - see file header comment on the two-level
     * Range/Read chunking. read_offset/data_len both start at 0 (already
     * zeroed by the memset in a7677s_http_get_range()). */
    s_http.state = HTTP_STATE_READ;
    uint32_t remaining = s_http.http_datalen - s_http.read_offset;
    uint32_t this_read = (remaining < A7677S_HTTP_READ_CHUNK_SIZE) ? remaining : A7677S_HTTP_READ_CHUNK_SIZE;
    snprintf(s_http_dyn_cmd_buf, sizeof(s_http_dyn_cmd_buf),
             "AT+HTTPREAD=%lu,%lu\r\n",
             (unsigned long)s_http.read_offset, (unsigned long)this_read);
    http_send_dynamic(HTTP_CMD_READ, s_http_dyn_cmd_buf,
                       "\r\nOK\r\n", "\r\nERROR\r\n",
                       cb_http_read, HTTP_TIMEOUT_SHORT_MS);
}

static void cb_http_read(modem_t *modem, const char *response,
                          modem_response_st_t res, void *arg)
{
    (void)arg;
    if (res != MODEM_RESPONSE_SUCCESS) {
        log_error(TAG, "AT+HTTPREAD failed/timed out at offset %lu (res=%d)",
                  (unsigned long)s_http.read_offset, res);
        s_http.state = HTTP_STATE_TERM;
        http_send_dynamic(HTTP_CMD_TERM, "AT+HTTPTERM\r\n",
                           "\r\nOK\r\n", "\r\nERROR\r\n",
                           cb_http_term, HTTP_TIMEOUT_SHORT_MS);
        return;
    }

    /* Expected shape: "+HTTPREAD:<len>\r\n<raw data, len bytes>\r\n\r\nOK\r\n"
     * (per a76xx_at_cmd.md's AT+HTTPREAD section - see a7677s_http.h file
     * header comment on why this whole line must fit under
     * MODEM_RX_BUFFER_SIZE). NOT verified against real hardware yet - the
     * exact framing (e.g. whether raw data can itself contain byte
     * sequences that look like "OK\r\n", which would matter if firmware
     * binary data ever coincidentally matches it) needs a real test with
     * an actual multi-hundred-KB file before this parsing is trusted, per
     * a7677s_http.h's note and this project's "real log beats datasheet"
     * rule. Parsing here locates "+HTTPREAD:" then the following "\r\n",
     * reads <len> raw bytes immediately after that, and does not attempt
     * to also validate the trailing "\r\n\r\nOK\r\n" content beyond what
     * modem_command's res_success match already confirmed (the presence of
     * "\r\nOK\r\n" somewhere in the buffer). */
    const char *marker = strstr(response, "+HTTPREAD:");
    if (!marker) {
        log_error(TAG, "AT+HTTPREAD: no +HTTPREAD: marker in response [%s]", modem->buff);
        s_http.state = HTTP_STATE_TERM;
        http_send_dynamic(HTTP_CMD_TERM, "AT+HTTPTERM\r\n",
                           "\r\nOK\r\n", "\r\nERROR\r\n",
                           cb_http_term, HTTP_TIMEOUT_SHORT_MS);
        return;
    }
    long chunk_len = strtol(marker + strlen("+HTTPREAD:"), NULL, 10);
    const char *line_end = strstr(marker, "\r\n");
    if (chunk_len <= 0 || !line_end) {
        log_error(TAG, "AT+HTTPREAD: bad chunk length or missing line terminator [%s]", modem->buff);
        s_http.state = HTTP_STATE_TERM;
        http_send_dynamic(HTTP_CMD_TERM, "AT+HTTPTERM\r\n",
                           "\r\nOK\r\n", "\r\nERROR\r\n",
                           cb_http_term, HTTP_TIMEOUT_SHORT_MS);
        return;
    }
    const char *data_start = line_end + 2; /* skip the "\r\n" after "+HTTPREAD:<len>" */

    if (s_http.data_len + (uint32_t)chunk_len > sizeof(s_http.data)) {
        log_error(TAG, "AT+HTTPREAD: chunk would overflow internal buffer (data_len=%lu chunk=%ld)",
                  (unsigned long)s_http.data_len, chunk_len);
        s_http.state = HTTP_STATE_TERM;
        http_send_dynamic(HTTP_CMD_TERM, "AT+HTTPTERM\r\n",
                           "\r\nOK\r\n", "\r\nERROR\r\n",
                           cb_http_term, HTTP_TIMEOUT_SHORT_MS);
        return;
    }

    memcpy(s_http.data + s_http.data_len, data_start, (size_t)chunk_len);
    s_http.data_len   += (uint32_t)chunk_len;
    s_http.read_offset += (uint32_t)chunk_len;

    if (s_http.read_offset < s_http.http_datalen) {
        /* More chunks remain in this range - issue the next AT+HTTPREAD at
         * the advanced offset (READMODE=1 allows re-reading the same
         * already-downloaded range at arbitrary offsets - see
         * a7677s_http.h file header comment; NOT verified against real
         * hardware yet). */
        uint32_t remaining = s_http.http_datalen - s_http.read_offset;
        uint32_t this_read = (remaining < A7677S_HTTP_READ_CHUNK_SIZE) ? remaining : A7677S_HTTP_READ_CHUNK_SIZE;
        snprintf(s_http_dyn_cmd_buf, sizeof(s_http_dyn_cmd_buf),
                 "AT+HTTPREAD=%lu,%lu\r\n",
                 (unsigned long)s_http.read_offset, (unsigned long)this_read);
        http_send_dynamic(HTTP_CMD_READ, s_http_dyn_cmd_buf,
                           "\r\nOK\r\n", "\r\nERROR\r\n",
                           cb_http_read, HTTP_TIMEOUT_SHORT_MS);
        return;
    }

    /* Range fully read - close the HTTP session before handing control
     * back to the caller (fota.c), so the next get_range() call's
     * AT+HTTPINIT is guaranteed to start from a clean state. */
    s_http.state = HTTP_STATE_TERM;
    http_send_dynamic(HTTP_CMD_TERM, "AT+HTTPTERM\r\n",
                       "\r\nOK\r\n", "\r\nERROR\r\n",
                       cb_http_term, HTTP_TIMEOUT_SHORT_MS);
}

static void cb_http_term(modem_t *modem, const char *response,
                          modem_response_st_t res, void *arg)
{
    (void)modem; (void)response; (void)arg;
    /* Reached from every error path above as well as the success path -
     * s_http.data_len/http_status reflect whatever was accumulated before
     * the point of failure (0 if we never got past ACTION). The result
     * code passed to http_op_done() must reflect why we ended up here, not
     * just "TERM succeeded" - tracked via s_http.http_status: 0 means we
     * never got a valid HTTP response at all (AT-layer problem somewhere
     * in INIT/PARA_URL/PARA_HDR/ACTION), non-zero-but-not-200/206 means a
     * real HTTP error, and a full successful read loop is the only path
     * that reaches here with http_status in {200,206} AND
     * read_offset == http_datalen. */
    if (res != MODEM_RESPONSE_SUCCESS) {
        /* AT+HTTPTERM itself failing is logged but does not change the
         * result already implied by how we got here - the caller mainly
         * needs to know whether ITS data is trustworthy, not whether
         * cleanup was perfectly clean. Session may be left open on the
         * modem side; a subsequent HTTPINIT failing is the caller's signal
         * something is stuck, at which point a full modem reset (existing
         * a7677s_ops recovery path) is the appropriate fix, not something
         * this module attempts itself. */
        log_warn(TAG, "AT+HTTPTERM failed/timed out (res=%d) - session may still be open", res);
    }

    if (s_http.http_status == 0) {
        http_op_done(A7677S_HTTP_RANGE_AT_ERROR, 0);
    } else if (s_http.http_status != 200 && s_http.http_status != 206) {
        http_op_done(A7677S_HTTP_RANGE_HTTP_ERROR, s_http.http_status);
    } else if (s_http.read_offset != s_http.http_datalen) {
        /* Got a valid HTTP response but the read loop was aborted midway
         * (buffer overflow guard or a mid-loop AT+HTTPREAD failure) -
         * surface as an AT error since the HTTP layer itself succeeded. */
        http_op_done(A7677S_HTTP_RANGE_AT_ERROR, s_http.http_status);
    } else {
        http_op_done(A7677S_HTTP_RANGE_OK, s_http.http_status);
    }
}