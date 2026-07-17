#include "sx_timer.h"
#include "sx_config.h"

#if SX_PLATFORM == SX_PLATFORM_STM32H5
    #include "stm32h5xx_hal.h"
#elif SX_PLATFORM == SX_PLATFORM_STM32H7
    #include "stm32h7xx_hal.h"
#elif SX_PLATFORM == SX_PLATFORM_STM32F4
    #include "stm32f4xx_hal.h"
#elif SX_PLATFORM == SX_PLATFORM_STM32F1
    #include "stm32f1xx_hal.h"
#endif

void sx_timer_init(sx_timer_t *_timer, sx_timer_ops_t *ops, void *_pDriver, uint32_t _tick_hz, uint32_t _timeout_ms, sx_timer_callback_t _callback, void *_arg){
    _timer->ops        = ops;
    _timer->pDriver     = _pDriver;
    _timer->tick_hz     = _tick_hz;
    _timer->timeout_ms  = _timeout_ms;
    _timer->callback    = _callback;
    _timer->arg         = _arg;
}

#if SX_PLATFORM == SX_PLATFORM_STM32H5 || SX_PLATFORM == SX_PLATFORM_STM32H7 || SX_PLATFORM == SX_PLATFORM_STM32F4 || SX_PLATFORM == SX_PLATFORM_STM32F1

/* Sets ARR from timeout_ms and the timer's real tick_hz (see sx_timer.h's
 * doc-comment on sx_timer_ops_t for how tick_hz is derived) — no longer
 * assumes a pre-tuned 1ms tick; this works for any tick_hz/prescaler the
 * caller's CubeMX init happens to use.
 *
 * arr = round(tick_hz * timeout_ms / 1000) - 1, computed in 64-bit to avoid
 * overflow (tick_hz can be up to tens of MHz, timeout_ms up to seconds).
 * +500 before the /1000 divide is standard round-to-nearest, same reasoning
 * as sx_pwm_sw.c's _clamp_duty_to_ticks() rounding. Rejects an arr of 0
 * that would result from timeout_ms too small for the given tick_hz to
 * represent (e.g. asking for 1ms at a 500Hz tick_hz) rather than silently
 * producing a 1-tick period that's wrong by a large relative margin.
 *
 * Resets the counter to 0 so the new period starts clean rather than
 * partway through whatever the old ARR's cycle was — acceptable for this
 * driver's use case (e.g. PWM software / periodic app-level timers),
 * matches the same CNT-reset approach commonly used when reconfiguring a
 * running timer. */
static int _sx_timer_set_period(sx_timer_t *timer, uint32_t timeout_ms){
    TIM_HandleTypeDef *htim = (TIM_HandleTypeDef *)timer->pDriver;
    if (htim == NULL || timeout_ms == 0 || timer->tick_hz == 0) {
        return -1;
    }

    uint64_t ticks = ((uint64_t)timer->tick_hz * timeout_ms + 500) / 1000;
    if (ticks == 0) {
        return -1; /* timeout_ms too small to represent at this tick_hz */
    }

    timer->timeout_ms = timeout_ms;
    __HAL_TIM_SET_COUNTER(htim, 0);
    __HAL_TIM_SET_AUTORELOAD(htim, (uint32_t)(ticks - 1));
    return 0;
}

/* Applies the configured timeout_ms as ARR, then starts the timer in
 * interrupt mode (HAL_TIM_Base_Start_IT) so HAL_TIM_PeriodElapsedCallback()
 * fires on overflow — the caller's board code is expected to call
 * sx_timer_irq_handle() from there (see sx_timer.h's doc-comment on why
 * this module doesn't register that callback itself). */
static int _sx_timer_start(sx_timer_t *timer){
    TIM_HandleTypeDef *htim = (TIM_HandleTypeDef *)timer->pDriver;
    if (htim == NULL) {
        return -1;
    }
    if (_sx_timer_set_period(timer, timer->timeout_ms) != 0) {
        return -1;
    }
    return (HAL_TIM_Base_Start_IT(htim) == HAL_OK) ? 0 : -1;
}

static int _sx_timer_stop(sx_timer_t *timer){
    TIM_HandleTypeDef *htim = (TIM_HandleTypeDef *)timer->pDriver;
    if (htim == NULL) {
        return -1;
    }
    return (HAL_TIM_Base_Stop_IT(htim) == HAL_OK) ? 0 : -1;
}

/* Same ARR-programming as _sx_timer_set_period() but takes an exact tick
 * count directly instead of deriving it from timeout_ms — see
 * sx_timer_ops_t's doc-comment in sx_timer.h for why this path exists
 * (callers like sx_pwm_sw that already computed period_ticks from
 * timer->tick_hz themselves, needing sub-ms precision the ms path can't
 * express). Deliberately does not touch timer->timeout_ms, since ticks
 * aren't that field's unit. */
static int _sx_timer_set_period_ticks(sx_timer_t *timer, uint32_t period_ticks){
    TIM_HandleTypeDef *htim = (TIM_HandleTypeDef *)timer->pDriver;
    if (htim == NULL || period_ticks == 0) {
        return -1;
    }
    __HAL_TIM_SET_COUNTER(htim, 0);
    __HAL_TIM_SET_AUTORELOAD(htim, period_ticks - 1);
    return 0;
}

/* Enables the timer without touching ARR/PSC — the caller must have
 * already applied the period via set_period()/set_period_ticks() first
 * (see sx_timer_ops_t's doc-comment). Used by sx_timer_start_ticks() so
 * that path doesn't re-apply timeout_ms's ms-rounded period on top of the
 * exact period_ticks just set. */
static int _sx_timer_start_hw(sx_timer_t *timer){
    TIM_HandleTypeDef *htim = (TIM_HandleTypeDef *)timer->pDriver;
    if (htim == NULL) {
        return -1;
    }
    return (HAL_TIM_Base_Start_IT(htim) == HAL_OK) ? 0 : -1;
}

sx_timer_ops_t sx_timer_ops = {
    .start            = _sx_timer_start,
    .stop             = _sx_timer_stop,
    .set_period       = _sx_timer_set_period,
    .set_period_ticks = _sx_timer_set_period_ticks,
    .start_hw         = _sx_timer_start_hw,
};

/* See sx_timer.h's doc-comment for the full contract. Picks the smallest
 * Prescaler (largest resulting tick_hz, i.e. best timing resolution) such
 * that ARR = input_clock_hz/(PSC+1)/freq_hz - 1 still fits in TIM's 16-bit
 * ARR (0..65535) - both PSC and ARR are also 16-bit fields on every STM32
 * timer this project targets (even "32-bit" timers like TIM2/TIM5 on some
 * parts still have a 16-bit PSC), so both are range-checked here rather
 * than assumed. Iterates PSC from 0 upward instead of computing it
 * directly, since integer rounding on the direct formula can land one PSC
 * off the valid boundary either way - a plain search is simpler to get
 * right than reverse-engineering the exact rounding case, and 65536
 * iterations worst case is negligible at init time (this is not called
 * from an ISR or a hot path). */
int sx_timer_init_freq(sx_timer_t *_timer, sx_timer_ops_t *ops, void *_pDriver,
                        uint32_t _input_clock_hz, uint32_t _freq_hz,
                        sx_timer_callback_t _callback, void *_arg)
{
    if (_timer == NULL || ops == NULL || _pDriver == NULL ||
        _input_clock_hz == 0 || _freq_hz == 0 || _freq_hz > _input_clock_hz) {
        return -1;
    }

    TIM_HandleTypeDef *htim = (TIM_HandleTypeDef *)_pDriver;

    uint32_t psc = 0;
    uint32_t arr = 0;
    int found = 0;
    for (psc = 0; psc <= 0xFFFFU; psc++) {
        uint32_t tick_hz = _input_clock_hz / (psc + 1);
        if (tick_hz < _freq_hz) {
            break; /* tick_hz only decreases as psc grows - no larger psc can work either */
        }
        uint64_t period_ticks = (uint64_t)tick_hz / _freq_hz; /* truncates; see rounding note above */
        if (period_ticks == 0) {
            continue;
        }
        if (period_ticks - 1 <= 0xFFFFU) {
            arr = (uint32_t)(period_ticks - 1);
            found = 1;
            break;
        }
    }
    if (!found) {
        return -1; /* no PSC/ARR combination fits freq_hz in 16-bit ARR */
    }

    __HAL_TIM_SET_PRESCALER(htim, (uint32_t)psc);
    __HAL_TIM_SET_COUNTER(htim, 0);
    __HAL_TIM_SET_AUTORELOAD(htim, arr);

    uint32_t tick_hz = _input_clock_hz / (psc + 1);
    sx_timer_init(_timer, ops, _pDriver, tick_hz, /* timeout_ms computed below */ 0, _callback, _arg);
    /* timeout_ms is only meaningful in whole milliseconds (sx_timer_set_period()'s
     * unit) and freq_hz may not divide evenly into one - store the closest
     * whole-ms equivalent for bookkeeping/future sx_timer_set_period() calls,
     * but the ARR programmed above (exact for freq_hz) is what's actually
     * running right now, not this rounded value. */
    _timer->timeout_ms = (_freq_hz >= 1000) ? 1 : (1000 + _freq_hz / 2) / _freq_hz;

    return 0;
}

/* See sx_timer.h's doc-comment for the full contract — caller supplies
 * Prescaler/Period directly (like hand-tuning tim.c's CubeMX values),
 * no search/derivation of tick_hz happens here beyond the one division
 * needed to store it in the struct. */
int sx_timer_init_regs(sx_timer_t *_timer, sx_timer_ops_t *ops, void *_pDriver,
                        uint32_t _input_clock_hz, uint32_t _prescaler, uint32_t _period,
                        sx_timer_callback_t _callback, void *_arg)
{
    if (_timer == NULL || ops == NULL || _pDriver == NULL || _input_clock_hz == 0 ||
        _prescaler > 0xFFFFU || _period > 0xFFFFU) {
        return -1;
    }

    TIM_HandleTypeDef *htim = (TIM_HandleTypeDef *)_pDriver;

    __HAL_TIM_SET_PRESCALER(htim, _prescaler);
    __HAL_TIM_SET_COUNTER(htim, 0);
    __HAL_TIM_SET_AUTORELOAD(htim, _period);

    uint32_t tick_hz = _input_clock_hz / (_prescaler + 1);
    sx_timer_init(_timer, ops, _pDriver, tick_hz, /* timeout_ms: not meaningful
                   * here, caller chose period directly rather than a ms
                   * value — left 0, same "bookkeeping only" caveat as
                   * sx_timer_init_freq()'s timeout_ms above applies even
                   * more so here since there's no freq_hz to round from */
                   0, _callback, _arg);

    return 0;
}

#endif