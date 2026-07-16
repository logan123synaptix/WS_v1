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

/* period_ms -> period_ticks using the timer's REAL tick_hz (not a fixed
 * assumed ms-per-tick) — same round-to-nearest-then-floor-to-1 approach
 * as sx_timer.c's own ARR derivation (_sx_timer_set_period()), so this
 * module's notion of "how many ticks is period_ms" always matches
 * whatever ARR sx_timer_start_ticks() will actually program. */
static uint32_t _period_ms_to_ticks(uint32_t tick_hz, uint32_t period_ms){
    uint64_t ticks = ((uint64_t)tick_hz * period_ms + 500) / 1000;
    if (ticks == 0) {
        ticks = 1; /* degenerate case: period_ms too short to represent
                     * even one tick at this tick_hz — clamp rather than
                     * run with period_ticks=0 (divide-by-zero elsewhere).
                     * Caller should really pass a larger period_ms; this
                     * is a safety floor, not a recommended mode. */
    }
    return (uint32_t)ticks;
}

void sx_pwm_software_init(sx_pwm_software_t *pwm, sx_gpio_t *gpio, sx_timer_t *timer,
                           uint32_t period_ms, uint8_t duty_percent){
    pwm->gpio = gpio;
    pwm->timer = timer;
    pwm->tick_count = 0;
    pwm->running = 0;

    uint32_t period_ticks = _period_ms_to_ticks(timer->tick_hz, period_ms);
    pwm->period_ticks = period_ticks;
    pwm->on_ticks = _clamp_duty_to_ticks(period_ticks, duty_percent);
}

void sx_pwm_software_start(sx_pwm_software_t *pwm){
    pwm->tick_count = 0;
    pwm->running = 1;
    /* Applies pwm->period_ticks directly as ARR (exact, no ms-rounding),
     * then starts the timer — see sx_timer_start_ticks()'s doc-comment in
     * sx_timer.h for why this path is used instead of sx_timer_start(). */
    sx_timer_start_ticks(pwm->timer, pwm->period_ticks);
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