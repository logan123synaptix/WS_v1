#include "sps30_app.h"
#include "sps30_uart.h"
#include "sensirion_common.h"
#include "logger.h"

static const char *TAG = "SPS30_APP";

/* Time to let the SPS30 module's own power supply and UART interface
 * settle after driving EN_PW_DUST high, before attempting any SHDLC
 * communication. Not a datasheet-specified value (the datasheet only
 * documents the internal Wake-up command's 100ms interface-activation
 * window, not physical power-on settling time for a board like this
 * one, where EN_PW_DUST/the opto+MOSTFET chain is this project's own
 * addition, absent from Sensirion's reference hardware) — chosen to
 * match WS_v0's sps30_app_init() power-on delay (bsp_delay(1000)) as a
 * conservative, previously-working value. Revisit if real hardware
 * testing shows this can be shorter. */
#define SPS30_APP_POWER_ON_SETTLE_MS    1000U

/* Datasheet: fan start-up is ~3s before speed is checked, typical
 * start-up time for stable number-concentration readings is ~8s.
 * WS_v0 used a flat 5000ms wait here and worked in practice; kept as
 * the same conservative middle-ground rather than pushing to the full
 * 8s datasheet figure, since this is not a hard correctness
 * requirement (SPEED/LASER/FAN status-register bits, not implemented
 * here yet, are the actual documented way to detect a bad reading). */
#define SPS30_APP_MEASUREMENT_STABILIZE_MS   5000U

static void sps30_app_enter(sps30_app_t *app, sps30_app_state_t next)
{
    app->state = next;
    app->state_elapsed_ms = 0;
}

void sps30_app_init(sps30_app_t *app, sx_gpio_t *power_gpio)
{
    app->power_gpio      = power_gpio;
    app->state            = SPS30_APP_STATE_IDLE;
    app->state_elapsed_ms = 0;
    app->has_measurement  = false;
}

void sps30_app_start_cycle(sps30_app_t *app)
{
    if (app->state != SPS30_APP_STATE_IDLE && app->state != SPS30_APP_STATE_DONE) {
        log_warn(TAG, "start_cycle() called while a cycle is already running (state=%d), ignored",
                 app->state);
        return;
    }

    sx_gpio_write(app->power_gpio, SX_GPIO_HIGH);
    log_info(TAG, "EN_PW_DUST high, waiting %lu ms to settle",
             (unsigned long)SPS30_APP_POWER_ON_SETTLE_MS);

    sps30_app_enter(app, SPS30_APP_STATE_POWER_ON);
}

void sps30_app_poll(sps30_app_t *app, uint32_t delta_ms)
{
    int16_t ret;

    app->state_elapsed_ms += delta_ms;

    switch (app->state) {
    case SPS30_APP_STATE_IDLE:
    case SPS30_APP_STATE_DONE:
        /* Nothing to drive — caller starts the next cycle explicitly
         * via sps30_app_start_cycle() / clears DONE via
         * sps30_app_reset(). */
        break;

    case SPS30_APP_STATE_POWER_ON:
        if (app->state_elapsed_ms >= SPS30_APP_POWER_ON_SETTLE_MS) {
            sps30_app_enter(app, SPS30_APP_STATE_WAKE_UP);
        }
        break;

    case SPS30_APP_STATE_WAKE_UP:
        ret = sps30_wake_up_sequence();
        if (ret == NO_ERROR) {
            log_info(TAG, "wake_up_sequence OK");
            sps30_app_enter(app, SPS30_APP_STATE_START_MEASUREMENT);
        } else {
            log_error(TAG, "wake_up_sequence failed, err=%d", ret);
            /* Give up this cycle rather than retrying forever — next
             * sps30_app_start_cycle() will try again from POWER_ON. */
            sps30_app_enter(app, SPS30_APP_STATE_DONE);
        }
        break;

    case SPS30_APP_STATE_START_MEASUREMENT:
        ret = sps30_start_measurement(SPS30_OUTPUT_FORMAT_OUTPUT_FORMAT_FLOAT);
        if (ret == NO_ERROR) {
            log_info(TAG, "measurement started");
            sps30_app_enter(app, SPS30_APP_STATE_WAIT_MEASUREMENT);
        } else {
            log_error(TAG, "start_measurement failed, err=%d", ret);
            sps30_app_enter(app, SPS30_APP_STATE_DONE);
        }
        break;

    case SPS30_APP_STATE_WAIT_MEASUREMENT:
        if (app->state_elapsed_ms >= SPS30_APP_MEASUREMENT_STABILIZE_MS) {
            sps30_app_enter(app, SPS30_APP_STATE_READ_MEASUREMENT);
        }
        break;

    case SPS30_APP_STATE_READ_MEASUREMENT:
        ret = sps30_read_measurement_values_float(
            &app->last_measurement.mc_1p0, &app->last_measurement.mc_2p5,
            &app->last_measurement.mc_4p0, &app->last_measurement.mc_10p0,
            &app->last_measurement.nc_0p5, &app->last_measurement.nc_1p0,
            &app->last_measurement.nc_2p5, &app->last_measurement.nc_4p0,
            &app->last_measurement.nc_10p0, &app->last_measurement.typical_particle_size);

        if (ret == NO_ERROR) {
            app->has_measurement = true;
            log_info(TAG, "PM1.0=%.2f PM2.5=%.2f PM4.0=%.2f PM10.0=%.2f",
                     app->last_measurement.mc_1p0, app->last_measurement.mc_2p5,
                     app->last_measurement.mc_4p0, app->last_measurement.mc_10p0);
        } else {
            log_error(TAG, "read_measurement failed, err=%d", ret);
            /* Deliberately NOT retrying/repeating the cycle here (unlike
             * WS_v0's mc_pm10p0==mc_pm2p5 heuristic, which re-looped
             * back to IDLE on what it treated as "invalid data" — see
             * this file's header comment for why that heuristic was
             * dropped). A real read failure (non-NO_ERROR) still just
             * proceeds to STOP_MEASUREMENT below with has_measurement
             * left false / stale, same as WS_v0 did in its own error
             * branch. */
        }
        sps30_app_enter(app, SPS30_APP_STATE_STOP_MEASUREMENT);
        break;

    case SPS30_APP_STATE_STOP_MEASUREMENT:
        ret = sps30_stop_measurement();
        if (ret == NO_ERROR) {
            log_info(TAG, "measurement stopped");
        } else {
            log_error(TAG, "stop_measurement failed, err=%d", ret);
        }
        /* Proceed to DONE regardless — caller decides what to do with
         * has_measurement (stale data if a read/stop above failed). */
        sps30_app_enter(app, SPS30_APP_STATE_DONE);
        break;

    default:
        sps30_app_enter(app, SPS30_APP_STATE_IDLE);
        break;
    }
}

bool sps30_app_is_cycle_done(sps30_app_t *app)
{
    return app->state == SPS30_APP_STATE_DONE;
}

bool sps30_app_has_measurement(sps30_app_t *app)
{
    return app->has_measurement;
}

const sps30_app_measurement_t *sps30_app_get_measurement(sps30_app_t *app)
{
    return &app->last_measurement;
}

void sps30_app_reset(sps30_app_t *app)
{
    sps30_app_enter(app, SPS30_APP_STATE_IDLE);
}

/* ===== sleep_step-compatible power-down ===== */

void sps30_app_sleep_step_start(void *ctx)
{
    sps30_app_t *app = (sps30_app_t *)ctx;

    if (app->state != SPS30_APP_STATE_IDLE && app->state != SPS30_APP_STATE_DONE) {
        log_warn(TAG, "sleep requested mid-cycle (state=%d) — powering down anyway; "
                       "fan/laser will stop abruptly rather than via the normal "
                       "stop_measurement()+sleep() sequence", app->state);
    }

    /* Per datasheet-recommended shutdown: stop any measurement (safe to
     * call even if already stopped/idle — worst case returns a benign
     * error, which we don't treat as fatal here), then SHDLC sleep()
     * before cutting physical power. */
    sps30_stop_measurement();
    sps30_sleep();

    sx_gpio_write(app->power_gpio, SX_GPIO_LOW);
    log_info(TAG, "EN_PW_DUST low, SPS30 powered down");

    sps30_app_enter(app, SPS30_APP_STATE_IDLE);
}

uint8_t sps30_app_sleep_step_is_done(void *ctx)
{
    (void)ctx;
    return 1; /* power-down above is synchronous/fire-and-forget */
}