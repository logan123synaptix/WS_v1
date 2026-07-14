#include "sx_sleep_service.h"
#include "sx_delay.h"
#include "logger.h"

static const char *TAG = "SX_SLEEP_SVC";

void sx_sleep_service_init(sx_sleep_service_t *svc,
                            sx_sleep_t         *sleep,
                            sx_sleep_step_t    *wake_steps,
                            uint8_t             wake_step_count,
                            sx_sleep_step_t    *sleep_steps,
                            uint8_t             sleep_step_count,
                            uint32_t            step_timeout_ms)
{
    svc->sleep             = sleep;
    svc->wake_steps         = wake_steps;
    svc->wake_step_count    = wake_step_count;
    svc->sleep_steps        = sleep_steps;
    svc->sleep_step_count   = sleep_step_count;
    svc->step_timeout_ms    = step_timeout_ms;

    svc->current_step        = 0;
    svc->step_started         = 0;
    svc->step_elapsed_ms      = 0;
    svc->running_sleep_steps  = 0;

    log_info(TAG, "init OK (%u wake step(s), %u sleep step(s))",
             wake_step_count, sleep_step_count);
}

/* Runs a single caller-supplied step array to completion, blocking
 * (tight-poll) — used for sleep_steps in enter_sleep(), which was always
 * a blocking sequence in the tier-3 predecessor this replaces (see old
 * sx_sleep_manager_enter()'s gps_power_off()/power_off_start() calls
 * followed by sx_delay_ms()). Generic here: this function has no idea
 * what any given step actually does. */
static void _run_steps_blocking(sx_sleep_step_t *steps, uint8_t count,
                                 uint32_t step_timeout_ms)
{
    for (uint8_t i = 0; i < count; i++) {
        sx_sleep_step_t *step = &steps[i];
        if (!step->start || !step->is_done) {
            log_warn(TAG, "step '%s' missing start/is_done, skipping",
                     step->name ? step->name : "?");
            continue;
        }

        log_info(TAG, "step '%s' starting", step->name ? step->name : "?");
        step->start(step->ctx);

        uint32_t elapsed_ms = 0;
        while (!step->is_done(step->ctx)) {
            if (step_timeout_ms > 0 && elapsed_ms >= step_timeout_ms) {
                log_warn(TAG, "step '%s' timed out after %lu ms",
                         step->name ? step->name : "?", elapsed_ms);
                break;
            }
            sx_delay_ms(10);
            elapsed_ms += 10;
        }
        log_info(TAG, "step '%s' done", step->name ? step->name : "?");
    }
}

void sx_sleep_service_enter_sleep(sx_sleep_service_t *svc, uint32_t sleep_sec)
{
    if (sleep_sec == 0) sleep_sec = 1;

    log_info(TAG, "Running %u sleep step(s) before STOP", svc->sleep_step_count);
    _run_steps_blocking(svc->sleep_steps, svc->sleep_step_count,
                        svc->step_timeout_ms);

    log_info(TAG, "Setting RTC wakeup = %lu sec", sleep_sec);
    sx_sleep_set_rtc_wake(svc->sleep, sleep_sec);

    log_info(TAG, ">>> Entering STOP mode NOW");
    sx_delay_ms(10);

    sx_sleep_enter_stop(svc->sleep);

    log_info(TAG, "<<< Woke from STOP mode");
    sx_sleep_cancel_rtc(svc->sleep);
}

void sx_sleep_service_wake_process(sx_sleep_service_t *svc, uint32_t delta_ms)
{
    if (svc->current_step >= svc->wake_step_count) {
        return; /* all wake steps already done */
    }

    sx_sleep_step_t *step = &svc->wake_steps[svc->current_step];

    if (!step->start || !step->is_done) {
        log_warn(TAG, "wake step '%s' missing start/is_done, skipping",
                 step->name ? step->name : "?");
        svc->current_step++;
        svc->step_started    = 0;
        svc->step_elapsed_ms = 0;
        return;
    }

    if (!svc->step_started) {
        log_info(TAG, "wake step '%s' starting", step->name ? step->name : "?");
        step->start(step->ctx);
        svc->step_started    = 1;
        svc->step_elapsed_ms = 0;
        return; /* give start() one full tick before polling is_done() */
    }

    svc->step_elapsed_ms += delta_ms;

    uint8_t done = step->is_done(step->ctx);
    uint8_t timed_out = (svc->step_timeout_ms > 0 &&
                         svc->step_elapsed_ms >= svc->step_timeout_ms);

    if (done || timed_out) {
        if (timed_out && !done) {
            log_warn(TAG, "wake step '%s' timed out after %lu ms",
                     step->name ? step->name : "?", svc->step_elapsed_ms);
        } else {
            log_info(TAG, "wake step '%s' done", step->name ? step->name : "?");
        }
        svc->current_step++;
        svc->step_started    = 0;
        svc->step_elapsed_ms = 0;

        if (svc->current_step >= svc->wake_step_count) {
            log_info(TAG, "all wake steps complete");
        }
    }
}

uint8_t sx_sleep_service_is_wake_done(sx_sleep_service_t *svc)
{
    return (svc->current_step >= svc->wake_step_count);
}

void sx_sleep_service_reset_wake(sx_sleep_service_t *svc)
{
    svc->current_step    = 0;
    svc->step_started     = 0;
    svc->step_elapsed_ms  = 0;
}