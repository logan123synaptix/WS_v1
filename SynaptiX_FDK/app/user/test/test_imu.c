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

    log_info(TAG, "Init done — sampling every %u ms, euler/movement logged every %u ms",
              (unsigned)ACCEL_APP_SAMPLE_PERIOD_MS, (unsigned)EULER_LOG_PERIOD_MS);
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
            log_info(TAG, "Euler heading=%d roll=%d pitch=%d  movement=%s",
                      euler.heading, euler.roll, euler.pitch,
                      accel_app_is_movement_detected(&s_accel) ? "YES" : "no");
        } else {
            log_error(TAG, "get_euler FAILED (ret=%d)", ret);
        }
    }
}