#ifndef A7677S_HTTP_H
#define A7677S_HTTP_H
#ifdef __cplusplus
extern "C" {
#endif

#include "a7677s.h"
#include "modem_ops.h"
#include <stdint.h>
#include <stdbool.h>

/* HTTP support for A7677S, built specifically for fota.c's range-download
 * use case (see SynaptiX_FDK/app/user/fota/fota.h) - NOT a general-purpose
 * HTTP client. Included directly by fota.c (no modem_ops_t vtable entry),
 * same precedent as ota_trigger.c including sx_flash.h directly: a feature
 * tied to one specific piece of hardware (this modem's AT+HTTP command set)
 * is allowed to skip the generic abstraction layer, since sim76xx.c has no
 * live instance anywhere in this codebase (confirmed by grep - only shows
 * up in historical comments) and adding a vtable entry for a single real
 * implementation buys nothing today. See a7677s.c's command[] table history
 * for the analogous MQTT precedent this mirrors.
 *
 * --- Why chunking is capped far below the 4-8KB the FOTA design doc assumed ---
 *
 * AT+HTTPREAD's response (the "+HTTPREAD:<len>\r\n<data>\r\n\r\nOK\r\n" line)
 * goes through modem_send_command()/modem_poll() like any other AT command
 * -- it is NOT a URC (confirmed: it is a direct reply to an AT command this
 * driver just sent, not an unsolicited push from the modem). modem_poll()
 * (modem.c) only accepts bytes while
 *   modem->buff_id + available < MODEM_RX_BUFFER_SIZE (512, modem.h)
 * and has NO fallback for a response that doesn't fit - it silently stops
 * accepting bytes and the command eventually times out, with no distinct
 * "overflow" error. So the entire "+HTTPREAD:...OK\r\n" line, header bytes
 * included, must stay under 512 bytes, which puts the actual firmware-data
 * portion at roughly A7677S_HTTP_READ_CHUNK_SIZE (see below), not the
 * multi-KB chunk the original design doc assumed before this constraint was
 * confirmed by reading modem_poll() directly.
 *
 * This is independent of A7677S_HTTP_RANGE_SIZE: one HTTP Range request (via
 * AT+HTTPACTION, the expensive/slow step - up to 120s MaxResponseTime per
 * a76xx_at_cmd.md) can still ask for a larger span (e.g. 4KB), then get read
 * back out of the modem's own internal buffer via several back-to-back
 * AT+HTTPREAD calls at consecutive offsets (READMODE=1 permits re-reading
 * the same already-downloaded range at different offsets) - this amortizes
 * the slow AT+HTTPACTION round-trip across many cheap AT+HTTPREAD calls,
 * instead of doing one AT+HTTPACTION per tiny AT+HTTPREAD chunk. fota.c
 * drives that two-level loop; this module only exposes the single-level
 * primitives (start range, read chunk, end range). */

/* Max HTTP response chunk readable in one AT+HTTPREAD, sized to leave
 * headroom under MODEM_RX_BUFFER_SIZE (512) for the "+HTTPREAD:<len>\r\n"
 * prefix and trailing "\r\n\r\nOK\r\n" - both variable-length in principle
 * (larger <len> values use more decimal digits) but bounded in practice
 * since this value itself never exceeds 4 digits. 400 leaves comfortable
 * margin (roughly 90 bytes) for prefix+suffix overhead plus whatever stale
 * bytes might transiently coexist during a partial UART read, without
 * having been measured against the real module yet - see fota.h's note
 * that a real multi-hundred-KB download must be tested against real
 * hardware before this constant is trusted at its current value. */
#define A7677S_HTTP_READ_CHUNK_SIZE   400U

/* Size of one AT+HTTPACTION Range request. Independent of the READ_CHUNK
 * constraint above (see file header comment) - this may be increased later
 * to reduce the number of slow AT+HTTPACTION round-trips, as long as it
 * still fits comfortably under the modem's own ~1MB internal response
 * buffer limit (per a76xx_at_cmd.md's READMODE note) and under
 * FOTA_MAX_FIRMWARE_SIZE (fota.h). Kept equal to one erase sector (8KB, see
 * BOOTLOADER_WS/bootloader/flash_define.h) is NOT required here - fota.c is
 * responsible for its own sector-boundary bookkeeping against whatever
 * range size this module is configured with. */
#define A7677S_HTTP_RANGE_SIZE        8192U

/* Max length of a URL passed to a7677s_http_get_range(), including any
 * query string, matching AT+HTTPPARA="URL",<url> per a76xx_at_cmd.md
 * (no explicit documented max; 256 is a conservative working limit shared
 * with A7677S_MQTT_BROKER_MAX's precedent in a7677s.h). */
#define A7677S_HTTP_URL_MAX           257U

/* --- HTTPS support ---
 * Per a76xx_at_cmd.md 16.2.3 (AT+HTTPPARA "URL"): the <url> parameter
 * itself may start with "https://" instead of "http://" - AT+HTTPACTION
 * then transparently does the TLS handshake, no separate "start HTTPS
 * mode" command exists (confirmed by reading the manual directly, this is
 * simpler than a7677s.c's MQTT SSL path, which needs a whole separate
 * CMQTTSSLCFG/CSSLCFG bind sequence before CONNECT).
 *
 * SSL context (id 0-9, a76xx_at_cmd.md chapter 19, AT+CSSLCFG) still needs
 * configuring once before the first HTTPS a7677s_http_get_range() call,
 * because two of its defaults are wrong for a server whose cert chains up
 * to a public root CA the module doesn't have preloaded, and whose
 * hostname is shared across multiple certs behind a CDN (GitHub raw
 * content, ngrok, etc):
 *   - authmode default (0) = "no authentication" - this is actually FINE
 *     for a plain download from a public HTTPS host (no client cert
 *     needed, and "no authentication" here means the module does not
 *     verify the server's cert chain, not that TLS itself is skipped) -
 *     no AT+CCERTDOWN of any root CA is required for this project's use
 *     case. Kept at 0, not changed by a7677s_http_ssl_configure() below.
 *   - enableSNI default (0) = SNI OFF. This one DOES need to be flipped to
 *     1: a CDN-backed HTTPS host serves different certs for different
 *     hostnames off the same IP, and without SNI the module's TLS
 *     ClientHello carries no hostname, so the server has no way to know
 *     which cert to present - this is expected to cause a handshake
 *     failure (a76xx_at_cmd.md's errcode 715, "Handshake failed") against
 *     GitHub raw / ngrok specifically, NOT a generic "some servers need
 *     it" caveat - confirmed necessary from how CDN-fronted TLS works, not
 *     yet confirmed against this specific module's real behavior (see
 *     a7677s_http_ssl_configure()'s doc-comment - NOT tested against real
 *     hardware yet, same caveat as the rest of this file). */
#define A7677S_HTTP_SSL_CTX_INDEX     1U  /* deliberately NOT 0 - MQTT's
                                            * A7677S_SSL_CTX_INDEX (a7677s.h)
                                            * already uses context 0; a
                                            * separate context for HTTP
                                            * avoids the two features ever
                                            * fighting over the same
                                            * context's authmode/SNI config
                                            * if both are active in the same
                                            * build (this project's fota.c
                                            * and MQTT client run at
                                            * different times, not
                                            * concurrently, but there is no
                                            * reason to force them to share
                                            * a context when 0-9 are
                                            * available and unused). */

/* Result of one a7677s_http_get_range() call, delivered via
 * a7677s_http_range_cb_t. Distinct from modem_ops_result_t's generic
 * OK/ERROR/TIMEOUT/BUSY because the caller (fota.c) needs to distinguish
 * "modem/AT-layer problem, maybe worth retrying the same range" from
 * "server responded but not with 200/206" from "range genuinely read fewer
 * bytes than requested" (e.g. final range of the file, which is expected
 * and not an error - fota.c must check this against the server-provided
 * `size` itself, this module has no notion of total file size). */
typedef enum {
    A7677S_HTTP_RANGE_OK = 0,       /* HTTP 200 or 206, range downloaded and readable */
    A7677S_HTTP_RANGE_HTTP_ERROR,   /* modem completed the request but status code was not 200/206 */
    A7677S_HTTP_RANGE_AT_ERROR,     /* an AT command in the sequence failed/timed out */
    A7677S_HTTP_RANGE_BUSY          /* This module's own internal command channel (private
                                      * command[]/state in a7677s_http.c, separate from
                                      * a7677s.c's CMD_DYNAMIC - see a7677s_http.c file header
                                      * comment) was already in use by another in-flight
                                      * a7677s_http_get_range()/a7677s_http_ssl_configure() call,
                                      * OR the shared modem_t command channel itself was busy
                                      * with an unrelated operation (MQTT, init sequence) -
                                      * either way, caller must wait and retry, never call a
                                      * second HTTP op while one is already in flight */
} a7677s_http_range_result_t;

/* Fired once per a7677s_http_get_range() call, after AT+HTTPTERM has been
 * sent and the whole HTTPINIT..HTTPTERM sequence for that one range is
 * complete (successfully or not). status_code is the raw HTTP status from
 * +HTTPACTION (0 if the AT sequence never got that far - check result
 * instead). data/data_len describe the single buffer inside this module
 * holding everything read back via AT+HTTPREAD for this range (may be
 * shorter than the requested range size - e.g. last range in the file, or
 * server ignoring the Range header and returning less than asked; fota.c
 * must not assume data_len == requested range size). Buffer is only valid
 * for the duration of this callback - fota.c must copy/write it to flash
 * before returning. */
typedef void (*a7677s_http_range_cb_t)(a7677s_http_range_result_t result,
                                        int status_code,
                                        const uint8_t *data,
                                        uint32_t data_len,
                                        void *ctx);

/* Downloads bytes [start_offset, start_offset+range_len) from url via one
 * HTTP Range request, and reads the result back into an internal buffer
 * (sized A7677S_HTTP_RANGE_SIZE) via as many AT+HTTPREAD calls as needed
 * (each capped at A7677S_HTTP_READ_CHUNK_SIZE - see file header comment).
 * Non-blocking: runs its own AT+HTTPINIT -> HTTPPARA(URL) ->
 * HTTPPARA(USERDATA=Range header) -> HTTPACTION=0 -> HTTPREAD(s) ->
 * HTTPTERM sequence across multiple a7677s_poll() ticks, firing cb exactly
 * once at the end (mirrors mqtt_op_done()'s "fire once, clear" contract in
 * a7677s.c).
 *
 * range_len must be <= A7677S_HTTP_RANGE_SIZE - caller (fota.c) is
 * responsible for splitting a full-file download into range_len-sized
 * calls itself; this module does not loop across ranges on its own.
 *
 * Returns 0 if the request was accepted (cb will fire later, asynchronously
 * - never synchronously from within this call, same "never call back
 * inline" contract as a7677s_mqtt_publish()/a7677s_ops functions), or -1 if
 * rejected immediately (e.g. a7677s_t's command channel already busy with
 * another operation - caller should retry later, no cb fires in this case). */
int a7677s_http_get_range(a7677s_t *dce,
                           const char *url,
                           uint32_t start_offset,
                           uint32_t range_len,
                           a7677s_http_range_cb_t cb,
                           void *ctx);

/* Must be called every tick, ALONGSIDE (not instead of) the normal
 * board.modem.ops->poll()/modem_handle_poll() chain that already drives
 * a7677s.c's own state machine - see test_http.c's test_http_poll() for
 * the calling convention this expects (modem_handle_poll() first, then
 * this, every tick, unconditionally; this function itself is a no-op
 * unless a get_range() call has reached its AT+HTTPREAD phase).
 *
 * BUG FIX (2026-08-05): AT+HTTPREAD's response can contain arbitrary
 * binary firmware data with no guaranteed text framing inside it
 * (confirmed on real hardware - see cb_http_action()'s doc-comment for
 * the exact failure this replaced: modem_poll()'s strstr()-based
 * completion detection silently failed and timed out whenever a chunk's
 * raw bytes happened not to contain "OK"/"+HTTPREAD:" text, which a real
 * firmware .bin's repeated-byte-value regions hit in practice, not just in
 * theory). This function reads the modem UART directly, byte by byte,
 * switching between text-scanning and verbatim-copy sub-states at exact
 * byte-counted boundaries instead of searching for a marker string inside
 * binary data - see a7677s_http.c's http_read_raw_substate_t for the full
 * state shape. delta_ms is the same tick delta passed to
 * modem_handle_poll(), used for this function's own read-timeout
 * tracking. */
void a7677s_http_poll(a7677s_t *dce, uint32_t delta_ms);

/* True while an a7677s_http_get_range() call is in flight (its cb has not
 * fired yet). fota.c should check this (or rely solely on the -1 return
 * from a7677s_http_get_range() above - either is sufficient) before issuing
 * the next range request; this module does not queue overlapping requests. */
bool a7677s_http_is_busy(a7677s_t *dce);

/* One-time (not per-call) SSL context setup for A7677S_HTTP_SSL_CTX_INDEX -
 * must be called once, successfully, before the first
 * a7677s_http_get_range() call with an "https://" url. Not needed at all
 * for "http://" urls. Configures authmode=0 (no server cert verification -
 * fine for this project's use case, see A7677S_HTTP_SSL_CTX_INDEX's
 * doc-comment) and enableSNI=1 (needed for CDN-fronted HTTPS hosts like
 * GitHub raw content or ngrok - same doc-comment).
 *
 * Non-blocking, same "fire cb once" contract as a7677s_http_get_range().
 * Returns 0 if accepted, -1 if rejected immediately (modem command channel
 * busy - see a7677s_http_get_range()'s BUSY doc-comment, same reasoning
 * applies here). cb receives MODEM_OPS_OK/MODEM_OPS_ERROR (this call has no
 * HTTP-specific result to report, unlike get_range's richer result enum -
 * it is pure AT-layer configuration). */
typedef void (*a7677s_http_ssl_cfg_cb_t)(modem_ops_result_t result, void *ctx);
int a7677s_http_ssl_configure(a7677s_t *dce, a7677s_http_ssl_cfg_cb_t cb, void *ctx);

#ifdef __cplusplus
}
#endif
#endif // A7677S_HTTP_H