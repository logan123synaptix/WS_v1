#include "accel_app.h"
#include <math.h>
#include "logger.h"

static const char *TAG = "ACCEL_APP";

/* WS_v1's bno055_get_linear_accel() returns raw int16_t register counts
 * (see bno055.c's _read_vec3(), no internal scaling) — unlike WS_v0's
 * bno055_getVectorLinearAccel(), which divided by accelScale=100
 * internally before returning float m/s^2. bno055_init() sets
 * UNIT_SEL=0x00 (m/s^2, default), and per the datasheet's unit tables
 * (Documents/bno055.md: "1 m/s^2 = 100 LSB"), so this module must apply
 * that same /100.0f conversion itself to keep
 * ACCEL_APP_MOVEMENT_THRESHOLD's value (carried over unchanged from
 * WS_v0) meaningful in the same m/s^2 units it was originally tuned in. */
#define ACCEL_APP_LSB_PER_MS2   100.0f

void accel_app_init(accel_app_t *app, bno055_t *imu)
{
    app->imu               = imu;
    app->period_elapsed_ms = 0;
    app->filtered_mag      = 0.0f;
    app->has_reading       = false;
    app->movement_detected = false;
}

void accel_app_poll(accel_app_t *app, uint32_t delta_ms)
{
    app->period_elapsed_ms += delta_ms;
    if (app->period_elapsed_ms < ACCEL_APP_SAMPLE_PERIOD_MS) {
        return;
    }
    app->period_elapsed_ms = 0;

    bno055_vec3_t v;
    if (bno055_get_linear_accel(app->imu, &v) != BNO055_OK) {
        log_warn(TAG, "linear accel read failed");
        return;
    }

    float ax = (float)v.x / ACCEL_APP_LSB_PER_MS2;
    float ay = (float)v.y / ACCEL_APP_LSB_PER_MS2;
    float az = (float)v.z / ACCEL_APP_LSB_PER_MS2;
    float mag = sqrtf(ax * ax + ay * ay + az * az);

    if (!app->has_reading) {
        /* First sample: seed the filter directly rather than filtering
         * against the zero-initialized default, same as WS_v0's
         * static float prev_mag = 0 being overwritten on its first real
         * pass (that first pass compares against 0, which would falsely
         * report movement on startup — avoided here explicitly). */
        app->filtered_mag = mag;
        app->has_reading  = true;
        app->movement_detected = false;
        return;
    }

    float filtered = app->filtered_mag * (1.0f - ACCEL_APP_FILTER_ALPHA)
                     + mag * ACCEL_APP_FILTER_ALPHA;

    app->movement_detected =
        (fabsf(filtered - app->filtered_mag) > ACCEL_APP_MOVEMENT_THRESHOLD);

    app->filtered_mag = filtered;
}

bool accel_app_is_movement_detected(accel_app_t *app)
{
    return app->movement_detected;
}

/* ===== sleep/wake steps ===== */

void accel_app_sleep_step_start(void *ctx)
{
    accel_app_t *app = (accel_app_t *)ctx;
    /* bno055_set_pwr_mode() internally drops to CONFIGMODE before writing
     * PWR_MODE (see bno055.c) — real power-down per the datasheet's
     * Suspend Mode section, not just a UART/processing-load reduction. */
    if (bno055_set_pwr_mode(app->imu, BNO055_PWR_MODE_SUSPEND) != BNO055_OK) {
        log_warn(TAG, "suspend failed");
    }
}

uint8_t accel_app_sleep_step_is_done(void *ctx)
{
    (void)ctx;
    return 1; /* one-shot I2C write, no async wait needed */
}

void accel_app_wake_step_start(void *ctx)
{
    accel_app_t *app = (accel_app_t *)ctx;
    /* Per the datasheet, leaving suspend only requires rewriting
     * PWR_MODE — but the chip does not resume its previous fusion
     * operation mode on its own, so NDOF must be re-selected explicitly
     * (mirrors what bno055_init() did once at startup). */
    if (bno055_set_pwr_mode(app->imu, BNO055_PWR_MODE_NORMAL) != BNO055_OK) {
        log_warn(TAG, "resume (normal power) failed");
    }
    if (bno055_set_opr_mode(app->imu, BNO055_OPR_MODE_NDOF) != BNO055_OK) {
        log_warn(TAG, "resume (NDOF) failed");
    }
    /* Discard the stale pre-sleep filter state — first post-wake sample
     * should reseed rather than compare against a filtered_mag value
     * computed before the sensor was suspended. */
    app->has_reading       = false;
    app->period_elapsed_ms = 0;
}

uint8_t accel_app_wake_step_is_done(void *ctx)
{
    (void)ctx;
    return 1;
}