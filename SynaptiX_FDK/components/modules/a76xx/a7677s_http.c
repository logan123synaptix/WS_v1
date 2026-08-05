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

/* a7677s.c's pModem(dce) macro (== (modem_t*)&dce->base) is private to that
 * file, not exposed via a7677s.h - confirmed by grep, only defined in
 * a7677s.c itself. struct a7677s's base field IS public (a7677s.h, first
 * field, modem_t base) so this module can reach it directly; redefining
 * an equivalent macro here (rather than exposing a7677s.c's) keeps this
 * file's stated goal of not touching/depending on a7677s.c's internals -
 * same spirit as this file having its own command[]/state instead of
 * a7677s.c's CMD_DYNAMIC. */
#define pModem(dce) (&(dce)->base)

/* --- Private AT command table, entirely separate from a7677s.c's command[]
 * / CMD_DYNAMIC (see file header comment - deliberate, not an oversight). */
#define HTTP_CMD_INIT      0   /* AT+HTTPINIT */
#define HTTP_CMD_PARA_URL  1   /* AT+HTTPPARA="URL",<url> */
#define HTTP_CMD_PARA_SSL  2   /* AT+HTTPPARA="SSLCFG",<ctx> - only sent when url is https:// */
#define HTTP_CMD_PARA_HDR  3   /* AT+HTTPPARA="USERDATA",<range header> */
#define HTTP_CMD_ACTION    4   /* AT+HTTPACTION=0 (GET) */
#define HTTP_CMD_TERM      5   /* AT+HTTPTERM */
#define HTTP_CMD_COUNT     6

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
    HTTP_STATE_READ_RAW,     /* raw UART read for AT+HTTPREAD's binary response - see
                               * a7677s_http_poll()'s doc-comment and this file's header
                               * comment (2026-08-05 update) for why this replaced the
                               * old modem_send_command()/modem_poll()-based HTTP_STATE_READ */
    HTTP_STATE_TERM,
} http_state_t;

/* Sub-states within HTTP_STATE_READ_RAW - see a7677s_http_poll(). Every
 * AT+HTTPREAD response has the same 3-part shape:
 *   "AT+HTTPREAD=<off>,<len>\r\n\r\nOK\r\n\r\n+HTTPREAD:<chunk_len>\r\n"
 *   <chunk_len raw bytes, MAY CONTAIN ANY BYTE VALUE INCLUDING 0x00/0x45/etc>
 *   "\r\n\r\nOK\r\n"    (or "\r\n\r\n+HTTPREAD:0\r\n" instead when this was
 *                        the last chunk of the range - both start with
 *                        "\r\n\r\n", which is all this parser looks for)
 * Only the text portions (RAW_WAIT_EOK/RAW_WAIT_HEADER/RAW_WAIT_FOOTER) are
 * scanned byte-by-byte for a marker string - RAW_COPY_DATA counts exactly
 * chunk_len bytes and copies them verbatim regardless of content, never
 * calling strstr() on them. This is the entire point of this rewrite (see
 * file header comment). */
typedef enum {
    RAW_WAIT_ECHO_OK = 0,  /* skip the command echo + first "OK" (command accepted) */
    RAW_WAIT_HEADER,       /* find "+HTTPREAD:" then parse the decimal chunk_len after it */
    RAW_COPY_DATA,         /* copy exactly chunk_len raw bytes, no string scanning */
    RAW_WAIT_FOOTER,       /* skip trailing "\r\n\r\nOK\r\n" (or "...+HTTPREAD:0\r\n") */
} http_read_raw_substate_t;

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

    /* --- Raw-read state (HTTP_STATE_READ_RAW / a7677s_http_poll()) ---
     * See http_read_raw_substate_t's doc-comment above for the shape being
     * parsed. Reset at the start of every HTTP_STATE_READ_RAW entry (see
     * start_read_raw() below), not persisted across ranges. */
    http_read_raw_substate_t raw_substate;
    uint32_t raw_chunk_len;      /* parsed from "+HTTPREAD:<len>" in RAW_WAIT_HEADER,
                                   * how many raw data bytes RAW_COPY_DATA must still copy */
    uint32_t raw_chunk_copied;   /* how many of raw_chunk_len bytes RAW_COPY_DATA has
                                   * copied so far this chunk */
    /* Small line-scan buffer for the text portions (echo/OK/header/footer)
     * only - RAW_COPY_DATA never touches this, it copies straight from the
     * UART read buffer into s_http.data[]. Sized generously for the
     * longest text line this parser scans byte-by-byte in one sitting
     * (the command echo "AT+HTTPREAD=4294967295,4294967295\r\n" is the
     * longest, well under 64 bytes). */
    char     raw_line_buf[64];
    uint32_t raw_line_len;       /* bytes currently held in raw_line_buf */
    uint32_t raw_state_elapsed_ms; /* time spent in the current raw sub-state, for
                                     * per-substate timeout - see a7677s_http_poll() */

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
static void cb_http_para_ssl(modem_t *modem, const char *response, modem_response_st_t res, void *arg);
static void cb_http_para_hdr(modem_t *modem, const char *response, modem_response_st_t res, void *arg);
static void cb_http_action(modem_t *modem, const char *response, modem_response_st_t res, void *arg);
static void start_read_raw(void);
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

/* --- a7677s_http_ssl_configure() -----------------------------------------
 * Separate static state from s_http above, since this is a one-time
 * boot-time call (not per-range) and its own in-flight/idle tracking must
 * not be confused with a7677s_http_is_busy()'s per-range meaning. Both
 * still ultimately serialize through the same underlying modem_t command
 * channel (isBusy) - modem_send_command() itself is what actually prevents
 * this from overlapping with a range download or an MQTT operation, same
 * as everywhere else in this file. */
static struct {
    bool     busy;
    a7677s_http_ssl_cfg_cb_t cb;
    void    *ctx;
} s_ssl_cfg;

static void ssl_cfg_done(modem_ops_result_t result)
{
    a7677s_http_ssl_cfg_cb_t cb = s_ssl_cfg.cb;
    void *ctx = s_ssl_cfg.ctx;
    s_ssl_cfg.cb   = NULL;
    s_ssl_cfg.busy = false;
    if (cb) cb(result, ctx);
}

static void cb_ssl_sni(modem_t *modem, const char *response,
                        modem_response_st_t res, void *arg)
{
    (void)modem; (void)response; (void)arg;
    if (res != MODEM_RESPONSE_SUCCESS) {
        log_error(TAG, "AT+CSSLCFG=enableSNI failed (res=%d)", res);
        ssl_cfg_done(MODEM_OPS_ERROR);
        return;
    }
    log_info(TAG, "SSL context %u configured (authmode=0, SNI=1)", A7677S_HTTP_SSL_CTX_INDEX);
    ssl_cfg_done(MODEM_OPS_OK);
}

static void cb_ssl_authmode(modem_t *modem, const char *response,
                             modem_response_st_t res, void *arg)
{
    static char cmd_buf[64];
    (void)modem; (void)response; (void)arg;
    if (res != MODEM_RESPONSE_SUCCESS) {
        log_error(TAG, "AT+CSSLCFG=authmode failed (res=%d)", res);
        ssl_cfg_done(MODEM_OPS_ERROR);
        return;
    }

    /* AT+CSSLCFG="enableSNI",<ctx>,1 - see A7677S_HTTP_SSL_CTX_INDEX's
     * doc-comment in a7677s_http.h for why this is required against
     * CDN-fronted hosts (GitHub raw, ngrok). NOT tested against real
     * hardware yet - if the module's real response text differs from the
     * bare "\r\nOK\r\n" assumed here (unlikely for a simple config command,
     * but every other assumption in this file carries the same caveat, see
     * a7677s_http.h's file header comment), this needs correcting against
     * a real log. */
    snprintf(cmd_buf, sizeof(cmd_buf), "AT+CSSLCFG=\"enableSNI\",%u,1\r\n",
             A7677S_HTTP_SSL_CTX_INDEX);
    s_ssl_command[SSL_CMD_SNI].cmd         = cmd_buf;
    s_ssl_command[SSL_CMD_SNI].res_success = "\r\nOK\r\n";
    s_ssl_command[SSL_CMD_SNI].res_fail    = "\r\nERROR\r\n";
    s_ssl_command[SSL_CMD_SNI].callback    = cb_ssl_sni;
    s_ssl_command[SSL_CMD_SNI].arg         = NULL;
    modem_send_command(pModem(s_http.dce), &s_ssl_command[SSL_CMD_SNI], HTTP_TIMEOUT_SHORT_MS);
}

int a7677s_http_ssl_configure(a7677s_t *dce, a7677s_http_ssl_cfg_cb_t cb, void *ctx)
{
    static char cmd_buf[64];

    if (!dce || !cb) {
        log_error(TAG, "ssl_configure(): invalid arguments");
        return -1;
    }
    if (s_ssl_cfg.busy) {
        log_warn(TAG, "ssl_configure(): already in progress");
        return -1;
    }
    if (modem_is_busy(pModem(dce))) {
        log_warn(TAG, "ssl_configure(): modem command channel busy with another operation");
        return -1;
    }

    s_ssl_cfg.busy = true;
    s_ssl_cfg.cb   = cb;
    s_ssl_cfg.ctx  = ctx;
    /* Reuses s_http.dce as the single-instance modem pointer for the
     * cb_ssl_* chain below, same as everywhere else in this file - fine
     * since a7677s_http_ssl_configure() and a7677s_http_get_range() are
     * never expected to run concurrently (both funnel through the one
     * shared modem command channel regardless). */
    s_http.dce = dce;

    /* AT+CSSLCFG="authmode",<ctx>,0 - explicit even though 0 is the
     * documented default (a76xx_at_cmd.md 19.2.1), so this function's
     * behavior does not depend on assuming the module's power-on defaults
     * match the datasheet (this project's "real log beats datasheet" rule
     * applies just as much to "what the default already is" as to
     * response text formatting). */
    snprintf(cmd_buf, sizeof(cmd_buf), "AT+CSSLCFG=\"authmode\",%u,0\r\n",
             A7677S_HTTP_SSL_CTX_INDEX);
    s_ssl_command[SSL_CMD_AUTHMODE].cmd         = cmd_buf;
    s_ssl_command[SSL_CMD_AUTHMODE].res_success = "\r\nOK\r\n";
    s_ssl_command[SSL_CMD_AUTHMODE].res_fail    = "\r\nERROR\r\n";
    s_ssl_command[SSL_CMD_AUTHMODE].callback    = cb_ssl_authmode;
    s_ssl_command[SSL_CMD_AUTHMODE].arg         = NULL;
    modem_send_command(pModem(dce), &s_ssl_command[SSL_CMD_AUTHMODE], HTTP_TIMEOUT_SHORT_MS);
    return 0;
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
         * an HTTP session is still open. See abort_read_raw()'s call site
         * (a7677s_http_poll()) for the same reasoning applied after
         * ACTION/READ failures in the raw-read path. */
        s_http.state = HTTP_STATE_TERM;
        http_send_dynamic(HTTP_CMD_TERM, "AT+HTTPTERM\r\n",
                           "\r\nOK\r\n", "\r\nERROR\r\n",
                           cb_http_term, HTTP_TIMEOUT_SHORT_MS);
        return;
    }

    if (s_http.is_https) {
        /* Point this HTTP session at the SSL context that
         * a7677s_http_ssl_configure() should already have set up (authmode
         * + SNI) - see a7677s_http.h's A7677S_HTTP_SSL_CTX_INDEX
         * doc-comment for why this is a separate context from MQTT's. If
         * a7677s_http_ssl_configure() was never called, this AT+HTTPPARA
         * itself is expected to still succeed (it just selects a context
         * index, does not validate its contents) - the failure would only
         * surface later at AT+HTTPACTION as a TLS handshake error, not
         * here. fota.c's design doc/next_prompt.md should call out this
         * ordering requirement explicitly once fota.c itself is written. */
        s_http.state = HTTP_STATE_PARA_SSL;
        snprintf(s_http_dyn_cmd_buf, sizeof(s_http_dyn_cmd_buf),
                 "AT+HTTPPARA=\"SSLCFG\",%u\r\n", A7677S_HTTP_SSL_CTX_INDEX);
        http_send_dynamic(HTTP_CMD_PARA_SSL, s_http_dyn_cmd_buf,
                           "\r\nOK\r\n", "\r\nERROR\r\n",
                           cb_http_para_ssl, HTTP_TIMEOUT_SHORT_MS);
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

static void cb_http_para_ssl(modem_t *modem, const char *response,
                              modem_response_st_t res, void *arg)
{
    (void)modem; (void)arg;
    if (res != MODEM_RESPONSE_SUCCESS) {
        log_error(TAG, "AT+HTTPPARA=SSLCFG failed (res=%d)", res);
        s_http.state = HTTP_STATE_TERM;
        http_send_dynamic(HTTP_CMD_TERM, "AT+HTTPTERM\r\n",
                           "\r\nOK\r\n", "\r\nERROR\r\n",
                           cb_http_term, HTTP_TIMEOUT_SHORT_MS);
        return;
    }

    /* Same next step as the non-https branch in cb_http_para_url() above -
     * both paths converge on PARA_HDR from here. */
    s_http.state = HTTP_STATE_PARA_HDR;
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
     * set res_success to the "+HTTPACTION: 0," prefix (method=0 is GET),
     * not "\r\nOK\r\n" - matching that established pattern exactly, so this
     * callback only fires once the real result line has arrived, not on
     * the earlier bare OK. CONFIRMED against real hardware log (2026-08-05):
     * the module sends "+HTTPACTION: 0,206,2048" - WITH a space after the
     * colon, exactly the CMQTTSTART-style mismatch this comment used to
     * warn about before it was verified. Datasheet text
     * (a76xx_at_cmd.md) shows no space; real module output does - this
     * project's "real log beats datasheet" rule applies, matching the
     * space here rather than the documented format. */
    http_send_dynamic(HTTP_CMD_ACTION, "AT+HTTPACTION=0\r\n",
                       "+HTTPACTION: 0,", "\r\nERROR\r\n",
                       cb_http_action, HTTP_TIMEOUT_ACTION_MS);
}

static void cb_http_action(modem_t *modem, const char *response,
                            modem_response_st_t res, void *arg)
{
    const char *p;
    int status;
    unsigned long datalen;

    (void)arg;
    if (res != MODEM_RESPONSE_SUCCESS) {
        log_error(TAG, "AT+HTTPACTION failed/timed out (res=%d)", res);
        s_http.state = HTTP_STATE_TERM;
        http_send_dynamic(HTTP_CMD_TERM, "AT+HTTPTERM\r\n",
                           "\r\nOK\r\n", "\r\nERROR\r\n",
                           cb_http_term, HTTP_TIMEOUT_SHORT_MS);
        return;
    }

    /* Parse "+HTTPACTION: 0,<statuscode>,<datalen>" out of modem->buff
     * (response points into it) - CONFIRMED real format includes a space
     * after the colon (real hardware log, 2026-08-05: "+HTTPACTION:
     * 0,206,2048"), matching res_success above. Not using response
     * directly with sscanf's %d on the whole buffer in case of leading
     * noise - locate the marker first, same defensive style as
     * urc_process_header_line()'s strchr(line, ':') + sscanf(p+1, ...) in
     * a7677s.c. */
    p = strstr(response, "+HTTPACTION: 0,");
    status = 0;
    datalen = 0;
    if (!p || sscanf(p, "+HTTPACTION: 0,%d,%lu", &status, &datalen) != 2) {
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
     * zeroed by the memset in a7677s_http_get_range()).
     *
     * BUG FIX (2026-08-05): previously sent AT+HTTPREAD through
     * http_send_dynamic()/modem_send_command(), relying on modem_poll()'s
     * strstr(modem->buff, "\r\nOK\r\n") to detect completion. CONFIRMED on
     * real hardware this fails: the actual firmware file being downloaded
     * (TrackingFirmWare.bin) contains a long run of a single repeated byte
     * value (0x45 'E') that, when it lands inside one 400-byte HTTPREAD
     * chunk, contains no "OK"/"+HTTPREAD:" text anywhere in that chunk -
     * strstr() never matches, the command times out (modem->buff dumped in
     * the TIMEOUT log line as a wall of 'E' characters - not corrupted
     * memory, the actual binary file content), and the download fails.
     * This is not a rare edge case for real firmware binaries (padding,
     * zero-initialized data sections, repeated instruction patterns) -
     * text-based framing detection is fundamentally unsafe for binary
     * payloads. Switched to start_read_raw() (below), a hand-rolled UART
     * reader that counts exactly datalen raw bytes instead of scanning for
     * a marker string inside them - see http_read_raw_substate_t's
     * doc-comment. */
    s_http.state = HTTP_STATE_READ_RAW;
    start_read_raw();
}

/* Timeout for the ENTIRE HTTP_STATE_READ_RAW sequence for one AT+HTTPREAD
 * chunk (echo+OK, header, data, footer combined) - generous since this
 * covers UART transfer of up to A7677S_HTTP_READ_CHUNK_SIZE raw bytes plus
 * framing, at whatever baud rate this UART runs, not just a short AT
 * command's usual turnaround. Same order of magnitude as
 * HTTP_TIMEOUT_SHORT_MS but kept separate/named for clarity, since this
 * timeout now guards a hand-rolled loop instead of modem_send_command()'s
 * built-in one. */
#define HTTP_READ_RAW_TIMEOUT_MS   5000U

/* Begins (or restarts, for the next chunk within the same range)
 * HTTP_STATE_READ_RAW: sends "AT+HTTPREAD=<offset>,<size>" directly via
 * sx_uart_write() - deliberately bypassing modem_send_command()/
 * modem_poll() for this command only (every other AT+HTTP* command in this
 * file still goes through the normal http_send_dynamic() path, which is
 * fine since none of their responses carry arbitrary binary payload).
 * Manually claims modem->isBusy so modem_poll() (called elsewhere in the
 * same tick via a7677s_poll(), see a7677s.c) does not also try to read
 * this UART concurrently - released again in finish_read_raw() below. */
static void start_read_raw(void)
{
    uint32_t remaining = s_http.http_datalen - s_http.read_offset;
    uint32_t this_read = (remaining < A7677S_HTTP_READ_CHUNK_SIZE) ? remaining : A7677S_HTTP_READ_CHUNK_SIZE;

    snprintf(s_http_dyn_cmd_buf, sizeof(s_http_dyn_cmd_buf),
             "AT+HTTPREAD=%lu,%lu\r\n",
             (unsigned long)s_http.read_offset, (unsigned long)this_read);

    s_http.raw_substate         = RAW_WAIT_ECHO_OK;
    s_http.raw_chunk_len        = 0;
    s_http.raw_chunk_copied     = 0;
    s_http.raw_line_len         = 0;
    s_http.raw_state_elapsed_ms = 0;

    log_debug(TAG, "HTTP RAW CMD: %s", s_http_dyn_cmd_buf);
    pModem(s_http.dce)->isBusy = 1;   /* claim the channel - see file header comment above */
    sx_uart_flush(&pModem(s_http.dce)->uart);
    sx_uart_write(&pModem(s_http.dce)->uart,
                   (const uint8_t *)s_http_dyn_cmd_buf, strlen(s_http_dyn_cmd_buf));
}

/* Releases the manually-claimed modem->isBusy and either issues the next
 * chunk's AT+HTTPREAD (more of this range still unread) or moves on to
 * HTTP_STATE_TERM (range fully read) - the same two-way branch
 * cb_http_read() used to make at the end of its success path, just
 * relocated here since a7677s_http_poll() drives this now instead of a
 * modem command callback. */
static void finish_read_raw_chunk(void)
{
    pModem(s_http.dce)->isBusy = 0;

    if (s_http.read_offset < s_http.http_datalen) {
        start_read_raw();
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

/* Aborts the in-flight raw read on error/timeout, same cleanup path every
 * other error branch in this file uses (HTTPTERM then surface the error to
 * the caller via cb_http_term()'s http_status==0 case). */
static void abort_read_raw(const char *why)
{
    log_error(TAG, "AT+HTTPREAD (raw): %s at offset %lu", why, (unsigned long)s_http.read_offset);
    pModem(s_http.dce)->isBusy = 0;
    s_http.state = HTTP_STATE_TERM;
    http_send_dynamic(HTTP_CMD_TERM, "AT+HTTPTERM\r\n",
                       "\r\nOK\r\n", "\r\nERROR\r\n",
                       cb_http_term, HTTP_TIMEOUT_SHORT_MS);
}

/* Drives HTTP_STATE_READ_RAW - must be called every tick alongside
 * modem_handle_poll()/a7677s_poll() (see a7677s_http.h's a7677s_http_poll()
 * doc-comment), NOT instead of it: this only takes over the UART channel
 * for the specific duration of one AT+HTTPREAD raw read (isBusy claimed in
 * start_read_raw(), released in finish_read_raw_chunk()/abort_read_raw()),
 * every other HTTP_STATE_* still relies on modem_poll() via
 * modem_handle_poll() as before.
 *
 * Parses the 3-part response shape documented on http_read_raw_substate_t
 * above. RAW_COPY_DATA is the only sub-state that does NOT scan for a
 * marker string - it counts exactly raw_chunk_len bytes and copies them
 * verbatim into s_http.data[], regardless of content (this is the entire
 * fix for the 2026-08-05 bug - see cb_http_action()'s doc-comment on why
 * strstr()-based framing was unsafe for binary firmware payloads). */
void a7677s_http_poll(a7677s_t *dce, uint32_t delta_ms)
{
    sx_uart_t *uart;
    uint8_t byte;
    int n;

    if (!dce || s_http.state != HTTP_STATE_READ_RAW) {
        return;
    }

    uart = &pModem(s_http.dce)->uart;

    s_http.raw_state_elapsed_ms += delta_ms;
    if (s_http.raw_state_elapsed_ms >= HTTP_READ_RAW_TIMEOUT_MS) {
        abort_read_raw("raw read timed out");
        return;
    }

    /* Read one byte at a time - simplest correct implementation for a
     * byte-oriented state machine that must switch behavior (text-scan vs
     * verbatim-copy) mid-stream at an exact byte boundary. UART throughput
     * at typical AT-command baud rates (many kbps) means this is not a
     * meaningful bottleneck against HTTP_READ_RAW_TIMEOUT_MS above; a
     * batched sx_uart_read() into a temporary buffer would save calls but
     * adds complexity (re-slicing a batch across a sub-state transition)
     * for no measured benefit yet. Revisit if real hardware testing shows
     * this is too slow for the full chunk size. */
    n = sx_uart_read(uart, &byte, 1, 0);
    if (n <= 0) {
        return; /* nothing new yet, try again next tick */
    }
    s_http.raw_state_elapsed_ms = 0; /* got a byte, reset this sub-state's timeout */

    switch (s_http.raw_substate) {

    case RAW_WAIT_ECHO_OK:
        /* Skip everything up through the command's own "OK" (echo of
         * "AT+HTTPREAD=...\r\n" followed by "\r\nOK\r\n" per every other
         * AT command's shape in this codebase) - accumulate into
         * raw_line_buf and look for "OK\r\n" as a plain substring, safe
         * here since nothing binary has appeared yet (we are still in the
         * command-echo/acknowledgement portion, always plain ASCII). */
        if (s_http.raw_line_len < sizeof(s_http.raw_line_buf) - 1) {
            s_http.raw_line_buf[s_http.raw_line_len++] = (char)byte;
            s_http.raw_line_buf[s_http.raw_line_len] = '\0';
        } else {
            /* Shift buffer left by 1 to keep scanning a sliding window,
             * rather than growing unboundedly or giving up - the marker
             * we need ("OK\r\n") is short, a small sliding window suffices. */
            memmove(s_http.raw_line_buf, s_http.raw_line_buf + 1, sizeof(s_http.raw_line_buf) - 2);
            s_http.raw_line_buf[sizeof(s_http.raw_line_buf) - 2] = (char)byte;
            s_http.raw_line_buf[sizeof(s_http.raw_line_buf) - 1] = '\0';
        }
        if (strstr(s_http.raw_line_buf, "OK\r\n")) {
            s_http.raw_substate = RAW_WAIT_HEADER;
            s_http.raw_line_len = 0;
        }
        return;

    case RAW_WAIT_HEADER:
        /* Find "+HTTPREAD:" then parse the decimal chunk_len that follows,
         * up to the "\r\n" ending that line - still plain ASCII, safe to
         * scan as a string. Same sliding-window accumulation as above. */
        if (s_http.raw_line_len < sizeof(s_http.raw_line_buf) - 1) {
            s_http.raw_line_buf[s_http.raw_line_len++] = (char)byte;
            s_http.raw_line_buf[s_http.raw_line_len] = '\0';
        } else {
            memmove(s_http.raw_line_buf, s_http.raw_line_buf + 1, sizeof(s_http.raw_line_buf) - 2);
            s_http.raw_line_buf[sizeof(s_http.raw_line_buf) - 2] = (char)byte;
            s_http.raw_line_buf[sizeof(s_http.raw_line_buf) - 1] = '\0';
        }
        {
            const char *marker = strstr(s_http.raw_line_buf, "+HTTPREAD:");
            if (marker && strstr(marker, "\r\n")) {
                long chunk_len = strtol(marker + strlen("+HTTPREAD:"), NULL, 10);
                if (chunk_len < 0 || (uint32_t)chunk_len > A7677S_HTTP_READ_CHUNK_SIZE) {
                    abort_read_raw("bad or oversized chunk_len in +HTTPREAD: header");
                    return;
                }
                if (s_http.data_len + (uint32_t)chunk_len > sizeof(s_http.data)) {
                    abort_read_raw("chunk would overflow internal data[] buffer");
                    return;
                }
                s_http.raw_chunk_len    = (uint32_t)chunk_len;
                s_http.raw_chunk_copied = 0;
                s_http.raw_substate     = (chunk_len == 0) ? RAW_WAIT_FOOTER : RAW_COPY_DATA;
                /* chunk_len==0 happens on the final "+HTTPREAD:0" marker
                 * some ranges send after the last real chunk - nothing to
                 * copy, go straight to skipping the footer. */
            }
        }
        return;

    case RAW_COPY_DATA:
        /* The core fix: copy this byte verbatim into s_http.data[] no
         * matter what value it is (0x00, 0x45 'E', anything) - no string
         * scanning happens here at all. */
        s_http.data[s_http.data_len++] = byte;
        s_http.raw_chunk_copied++;
        if (s_http.raw_chunk_copied >= s_http.raw_chunk_len) {
            s_http.raw_substate = RAW_WAIT_FOOTER;
            s_http.raw_line_len = 0;
        }
        return;

    case RAW_WAIT_FOOTER:
        /* Skip the trailing "\r\n\r\nOK\r\n" (or "\r\n\r\n+HTTPREAD:0\r\n"
         * for the last chunk - either way this parser only needs to see
         * "OK\r\n" appear to know the response is fully consumed; a
         * trailing "+HTTPREAD:0" line, if present, is itself followed by
         * its own "\r\n" only, not "OK\r\n" - PER A76XX_AT_CMD.MD's
         * example the module still emits nothing further after that, so
         * this case is handled by the timeout naturally completing the
         * chunk via read_offset bookkeeping in the caller, NOT by this
         * substate waiting for text that will never come. To keep this
         * robust either way, this substate advances on EITHER seeing
         * "OK\r\n" OR accumulating a line that starts with "+HTTPREAD:0"
         * followed by "\r\n". Not verified byte-for-byte against every
         * possible module firmware revision - see this file's
         * "real log beats datasheet" note if a real capture ever shows a
         * third shape here. */
        if (s_http.raw_line_len < sizeof(s_http.raw_line_buf) - 1) {
            s_http.raw_line_buf[s_http.raw_line_len++] = (char)byte;
            s_http.raw_line_buf[s_http.raw_line_len] = '\0';
        } else {
            memmove(s_http.raw_line_buf, s_http.raw_line_buf + 1, sizeof(s_http.raw_line_buf) - 2);
            s_http.raw_line_buf[sizeof(s_http.raw_line_buf) - 2] = (char)byte;
            s_http.raw_line_buf[sizeof(s_http.raw_line_buf) - 1] = '\0';
        }
        if (strstr(s_http.raw_line_buf, "OK\r\n") ||
            strstr(s_http.raw_line_buf, "+HTTPREAD:0\r\n")) {
            s_http.read_offset += s_http.raw_chunk_len;
            finish_read_raw_chunk();
        }
        return;
    }
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