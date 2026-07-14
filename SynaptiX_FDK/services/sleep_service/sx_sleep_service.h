#ifndef SX_SLEEP_SERVICE_H
#define SX_SLEEP_SERVICE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "sx_sleep.h"

/* Tier 2 of the 3-tier sleep architecture:
 *   Tier 1 (components/peripherals/sleep/sx_sleep.c/.h) — knows only HAL
 *     STOP mode + RTC wakeup timer. No peripheral knowledge at all.
 *   Tier 2 (this file) — generic step-sequencing ENGINE. Knows there is
 *     an ordered list of steps to run before sleep is allowed and another
 *     ordered list to run after waking, but has ZERO knowledge of what any
 *     step actually does (no GPS, no modem, no MQTT — none of that belongs
 *     here). This is what makes it reusable across projects.
 *   Tier 3 (project-specific, e.g. app/.../sx_sleep_manager.c/.h) — the
 *     only place that knows what a "GPS wake step" or "modem power-down
 *     step" concretely is; it builds sx_sleep_step_t arrays and hands them
 *     to this service.
 *
 * A step is just a non-blocking start() + a poll-style is_done(). The
 * service calls start() exactly once on entering a step, then polls
 * is_done() every tick until it returns true or the optional per-step
 * timeout elapses (whichever comes first) — an elapsed timeout is treated
 * the same as "done", to avoid ever hanging forever on a single step. */
typedef struct {
    void       (*start)  (void *ctx);   /* kick off the step; must not block */
    uint8_t    (*is_done)(void *ctx);   /* poll: has the step finished? */
    void        *ctx;                    /* opaque to this service */
    const char  *name;                   /* for logging only */
} sx_sleep_step_t;

typedef struct {
    sx_sleep_t      *sleep;              /* tier 1 handle */

    /* Wake-side step sequence (run after waking, before the app is allowed
     * to consider itself "awake"), caller-owned array. */
    sx_sleep_step_t  *wake_steps;
    uint8_t           wake_step_count;

    /* Sleep-side step sequence (run before actually entering STOP mode —
     * e.g. powering down peripherals), caller-owned array. May be NULL/0
     * if nothing needs to run before sleeping besides the RTC/STOP call
     * itself. */
    sx_sleep_step_t  *sleep_steps;
    uint8_t           sleep_step_count;

    /* Per-step timeout in ms, shared by both wake_steps and sleep_steps.
     * 0 = no timeout (step only ends when is_done() says so). This is
     * intentionally a single shared value for now — split into separate
     * wake/sleep timeouts later if a project ever needs that. */
    uint32_t          step_timeout_ms;

    /* Internal iteration state — do not touch directly. */
    uint8_t           current_step;
    uint8_t           step_started;
    uint32_t          step_elapsed_ms;
    uint8_t           running_sleep_steps; /* 0 = iterating wake_steps, 1 = sleep_steps */
} sx_sleep_service_t;

/* Registers the step arrays and tier-1 sleep handle. Does not run
 * anything yet — call sx_sleep_service_wake_process() or
 * sx_sleep_service_enter_sleep() to actually drive the steps. */
void sx_sleep_service_init(sx_sleep_service_t *svc,
                            sx_sleep_t         *sleep,
                            sx_sleep_step_t    *wake_steps,
                            uint8_t             wake_step_count,
                            sx_sleep_step_t    *sleep_steps,
                            uint8_t             sleep_step_count,
                            uint32_t            step_timeout_ms);

/* Runs sleep_steps to completion (blocking — each step's start()/is_done()
 * is polled in a tight loop here, same blocking style as the tier-3
 * predecessor this replaces), then sets the RTC wakeup timer and enters
 * STOP mode via tier 1. Returns only after waking back up. Caller is
 * responsible for calling sx_sleep_service_reset_wake() afterwards before
 * driving wake_process(), so the wake-side step iteration starts clean. */
void sx_sleep_service_enter_sleep(sx_sleep_service_t *svc, uint32_t sleep_sec);

/* Drives the wake_steps sequence — call once per app tick with the
 * elapsed time since the last call. Generic: starts the current step's
 * start() exactly once, polls is_done()/timeout every call, advances to
 * the next step when finished. No-op once all steps are done (check via
 * sx_sleep_service_is_wake_done()). */
void sx_sleep_service_wake_process(sx_sleep_service_t *svc, uint32_t delta_ms);

uint8_t sx_sleep_service_is_wake_done(sx_sleep_service_t *svc);

/* Resets wake-step iteration back to the first step (current_step = 0,
 * elapsed = 0). Call this before the next wake_process() cycle, i.e.
 * right after waking from enter_sleep(). */
void sx_sleep_service_reset_wake(sx_sleep_service_t *svc);

static inline wake_reason_t sx_sleep_service_get_wake_reason(sx_sleep_service_t *svc)
{
    return sx_sleep_get_wake_reason(svc->sleep);
}

#ifdef __cplusplus
}
#endif

#endif