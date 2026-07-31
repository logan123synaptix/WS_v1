#ifndef PUMP_H
#define PUMP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "sx_gpio.h"
#include "sx_timer.h"
#include "sx_pwm_sw.h"

#define PUMP_FULL_DRIVE_PERCENT 100U

/* Initializes the pump's GPIO (via sx_gpio_init()) and wires it to the
 * caller-owned sx_pwm_software_t/sx_timer_t so the pump is driven through
 * software PWM instead of a plain on/off GPIO write. `timer` must already
 * be initialized AND already have its ARR fixed at a fast, constant ISR
 * rate (e.g. via sx_timer_init_regs() in sx_board.c) before this call —
 * this function only owns the GPIO + PWM struct, not the timer hardware
 * itself, and never reprograms the timer's ARR (see sx_pwm_sw.h's file
 * doc-comment for why that distinction matters). `isr_rate_hz` must match
 * whatever ISR fire rate that ARR actually produces (timer->tick_hz /
 * (ARR+1)) — sx_board.c documents the exact value it uses. Starts at
 * duty=0%, not running (see pump_on()/pump_set_power() to actually start
 * it). */
void pump_init(sx_pwm_software_t *pwm, sx_gpio_t *gpio, sx_gpio_ops_t *ops,
                void *gpio_pDriver, sx_timer_t *timer, uint32_t isr_rate_hz);

/* Full drive: duty=100%. If the PWM is already running (e.g. via
 * pump_set_power()), just updates duty in place without restarting —
 * otherwise sets duty then starts. */
void pump_on(sx_pwm_software_t *pwm);

/* Stops the pump: stops the underlying timer and forces the GPIO LOW (see
 * sx_pwm_software_stop()'s doc-comment) — not merely duty=0% while still
 * running. */
void pump_off(sx_pwm_software_t *pwm);

/* Arbitrary duty cycle, 0-100 (clamped by sx_pwm_software_set_duty()). If
 * not already running, starts it after setting duty. */
void pump_set_power(sx_pwm_software_t *pwm, uint8_t duty_percent);

#ifdef __cplusplus
}
#endif

#endif