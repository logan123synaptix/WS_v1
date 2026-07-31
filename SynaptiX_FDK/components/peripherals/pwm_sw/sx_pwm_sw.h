#ifndef SX_PWM_SOFTWARE_H
#define SX_PWM_SOFTWARE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "sx_gpio.h"
#include "sx_timer.h"

/* Software (bit-banged) PWM: toggles a plain sx_gpio_t on/off according to
 * a duty cycle, driven by an sx_timer_t ISR — for boards/pins with no
 * hardware PWM channel wired to them (e.g. this project's pump driver,
 * EN_PW_PUMP/PA8, which the schematic confirms has no TIM1 output-compare
 * channel connected to it — bit-banging is the only option here, not a
 * choice made for convenience).
 *
 * FIXED 2026-07-31 — the previous version of this file had a real design
 * bug (found via bench test: multimeter showed 3.3V/0V for every duty
 * value except exactly 0%/100%, never an intermediate average). Root
 * cause: sx_pwm_software_start()/_set_duty() were deriving period_ticks
 * directly from timer->tick_hz (the timer's raw COUNTER frequency, e.g.
 * 1MHz) and then applying that same value as the timer's ARR via
 * sx_timer_start_ticks(). That conflates two different things:
 *   - the timer's COUNTER frequency (how fast CNT increments — fast,
 *     e.g. 1MHz on this project's TIM1)
 *   - the timer's ISR/OVERFLOW frequency (how often
 *     HAL_TIM_PeriodElapsedCallback() actually FIRES — once per ARR
 *     overflow, i.e. once per COUNTER-frequency/ARR seconds)
 * sx_pwm_software_tick_cb() (below) only runs once per ISR fire, NOT once
 * per counter tick. Setting ARR = period_ticks (e.g. 10000, for a 10ms
 * period at 1MHz counter) made the ISR fire only once per 10ms — i.e.
 * once per PWM period, not period_ticks times per period as the
 * tick_count/on_ticks compare logic assumed. tick_count could then only
 * ever be 0 or 1 when tick_cb ran, so any on_ticks > 1 (any duty above
 * ~0.01%) made "tick_count < on_ticks" true on effectively every call —
 * indistinguishable from 100% duty. duty=0% degenerately worked because
 * on_ticks=0 makes the compare false unconditionally, and on/off (start/
 * stop) worked because those bypass the compare entirely (stop forces
 * the pin LOW directly) — this is exactly the on/off-works-but-nothing-
 * between-does-anything symptom observed on the bench.
 *
 * Fix: decouple the ISR rate from the counter rate. The timer's ARR is
 * now fixed at a caller-supplied `isr_period_ticks` (in COUNTER ticks,
 * e.g. 100 ticks at 1MHz counter = 100us per ISR fire = 10kHz ISR rate)
 * and this module never touches ARR again after init — it only starts/
 * stops the SAME fixed-period timer. `period_ms`/duty are now expressed
 * in units of ISR FIRES (period_ticks below = how many times tick_cb
 * must run to complete one PWM period), not raw counter ticks, so
 * tick_count actually walks through the intended number of steps within
 * a period instead of being stuck at 0-1.
 *
 * Duty resolution is now 1/period_ticks where period_ticks = period_ms *
 * isr_rate_hz / 1000 — e.g. 10ms PWM period at a 10kHz ISR rate (100us
 * ARR at 1MHz counter) gives period_ticks=100, i.e. 1% resolution, with
 * the ISR itself running comfortably below anything that would starve
 * other interrupts on this board (10kHz is a light ISR load for a single
 * compare + GPIO write + increment).
 *
 * Same real-world tradeoffs as before otherwise (see the resolution/
 * jitter notes that used to live here) — still not cycle-accurate like
 * true hardware PWM, still fine for a slow load like a pump. */

typedef struct {
    sx_gpio_t  *gpio;        /* caller-owned, already sx_gpio_init()'d */
    sx_timer_t *timer;       /* caller-owned, already sx_timer_init()'d AND
                               * already carrying a FIXED isr_period_ticks
                               * ARR (see sx_pwm_software_init()'s
                               * doc-comment) — this module starts/stops
                               * it but never changes its ARR again. */
    uint32_t    period_ticks; /* PWM period, in units of ISR fires (NOT raw
                               * counter ticks — see the file doc-comment
                               * above for why that distinction matters).
                               * >=1. */
    uint32_t    on_ticks;     /* ISR fires per period the GPIO is driven
                               * HIGH. */
    volatile uint32_t tick_count; /* current position within the period,
                                    * 0..period_ticks-1, advanced ONE PER
                                    * ISR FIRE (not per counter tick).
                                    * volatile: written from the timer
                                    * ISR, read by
                                    * sx_pwm_software_get_duty_percent(). */
    uint8_t     running;
} sx_pwm_software_t;

/* Initializes the struct only — does not touch hardware, does not start
 * the timer. `gpio` and `timer` must already be initialized by the
 * caller; `timer` must already have its ARR fixed via
 * ops->set_period_ticks(timer, isr_period_ticks) (or ops->set_period())
 * at whatever fast, fixed ISR rate the caller has chosen — this module
 * calls sx_timer_start_hw()/sx_timer_stop() only, it never reprograms
 * ARR itself any more. `timer`'s callback must be
 * sx_pwm_software_tick_cb with `arg` == this same sx_pwm_software_t*.
 *
 * Example (matches this project's TIM1/pump wiring, see sx_board.c):
 *
 *   // Fix the ISR rate once, e.g. 100 counter-ticks per fire at a 1MHz
 *   // counter = 10kHz ISR rate = 100us per fire:
 *   sx_timer_init_regs(&timer, &sx_timer_ops, htim1, 32000000U, 31U, 99U,
 *                       sx_pwm_software_tick_cb, &pwm);
 *   sx_gpio_init(&gpio, &sx_gpio_ops, pump_pin_driver);
 *   // isr_rate_hz below MUST match what the ARR above actually produces
 *   // (timer->tick_hz / (ARR+1) = 1000000/100 = 10000 in this example):
 *   sx_pwm_software_init(&pwm, &gpio, &timer, 10000U, 10U, 0U);
 *   sx_pwm_software_start(&pwm);
 *
 * `isr_rate_hz` is the real ISR fire frequency the caller's timer ARR
 * above actually produces (timer->tick_hz / (ARR+1)) — this module has
 * no way to derive it from `timer` alone (tick_hz alone doesn't tell it
 * what ARR the caller chose), so the caller must pass it explicitly.
 * period_ms is converted to period_ticks via isr_rate_hz (rounded to the
 * nearest ISR fire, floored to 1); duty_percent is clamped to [0, 100]. */
void sx_pwm_software_init(sx_pwm_software_t *pwm, sx_gpio_t *gpio, sx_timer_t *timer,
                           uint32_t isr_rate_hz, uint32_t period_ms,
                           uint8_t duty_percent);

/* Starts the underlying sx_timer_t via ops->start_hw() — the timer's ARR
 * (the fixed ISR rate) is assumed already programmed by the caller before
 * sx_pwm_software_init() (see its doc-comment); this function does NOT
 * touch ARR/period, only enables counting + IT. Resets tick_count to 0 so
 * the new PWM cycle begins cleanly. */
void sx_pwm_software_start(sx_pwm_software_t *pwm);

/* Stops the underlying sx_timer_t and forces the GPIO LOW (not left in
 * whatever mid-cycle state it happened to be in when stopped) — same
 * "known safe state on stop" convention as sx_pump.c's pump_off(). */
void sx_pwm_software_stop(sx_pwm_software_t *pwm);

/* Changes duty cycle for the CURRENT period_ms (period_ticks unchanged).
 * Takes effect on the next ISR fire, not synchronized to period
 * boundaries — for a bumpless/glitch-free change, stop+set+start instead
 * if that matters for a given load. duty_percent is clamped to [0, 100]. */
void sx_pwm_software_set_duty(sx_pwm_software_t *pwm, uint8_t duty_percent);

/* Returns the currently configured duty cycle, 0-100. */
uint8_t sx_pwm_software_get_duty_percent(const sx_pwm_software_t *pwm);

/* The sx_timer_t callback (sx_timer_callback_t signature) — pass this as
 * sx_timer_init()'s callback argument, with `arg` set to the
 * sx_pwm_software_t* it should drive (see sx_pwm_software_init()'s
 * doc-comment above for the exact wiring). Drives the GPIO HIGH/LOW based
 * on tick_count vs on_ticks, then advances tick_count modulo
 * period_ticks — ONE step per ISR fire, i.e. once per ARR overflow, NOT
 * once per raw counter tick (see the file doc-comment's root-cause
 * explanation for why that distinction is the whole fix here). Runs in
 * ISR context (via sx_timer_irq_handle()) — keep this fast, which it is:
 * a compare, a GPIO write, and an increment. */
void sx_pwm_software_tick_cb(void *arg);

#ifdef __cplusplus
}
#endif

#endif // SX_PWM_SOFTWARE_H