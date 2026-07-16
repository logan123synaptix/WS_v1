#include "sx_pump.h"

void pump_init(sx_pwm_software_t *pwm, sx_gpio_t *gpio, sx_gpio_ops_t *ops,
                void *gpio_pDriver, sx_timer_t *timer)
{
    sx_gpio_init(gpio, ops, gpio_pDriver);
    /* period_ms/duty_percent here are just the initial struct state before
     * anyone starts the pump — actual run duty is set by pump_on()/
     * pump_set_power() below. period_ms=10 matches the value already
     * settled on for this board (100Hz PWM, 1% duty resolution at TIM1's
     * 1MHz tick_hz). */
    sx_pwm_software_init(pwm, gpio, timer, 10U, 0U);
}

void pump_on(sx_pwm_software_t *pwm)
{
    sx_pwm_software_set_duty(pwm, PUMP_FULL_DRIVE_PERCENT);
    if (!pwm->running) {
        sx_pwm_software_start(pwm);
    }
}

void pump_off(sx_pwm_software_t *pwm)
{
    sx_pwm_software_stop(pwm);
}

void pump_set_power(sx_pwm_software_t *pwm, uint8_t duty_percent)
{
    sx_pwm_software_set_duty(pwm, duty_percent);
    if (!pwm->running) {
        sx_pwm_software_start(pwm);
    }
}
EOF
cat SynaptiX_FDK/components/modules/pump/sx_pump.c
Output

#include "sx_pump.h"

void pump_init(sx_pwm_software_t *pwm, sx_gpio_t *gpio, sx_gpio_ops_t *ops,
                void *gpio_pDriver, sx_timer_t *timer)
{
    sx_gpio_init(gpio, ops, gpio_pDriver);
    /* period_ms/duty_percent here are just the initial struct state before
     * anyone starts the pump — actual run duty is set by pump_on()/
     * pump_set_power() below. period_ms=10 matches the value already
     * settled on for this board (100Hz PWM, 1% duty resolution at TIM1's
     * 1MHz tick_hz). */
    sx_pwm_software_init(pwm, gpio, timer, 10U, 0U);
}

void pump_on(sx_pwm_software_t *pwm)
{
    sx_pwm_software_set_duty(pwm, PUMP_FULL_DRIVE_PERCENT);
    if (!pwm->running) {
        sx_pwm_software_start(pwm);
    }
}

void pump_off(sx_pwm_software_t *pwm)
{
    sx_pwm_software_stop(pwm);
}

void pump_set_power(sx_pwm_software_t *pwm, uint8_t duty_percent)
{
    sx_pwm_software_set_duty(pwm, duty_percent);
    if (!pwm->running) {
        sx_pwm_software_start(pwm);
    }
}