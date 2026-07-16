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

void sx_timer_init(sx_timer_t *_timer, sx_timer_ops_t *ops, void *_pDriver, uint32_t _timeout_ms, sx_timer_callback_t _callback, void *_arg){
    _timer->ops        = ops;
    _timer->pDriver     = _pDriver;
    _timer->timeout_ms = _timeout_ms;
    _timer->callback    = _callback;
    _timer->arg         = _arg;
}

#if SX_PLATFORM == SX_PLATFORM_STM32H5 || SX_PLATFORM == SX_PLATFORM_STM32H7 || SX_PLATFORM == SX_PLATFORM_STM32F4 || SX_PLATFORM == SX_PLATFORM_STM32F1

/* Sets ARR from timeout_ms assuming 1 tick == 1ms (see sx_timer.h's
 * doc-comment on sx_timer_ops_t — the caller's CubeMX prescaler config is
 * what makes this assumption hold, not anything checked here). Resets the
 * counter to 0 so the new period starts clean rather than partway through
 * whatever the old ARR's cycle was — acceptable for this driver's use case
 * (e.g. PWM software / periodic app-level timers), matches the same
 * CNT-reset approach commonly used when reconfiguring a running timer. */
static int _sx_timer_set_period(sx_timer_t *timer, uint32_t timeout_ms){
    TIM_HandleTypeDef *htim = (TIM_HandleTypeDef *)timer->pDriver;
    if (htim == NULL || timeout_ms == 0) {
        return -1;
    }
    timer->timeout_ms = timeout_ms;
    __HAL_TIM_SET_COUNTER(htim, 0);
    __HAL_TIM_SET_AUTORELOAD(htim, timeout_ms - 1);
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

sx_timer_ops_t sx_timer_ops = {
    .start      = _sx_timer_start,
    .stop       = _sx_timer_stop,
    .set_period = _sx_timer_set_period,
};

#endif