#include "test_imu.h"
#include "sx_board.h"
#include "accel_app.h"
#include "bno055.h"
#include "logger.h"

static const char *TAG = "TEST_IMU";

/* Owned here, not in Board_t — accel_app_t is just the periodic
 * sampling + movement-detection wrapper around board.imu (see
 * accel_app.h), same reasoning as test_sht3x.c's s_th for sx_temp_humi_t. */
static accel_app_t s_accel;

static uint32_t s_euler_log_accum_ms = 0;
#define EULER_LOG_PERIOD_MS  1000U

/* Watchdog for the "no reading at all" case — per the user (2026-07-29):
 * if no successful read completes within NO_READING_TIMEOUT_MS, re-run
 * bno055_init()'s reset-pulse + re-config sequence rather than silently
 * retrying forever. Tracks time since the LAST successful read (any
 * BNO055_OK from bno055_get_euler() below counts as "a reading"), not
 * time since boot — so a sensor that goes quiet again after recovering
 * gets the same 5s grace period, not just once at startup. */
#define NO_READING_TIMEOUT_MS   5000U
static uint32_t s_no_reading_accum_ms = 0;

static void reinit_imu(void)
{
    log_error(TAG, "No successful IMU reading in %u ms — resetting BNO055",
              (unsigned)NO_READING_TIMEOUT_MS);

    /* Re-runs the same reset-pulse (toggle low->high through
     * DELAY_RESET_MS) + full register re-config bno055_init() already
     * does at boot (see bno055.c) — this is not a lighter-weight
     * recovery path, it repeats the whole init sequence. Note this
     * shares s_i2c1_reset with board.rtc (RX8130CE) — see
     * sx_board_get_imu_reset_gpio()'s doc-comment in sx_board.h — so
     * this pulse will also reset the RTC's reset line. Acceptable for a
     * bring-up/recovery path (RX8130CE's own init already tolerates a
     * reset-line pulse at any time), but worth knowing if RTC behavior
     * looks odd right after this fires. */
    int ret = bno055_init(&board.imu, &board.i2c1, board.imu.dev_addr,
                           sx_board_get_imu_reset_gpio());
    if (ret != BNO055_OK) {
        log_error(TAG, "bno055_init retry FAILED (ret=%d) — check wiring/power", ret);
    } else {
        log_info(TAG, "bno055_init retry OK");
    }

    /* accel_app_t's own state (filtered_mag/has_reading/movement_detected)
     * must also be reset here — otherwise the first post-reset sample
     * would low-pass-filter against a filtered_mag value computed before
     * the sensor was reset (see accel_app_init()'s doc-comment: it
     * zeroes exactly this state). */
    accel_app_init(&s_accel, &board.imu);

    s_no_reading_accum_ms = 0;
}

void test_imu_init(void)
{
    log_info(TAG, "=== TEST BNO055 (I2C1, addr=0x%02X) ===",
              board.imu.dev_addr >> 1);

    /* board.imu was already bno055_init()'d inside sx_board_init()
     * (sx_board.c) — accel_app_init() only starts the periodic
     * sampling/filtering cadence on top of the already-initialized
     * sensor, it does not re-init I2C or the reset GPIO. */
    accel_app_init(&s_accel, &board.imu);

    /* Explicit presence/comms check independent of the periodic
     * cadence below, same reasoning as test_sht3x.c's status-register
     * read: if the sensor is not wired/powered correctly, this fails
     * immediately instead of silently retrying every
     * ACCEL_APP_SAMPLE_PERIOD_MS. */
    bno055_calib_stat_t calib = {0};
    int ret = bno055_get_calib_stat(&board.imu, &calib);
    if (ret == BNO055_OK) {
        log_info(TAG, "Comms OK — calib sys=%u gyro=%u accel=%u mag=%u",
                  calib.sys, calib.gyro, calib.accel, calib.mag);
    } else {
        log_error(TAG, "Calib-status read FAILED (ret=%d) — check I2C1 wiring/address/reset GPIO", ret);
    }

    s_no_reading_accum_ms = 0;

    log_info(TAG, "Init done — sampling every %u ms, euler/movement logged every %u ms, reset after %u ms with no reading",
              (unsigned)ACCEL_APP_SAMPLE_PERIOD_MS, (unsigned)EULER_LOG_PERIOD_MS,
              (unsigned)NO_READING_TIMEOUT_MS);
}

void test_imu_poll(uint32_t delta_ms)
{
    /* Drives the periodic linear-accel sample + low-pass filter +
     * movement-detection state, same call app.c's real
     * accel_app_poll() makes every tick. */
    accel_app_poll(&s_accel, delta_ms);

    /* Euler/quaternion are read directly here (not through accel_app,
     * which only tracks linear-accel magnitude for movement detection)
     * since a bring-up test benefits from seeing orientation output
     * directly, independent of the movement-detection logic under test. */
    s_euler_log_accum_ms += delta_ms;
    if (s_euler_log_accum_ms >= EULER_LOG_PERIOD_MS) {
        s_euler_log_accum_ms = 0;

        bno055_euler_t euler = {0};
        int ret = bno055_get_euler(&board.imu, &euler);
        if (ret == BNO055_OK) {
            s_no_reading_accum_ms = 0;
            log_info(TAG, "Euler heading=%d roll=%d pitch=%d  movement=%s",
                      euler.heading, euler.roll, euler.pitch,
                      accel_app_is_movement_detected(&s_accel) ? "YES" : "no");
        } else {
            log_error(TAG, "get_euler FAILED (ret=%d)", ret);
        }
    }

    /* Counted every tick (not just on the 1s euler-log cadence above) so
     * the 5s budget is measured in real elapsed time, not in "number of
     * failed euler-log attempts" — matters if EULER_LOG_PERIOD_MS is
     * ever changed. Reset to 0 above on every successful read. */
    s_no_reading_accum_ms += delta_ms;
    if (s_no_reading_accum_ms >= NO_READING_TIMEOUT_MS) {
        reinit_imu();
    }
}