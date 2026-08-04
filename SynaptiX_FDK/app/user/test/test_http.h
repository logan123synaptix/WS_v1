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
 * sx_board.h) rather than a second/fake instance - a7677s_http.c's private
 * state (see that file's s_http) is single-instance only, matching the
 * "only one real a7677s_t on this board" assumption already made
 * throughout a7677s.c.
 *
 * Depends on the modem already being powered on, network-attached, and
 * a7677s_ops.is_ready() true before starting the HTTP call - this test
 * does NOT drive the modem power-on/network-attach sequence itself (unlike
 * test_lte_mqtt.c, which does via sx_user_mqtt_poll()). Call
 * test_http_init() only after confirming the modem is ready some other way
 * (e.g. run test_lte_mqtt first and confirm "-> Now CONNECTED" in the log,
 * THEN swap main.c's test call over to this one - the two tests are not
 * meant to run at the same time, since a7677s_http_get_range() and MQTT
 * both need the single shared modem command channel free, see
 * a7677s_http.h's A7677S_HTTP_RANGE_BUSY doc-comment).
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