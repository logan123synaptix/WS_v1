#ifndef TEST_SLEEP_H
#define TEST_SLEEP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* Standalone bring-up test for the FULL sleep/wake cycle (tier 1/2/3,
 * see sx_sleep.h/sx_sleep_service.h/sx_sleep_manager.h), independent of
 * app.c -- per the user (2026-07-29): this test's whole point is to put
 * EVERY module to sleep (GPS, modem, SPS30, pump, ZE12A, BNO055) via the
 * real sx_sleep_manager_enter_sleep() so current draw can be measured on
 * the bench, not to test sensor readings themselves.
 *
 * Reuses the real driver/app-layer stack for every reading it publishes
 * (accel_app for IMU, sx_temp_humi for SHT3x, board.rtc for time), same
 * "go through the real layers" pattern as test_lte_mqtt.c/test_sht3x.c/
 * test_imu.c/test_rtc.c. Reuses test_lte_mqtt.c's exact MQTT bring-up
 * (same public test broker + client id + connect/publish pattern) since
 * that one is already confirmed working on hardware -- this file does
 * not reimplement MQTT connect logic, only the publish payload/cadence.
 *
 * NOT wired to GPS by requirement (per the user: no GPS hardware on this
 * bench, publish null lat/long is fine) -- this test's wake_steps still
 * include the GPS step (sx_sleep_manager.c always registers all 6 wake
 * steps; there is no way to opt one out without changing that module),
 * so a GPS fix is still attempted every wake, up to GPS_TIMEOUT_MS --
 * but the published payload only requires a non-zero fix to include
 * lat/long, emitting JSON null otherwise, same convention as app.c's
 * build_telemetry_payload(). Expect this test to run the full
 * GPS_TIMEOUT_MS (130s, app_config.h) wait on every wake before
 * proceeding if no GPS is attached -- this is a real cost of testing
 * without GPS connected, not a bug in this test file.
 *
 * Gas sensor (ZE12A) channels are always published as JSON null -- per
 * the user, no gas sensor hardware is attached yet on this bench setup.
 * The sleep_steps/wake_steps still drive ZE12A's QA/Active mode UART
 * commands via sx_sleep_manager.c regardless (fire-and-forget, harmless
 * with no sensor attached).
 *
 * Cycle: publish current reading -> sleep for SLEEP_TEST_TIME_MS (via
 * sx_sleep_manager_enter_sleep(), which runs ALL sleep_steps then parks
 * the MCU in STOP mode) -> wake (runs ALL wake_steps: GPS on/wait, modem
 * on/wait ready, ZE12A active mode, BNO055 resume) -> publish again ->
 * repeat. Matches this project's existing blocking-call pattern (see
 * app.c's APP_MODE_ENTER_SLEEP handling) -- sx_sleep_manager_enter_sleep()
 * does not return until the RTC wakeup timer fires.
 *
 * Call test_sleep_init() once after sx_board_init() (Core/Src/main.c),
 * then test_sleep_poll(delta_ms) every tick in the main while(1) loop --
 * same calling convention as the other test_*.c files. */

/* How long to stay in STOP mode per cycle. Adjust freely per test run --
 * short values (e.g. 10-30s) are convenient for watching the publish/
 * sleep/wake cycle repeat quickly on the bench; the real app.c currently
 * uses SLEEP_TIME_MS (app_config.h, 5 minutes) for the production cycle.
 * This is deliberately a SEPARATE, independent define so tuning this
 * test never risks accidentally changing the real app's sleep duration. */
#define SLEEP_TEST_TIME_MS   60000U

void test_sleep_init(void);
void test_sleep_poll(uint32_t delta_ms);

#ifdef __cplusplus
}
#endif

#endif