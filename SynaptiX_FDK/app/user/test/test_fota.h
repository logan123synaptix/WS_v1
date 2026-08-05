#ifndef TEST_FOTA_H
#define TEST_FOTA_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* End-to-end bring-up test for fota.c (SynaptiX_FDK/app/user/fota/) -
 * connects to the SAME public test broker/topic-prefix convention as
 * test_lte_mqtt.c (MQTT_HOST_TEST/MQTT_CLIENTID_TEST, app_config.h),
 * subscribes to the real FOTA_CHECK_TOPIC_PREFIX + device_id topic via
 * fota_init(), and calls fota_download() for real (full erase+write+
 * verify+swap+NVIC_SystemReset() on a CRC32 match - see fota.h/fota.c)
 * the moment fota_is_pending() goes true after a retained message is
 * published by hand (e.g. via MQTT Explorer) to that topic.
 *
 * Deliberately calls the REAL fota_download() (not a re-implemented
 * "log only" copy) - this is the actual code path fota.h/fota.c will run
 * once wired into app.c, so a pass here is a real confirmation of that
 * code, not just of a similar-looking test harness. Confirmed with the
 * user (2026-08-05): OK for this test to actually erase+write the
 * Secondary partition and, on a CRC32 match, actually swap+reset into
 * whatever image was downloaded - the test firmware used
 * (TrackingFirmWare.bin, a raw random-ish binary blob, NOT a real
 * bootable WS_v1 image) is expected to possibly leave the board in a
 * broken/unbootable state after a successful swap+reset; the user has
 * accepted this and will do a full chip erase + reflash if that happens.
 * This test's only purpose is confirming fota.c's download/verify/backup-
 * register logic runs correctly against a real retained MQTT message and
 * real HTTP range downloads, not confirming TrackingFirmWare.bin itself
 * is anything bootable.
 *
 * How to run:
 *   1. Wire test_fota_init() (once) and test_fota_poll(delta_ms) (every
 *      tick) into main.c, in place of (not alongside - see test_http.h's
 *      "do not run two tests that both call board.modem.ops->start()"
 *      warning, same reasoning applies here) test_http_init()/
 *      test_http_poll() or test_lte_mqtt_init()/test_lte_mqtt_poll() if
 *      either of those is currently wired in.
 *   2. Build, flash, open the log UART.
 *   3. Wait for "MQTT connected, subscribed to <topic> - waiting for a
 *      retained FOTA check message..." in the log - this line prints the
 *      EXACT topic string to publish to (depends on this device's
 *      device_id, which may not be the "001" default if it was ever
 *      changed via CLI/RPC - the test never assumes a fixed topic
 *      string, always reads it back from the real
 *      build_fota_check_topic()-equivalent it constructs itself the same
 *      way fota.c does).
 *   4. In MQTT Explorer (or any other MQTT client), connect to
 *      broker.hivemq.com:1883 (plain TCP, no auth - same broker/port
 *      test_lte_mqtt.c already uses), and PUBLISH (RETAINED - the retain
 *      flag/checkbox must be on, or fota_on_message() will still receive
 *      it live but the whole point of testing the "retained" delivery
 *      path is lost) to the topic printed in step 3, with a JSON body:
 *          {"url":"https://raw.githubusercontent.com/logan123synaptix/WS_v1/ft/fota_ws/TrackingFirmWare.bin","crc32":"0x<YOUR_COMPUTED_VALUE>"}
 *      (crc32 computed by the user separately - see fota.c's Part 1
 *      header comment for the exact algorithm: IEEE 802.3/zlib CRC32, so
 *      Python's zlib.crc32() on the same file matches exactly).
 *   5. Watch the log: fota_on_message() should log "FOTA update latched:
 *      crc32=... url=...", then this test calls fota_download() on its
 *      next tick, which logs its own detailed per-range progress
 *      (already implemented in fota.c's fota_download_attempt() - not
 *      duplicated by this test file). */

void test_fota_init(void);
void test_fota_poll(uint32_t delta_ms);

#ifdef __cplusplus
}
#endif

#endif