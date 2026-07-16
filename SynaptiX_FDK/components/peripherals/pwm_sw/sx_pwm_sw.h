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
 * (via the caller's HAL_TIM_PeriodElapsedCallback() -> sx_timer_irq_handle()
 * -> this module's registered callback, same indirection sx_timer.h
 * documents). Each tick increments an internal tick counter modulo
 * period_ticks (period_ms / SX_PWM_SOFTWARE_TICK_MS); the GPIO is driven
 * HIGH for the first on_ticks of each period and LOW for the rest —
 * classic leading-edge PWM, just computed by ISR-tick counting instead of
 * a hardware compare register.
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

#ifndef SX_PWM_SOFTWARE_TICK_MS
#define SX_PWM_SOFTWARE_TICK_MS 1U /* underlying sx_timer_t tick period, ms */
#endif

typedef struct {
    sx_gpio_t  *gpio;        /* caller-owned, already sx_gpio_init()'d */
    sx_timer_t *timer;       /* caller-owned, already sx_timer_init()'d with
                               * this module's tick callback (see
                               * sx_pwm_software_init()'s doc-comment) */
    uint32_t    period_ticks; /* period_ms / SX_PWM_SOFTWARE_TICK_MS, >=1 */
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
 *   sx_gpio_init(&gpio, &sx_gpio_ops, &pump_pin);
 *   sx_timer_init(&timer, &sx_timer_ops, &htim3, SX_PWM_SOFTWARE_TICK_MS,
 *                 sx_pwm_software_tick_cb, &pwm);
 *   sx_pwm_software_init(&pwm, &gpio, &timer, period_ms, duty_percent);
 *   sx_pwm_software_start(&pwm);
 *
 * period_ms must be a multiple of SX_PWM_SOFTWARE_TICK_MS and >=
 * SX_PWM_SOFTWARE_TICK_MS (period_ticks must be >=1); duty_percent is
 * clamped to [0, 100]. */
void sx_pwm_software_init(sx_pwm_software_t *pwm, sx_gpio_t *gpio, sx_timer_t *timer,
                           uint32_t period_ms, uint8_t duty_percent);

/* Starts the underlying sx_timer_t (sx_timer_start()) and resets
 * tick_count to 0 so the new PWM cycle begins cleanly. */
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