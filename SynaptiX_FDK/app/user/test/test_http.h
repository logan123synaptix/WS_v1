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
 * SELF-CONTAINED w.r.t. test_lte_mqtt: does NOT depend on
 * test_lte_mqtt.c/test_lte_mqtt_init()/test_lte_mqtt_poll() being called
 * anywhere. Modem power-on itself is already kicked off unconditionally by
 * sx_board_init() (main.c, runs before either test's init()) via
 * board.modem.ops->power_on_start() - this test only waits on
 * board.modem.ops->is_ready() and drives board.modem.ops->poll() every
 * tick via modem_handle_poll(), it does not call power_on_start() or
 * start() itself (a7677s.c's own cb_at_probe() kicks off the network
 * attach sequence automatically once power_state reaches READY - see that
 * function's comment for why calling start() from test code is unnecessary
 * and, if attempted too early, produces a harmless "power not ready yet"
 * warning that this test used to trigger before this comment was
 * updated).
 *
 * IMPORTANT if test_lte_mqtt is ALSO still wired into main.c: do not run
 * both at once. Both this file and test_lte_mqtt.c now independently call
 * board.modem.ops->start() and drive board.modem.ops->poll() every tick -
 * calling start() a second time while already starting/started is not
 * something this test or a7677s_http.c has been checked against, and both
 * would be driving the one shared a7677s_t/modem command channel
 * simultaneously (see a7677s_http.h's A7677S_HTTP_RANGE_BUSY doc-comment
 * for why that matters for a7677s_http_get_range() specifically). To run
 * this test alone, remove or comment out the test_lte_mqtt_init()/
 * test_lte_mqtt_poll() calls in main.c - this test needs only
 * test_http_init() once and test_http_poll(delta) every tick.
 *
 * BEFORE BUILDING: set TEST_HTTP_URL below (test_http.c) to a real,
 * reachable file a few hundred KB in size that supports HTTP Range
 * requests. Currently set to a real https:// URL (GitHub raw content, see
 * test_http.c) - a7677s_http_ssl_configure() is called automatically by
 * this test before the first range download when the URL is https, no
 * extra wiring needed. */

void test_http_init(void);
void test_http_poll(uint32_t delta_ms);

#ifdef __cplusplus
}
#endif

#endif