#ifndef TEST_HTTP_H
#define TEST_HTTP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* Standalone bring-up test for a7677s_http.c (SynaptiX_FDK/components/
 * modules/a76xx/a7677s_http.h) - deliberately isolated from fota.c so the
 * HTTP range-download primitive can be confirmed against real hardware
 * BEFORE any FOTA integration work builds on top of it. Per this project's
 * rule that real hardware log always wins over datasheet assumptions -
 * a7677s_http.c currently has several points marked "NOT verified against
 * real hardware yet" (see that file's comments), most importantly:
 *   1) the EXACT text of "+HTTPACTION:0,<status>,<len>" (a space after the
 *      colon, like the "+CMQTTSTART: 0" vs datasheet's "+CMQTTSTART:0"
 *      mismatch already found and fixed in a7677s.c's MQTT code, is a real
 *      possibility here too and would silently make cb_http_action() time
 *      out forever if uncorrected)
 *   2) whether AT+HTTPREAD can really be called more than once against the
 *      same already-downloaded range at different offsets (READMODE=1)
 * This test exists to answer both from a real UART log, not to exercise
 * the full FOTA flow (no flash writing, no CRC32, no bootloader partition
 * flags are touched by this test at all).
 *
 * Follows the same init()-once / poll(delta_ms)-every-tick shape as
 * test_lte_mqtt.h, reusing the board's real modem instance (board.a7677s,
 * board.modem, sx_board.h) rather than a second/fake instance -
 * a7677s_http.c's private state (see that file's s_http) is single-instance
 * only, matching the "only one real a7677s_t on this board" assumption
 * already made throughout a7677s.c.
 *
 * SELF-CONTAINED: drives modem power-on and network registration itself,
 * via board.modem.ops->start()/is_ready()/poll() directly (modem_ops.h) -
 * the same calls sx_user_mqtt.c makes internally, but without going through
 * sx_user_mqtt.c/sx_mqtt.c or any MQTT config at all. This test has NO
 * dependency on test_lte_mqtt.c/test_lte_mqtt_init()/test_lte_mqtt_poll()
 * being called anywhere - test_http_init() + test_http_poll() alone are
 * sufficient to bring the modem up from cold and run the HTTP range
 * download. Confirmed by reading sx_user_mqtt.c's _common_init() (the
 * board.modem.ops->start() call) and sx_mqtt_poll()'s modem_handle_poll()
 * call - both reused here directly.
 *
 * BEFORE BUILDING: set TEST_HTTP_URL below (test_http.c) to a real,
 * reachable, plain-HTTP (not HTTPS - AT+HTTPSSL is not exercised by this
 * test) file a few hundred KB in size. A small JPEG or firmware .bin
 * hosted anywhere reachable from the SIM's APN works; must support HTTP
 * Range requests (most static file hosts do) for TEST_HTTP_RANGE_LEN's
 * chunking to be meaningfully tested rather than accidentally reading the
 * whole file in one range. */

void test_http_init(void);
void test_http_poll(uint32_t delta_ms);

#ifdef __cplusplus
}
#endif

#endif