#include "sx_pwm_sw.h"
#include "stdio.h"

static uint32_t _clamp_duty_to_ticks(uint32_t period_ticks, uint8_t duty_percent){
    if (duty_percent > 100) {
        duty_percent = 100;
    }
    /* Integer math: on_ticks = round(period_ticks * duty_percent / 100).
     * +50 before the divide is standard round-to-nearest, avoids always
     * truncating down (e.g. period_ticks=3, duty=50% would floor to 1
     * tick/33% without rounding — noticeably off for short periods). */
    return (uint32_t)(((uint64_t)period_ticks * duty_percent + 50) / 100);
}

void sx_pwm_software_init(sx_pwm_software_t *pwm, sx_gpio_t *gpio, sx_timer_t *timer,
                           uint32_t period_ms, uint8_t duty_percent){
    pwm->gpio = gpio;
    pwm->timer = timer;
    pwm->tick_count = 0;
    pwm->running = 0;

    uint32_t period_ticks = period_ms / SX_PWM_SOFTWARE_TICK_MS;
    if (period_ticks == 0) {
        period_ticks = 1; /* degenerate case: period_ms shorter than a
                            * single tick — clamp rather than divide by
                            * zero later. Caller should really pass a
                            * period_ms >= SX_PWM_SOFTWARE_TICK_MS; this
                            * is a safety floor, not a recommended mode. */
    }
    pwm->period_ticks = period_ticks;
    pwm->on_ticks = _clamp_duty_to_ticks(period_ticks, duty_percent);

    /* Keep the underlying sx_timer_t's own timeout_ms in sync with the
     * tick period this module actually wants — sx_timer_start() applies
     * timer->timeout_ms as the hardware period, so this must be set
     * before the caller calls sx_pwm_software_start(). */
    pwm->timer->timeout_ms = SX_PWM_SOFTWARE_TICK_MS;
}

void sx_pwm_software_start(sx_pwm_software_t *pwm){
    pwm->tick_count = 0;
    pwm->running = 1;
    sx_timer_start(pwm->timer);
}

void sx_pwm_software_stop(sx_pwm_software_t *pwm){
    pwm->running = 0;
    sx_timer_stop(pwm->timer);
    sx_gpio_write(pwm->gpio, SX_GPIO_LOW);
}

void sx_pwm_software_set_duty(sx_pwm_software_t *pwm, uint8_t duty_percent){
    pwm->on_ticks = _clamp_duty_to_ticks(pwm->period_ticks, duty_percent);
}

uint8_t sx_pwm_software_get_duty_percent(const sx_pwm_software_t *pwm){
    if (pwm->period_ticks == 0) {
        return 0;
    }
    return (uint8_t)((pwm->on_ticks * 100) / pwm->period_ticks);
}

void sx_pwm_software_tick_cb(void *arg){
    sx_pwm_software_t *pwm = (sx_pwm_software_t *)arg;
    if (pwm == NULL || !pwm->running) {
        return;
    }

    sx_gpio_write(pwm->gpio, (pwm->tick_count < pwm->on_ticks) ? SX_GPIO_HIGH : SX_GPIO_LOW);

    pwm->tick_count++;
    if (pwm->tick_count >= pwm->period_ticks) {
        pwm->tick_count = 0;
    }
}