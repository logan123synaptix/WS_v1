#ifndef SX_PWM_SOFTWARE_H
#define SX_PWM_SOFTWARE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "sx_gpio.h"
#include "sx_timer.h"

/* Software (bit-banged) PWM: toggles a plain sx_gpio_t on/off according to
 * a duty cycle, driven by an sx_timer_t tick — for boards/pins with no
 * hardware PWM channel wired to them (e.g. this project's pump driver,
 * EN_PW_PUMP, which is a plain GPIO with no TIMx output-compare channel
 * connected per the schematic).
 *
 * How it works: sx_timer_t is configured to tick every
 * SX_PWM_SOFTWARE_TICK_MS (see below) and calls sx_pwm_software_tick_cb()
 * from whatever ISR/ops-layer mechanism the currently-linked sx_timer_ops
 * driver uses to fire sx_timer_irq_handle() (see sx_timer.h — this module
 * only ever talks to sx_timer_t/sx_gpio_t's ops interfaces, never to any
 * platform HAL directly, so it builds unchanged against any sx_timer_ops/
 * sx_gpio_ops driver, STM32 or otherwise). Each tick increments an
 * internal tick counter modulo period_ticks (period_ms /
 * SX_PWM_SOFTWARE_TICK_MS); the GPIO is driven HIGH for the first
 * on_ticks of each period and LOW for the rest — classic leading-edge
 * PWM, just computed by ISR-tick counting instead of a hardware compare
 * register.
 *
 * Resolution/timing tradeoffs, spelled out since this is a real limitation
 * of doing PWM in software rather than hardware:
 *   - Duty cycle resolution is 1/period_ticks, not 1/65536 like a real
 *     TIM+ARR/CCR hardware PWM. E.g. a 100ms period at a 1ms tick gives
 *     only 100 discrete duty steps (1% resolution) — fine for something
 *     slow like a pump's average drive, unusable for anything needing fine
 *     analog-like control or audio-rate PWM.
 *   - Jitter: the GPIO edge timing depends on how promptly the underlying
 *     sx_timer_t's ISR actually fires and how long this callback takes to
 *     run relative to other ISRs on the system — expect edge jitter on the
 *     order of the tick period itself, unlike hardware PWM which is
 *     cycle-accurate regardless of CPU load. Not suitable for driving
 *     anything timing-sensitive (e.g. a servo, or switching-regulator
 *     control loop) — intended for slow, tolerant loads like a pump/heater/
 *     fan where an approximate average duty cycle is all that matters.
 *   - Every tick still runs even at 0% or 100% duty (the modulo/compare
 *     logic doesn't special-case those) — negligible CPU cost per tick,
 *     but worth knowing this isn't "stop ticking when idle". */

/* NOTE: no longer used to derive period_ticks (see sx_pwm_software_init()
 * below) — period_ticks is now computed from timer->tick_hz directly, so
 * it reflects whatever real tick rate the caller's sx_timer_t actually
 * runs at (e.g. 1MHz/1us-per-tick on this project's TIM1, not 1ms). Kept
 * only as the default assumed tick period for callers that still reason
 * in ms and haven't been updated to pass an sx_timer_t with a known
 * tick_hz; not read by this file's own logic anymore. */
#ifndef SX_PWM_SOFTWARE_TICK_MS
#define SX_PWM_SOFTWARE_TICK_MS 1U /* legacy/default assumed tick period, ms */
#endif

typedef struct {
    sx_gpio_t  *gpio;        /* caller-owned, already sx_gpio_init()'d */
    sx_timer_t *timer;       /* caller-owned, already sx_timer_init()'d with
                               * this module's tick callback (see
                               * sx_pwm_software_init()'s doc-comment) */
    uint32_t    period_ticks; /* round(timer->tick_hz * period_ms / 1000), >=1 —
                               * computed from the timer's REAL tick_hz, not
                               * SX_PWM_SOFTWARE_TICK_MS (see sx_pwm_software_init()) */
    uint32_t    on_ticks;     /* ticks per period the GPIO is driven HIGH */
    volatile uint32_t tick_count; /* current position within the period,
                                    * 0..period_ticks-1. volatile: written
                                    * from the timer ISR, read by
                                    * sx_pwm_software_get_duty_percent(). */
    uint8_t     running;
} sx_pwm_software_t;

/* Initializes the struct only — does not touch hardware. `gpio` and
 * `timer` must already be initialized by the caller (sx_gpio_init(),
 * sx_timer_init()) before this call; `timer`'s callback must be
 * sx_pwm_software_tick_cb with `arg` == this same sx_pwm_software_t*, e.g.:
 *
 *   sx_pwm_software_t pwm;
 *   sx_gpio_init(&gpio, &sx_gpio_ops, pump_pin_driver);
 *   sx_timer_init(&timer, &sx_timer_ops, timer_pdriver, tick_hz, 1,
 *                 sx_pwm_software_tick_cb, &pwm);
 *   sx_pwm_software_init(&pwm, &gpio, &timer, period_ms, duty_percent);
 *   sx_pwm_software_start(&pwm);
 *
 * (`tick_hz` above is the timer's real counter frequency, e.g. 1000000 for
 * a 1us tick — see sx_timer.h's doc-comment on sx_timer_ops_t for how to
 * derive it from an existing CubeMX init; the timeout_ms argument (1
 * above) is only a placeholder here since sx_pwm_software_start() applies
 * its own period via sx_timer_start_ticks(), not sx_timer_start().)
 *
 * `pump_pin_driver`/`timer_pdriver` are whatever pDriver type the
 * currently-linked sx_gpio_ops/sx_timer_ops driver expects (e.g. a
 * sx_gpio_pin_t* and a TIM_HandleTypeDef* on the STM32 HAL driver this
 * project currently builds against) — this module never inspects or
 * casts pDriver itself, it only ever calls through sx_gpio_t/sx_timer_t's
 * own ops functions.
 *
 * period_ms is converted to period_ticks via timer->tick_hz (rounded to
 * the nearest tick, floored to 1 if the result would be 0 — e.g. a
 * period_ms too short to represent even one tick at this tick_hz);
 * duty_percent is clamped to [0, 100]. */
void sx_pwm_software_init(sx_pwm_software_t *pwm, sx_gpio_t *gpio, sx_timer_t *timer,
                           uint32_t period_ms, uint8_t duty_percent);

/* Starts the underlying sx_timer_t via sx_timer_start_ticks(pwm->timer,
 * pwm->period_ticks) — applies the exact period_ticks computed in
 * sx_pwm_software_init()/_set_duty() directly (bypassing sx_timer_t's
 * timeout_ms/ms-rounding path entirely, see sx_timer.h's doc-comment on
 * set_period_ticks), and resets tick_count to 0 so the new PWM cycle
 * begins cleanly. */
void sx_pwm_software_start(sx_pwm_software_t *pwm);

/* Stops the underlying sx_timer_t and forces the GPIO LOW (not left in
 * whatever mid-cycle state it happened to be in when stopped) — same
 * "known safe state on stop" convention as sx_pump.c's pump_off(). */
void sx_pwm_software_stop(sx_pwm_software_t *pwm);

/* Changes duty cycle for the CURRENT period_ms (period_ticks unchanged).
 * Takes effect on the next tick, not synchronized to period boundaries —
 * for a bumpless/glitch-free change, stop+set+start instead if that
 * matters for a given load. duty_percent is clamped to [0, 100]. */
void sx_pwm_software_set_duty(sx_pwm_software_t *pwm, uint8_t duty_percent);

/* Returns the currently configured duty cycle, 0-100. */
uint8_t sx_pwm_software_get_duty_percent(const sx_pwm_software_t *pwm);

/* The sx_timer_t callback (sx_timer_callback_t signature) — pass this as
 * sx_timer_init()'s callback argument, with `arg` set to the
 * sx_pwm_software_t* it should drive (see sx_pwm_software_init()'s
 * doc-comment above for the exact wiring). Drives the GPIO HIGH/LOW based
 * on tick_count vs on_ticks, then advances tick_count modulo
 * period_ticks. Runs in ISR context (via sx_timer_irq_handle()) — keep
 * this fast, which it is: a compare, a GPIO write, and an increment. */
void sx_pwm_software_tick_cb(void *arg);

#ifdef __cplusplus
}
#endif

#endif // SX_PWM_SOFTWARE_H