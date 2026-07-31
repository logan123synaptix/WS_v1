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

/* period_ms -> period_ticks using isr_rate_hz (how often the ISR actually
 * fires — see sx_pwm_sw.h's file doc-comment for why this must be the
 * ISR/overflow rate, not the timer's raw counter tick_hz). Same
 * round-to-nearest-then-floor-to-1 approach as before. */
static uint32_t _period_ms_to_isr_ticks(uint32_t isr_rate_hz, uint32_t period_ms){
    uint64_t ticks = ((uint64_t)isr_rate_hz * period_ms + 500) / 1000;
    if (ticks == 0) {
        ticks = 1; /* degenerate case: period_ms too short to represent
                     * even one ISR fire at this isr_rate_hz — clamp
                     * rather than run with period_ticks=0 (divide-by-zero
                     * elsewhere). Caller should really pass a larger
                     * period_ms or a faster isr_rate_hz; this is a safety
                     * floor, not a recommended mode. */
    }
    return (uint32_t)ticks;
}

void sx_pwm_software_init(sx_pwm_software_t *pwm, sx_gpio_t *gpio, sx_timer_t *timer,
                           uint32_t isr_rate_hz, uint32_t period_ms,
                           uint8_t duty_percent){
    pwm->gpio = gpio;
    pwm->timer = timer;
    pwm->tick_count = 0;
    pwm->running = 0;

    uint32_t period_ticks = _period_ms_to_isr_ticks(isr_rate_hz, period_ms);
    pwm->period_ticks = period_ticks;
    pwm->on_ticks = _clamp_duty_to_ticks(period_ticks, duty_percent);
}

void sx_pwm_software_start(sx_pwm_software_t *pwm){
    pwm->tick_count = 0;
    pwm->running = 1;
    /* Does NOT touch ARR/period any more — the timer's fixed ISR rate was
     * already programmed once by the caller before sx_pwm_software_init()
     * (see its doc-comment). Just enables counting + IT via ops->start_hw()
     * directly, bypassing sx_timer_start()/sx_timer_start_ticks() (both of
     * which would reprogram ARR from timeout_ms/period_ticks respectively —
     * exactly the bug this rewrite removes). */
    if (pwm->timer->ops && pwm->timer->ops->start_hw) {
        pwm->timer->ops->start_hw(pwm->timer);
    }
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

    /* Runs once per ISR fire (once per ARR overflow), NOT once per raw
     * counter tick — tick_count therefore correctly walks 0..period_ticks-1
     * across period_ticks separate ISR fires, giving a real HIGH-for-
     * on_ticks-fires/LOW-for-the-rest waveform instead of the old
     * always-basically-100%-or-0% bug (see sx_pwm_sw.h's file doc-comment
     * for the full root-cause explanation). */
    sx_gpio_write(pwm->gpio, (pwm->tick_count < pwm->on_ticks) ? SX_GPIO_HIGH : SX_GPIO_LOW);

    pwm->tick_count++;
    if (pwm->tick_count >= pwm->period_ticks) {
        pwm->tick_count = 0;
    }
}