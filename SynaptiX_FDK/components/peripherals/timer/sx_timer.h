#ifndef SX_TIMER_H
#define SX_TIMER_H


#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef struct sx_timer sx_timer_t;

typedef void (*sx_timer_callback_t)(void *arg);

/* IMPORTANT ASSUMPTION for the STM32 HAL driver (sx_timer_ops, sx_timer.c):
 * timeout_ms/set_period()'s timeout_ms are both in milliseconds, and the
 * driver only ever touches the timer's ARR (auto-reload register) — it
 * never touches the prescaler at runtime (changing PSC while a timer is
 * running is a known HAL/hardware footgun: the new value doesn't always
 * take effect predictably until the next update event, and can corrupt
 * in-flight timing). This means the CALLER is responsible for configuring
 * the timer's prescaler (via CubeMX MX_TIMx_Init() or equivalent) so that
 * one timer tick equals exactly 1ms for the given APB clock — sx_timer.c
 * has no way to verify this and will silently produce wrong periods if it
 * doesn't hold. */
typedef struct sx_timer_ops{
    int (*start)(sx_timer_t *timer);
    int (*stop)(sx_timer_t *timer);
    int (*set_period)(sx_timer_t *timer, uint32_t timeout_ms);
} sx_timer_ops_t;

struct sx_timer{
    uint32_t timeout_ms;
    sx_timer_callback_t callback;
    void *arg;
    sx_timer_ops_t *ops;
    /* Caller-owned hardware timer handle (e.g. TIM_HandleTypeDef* for
     * STM32 HAL). This module never allocates or configures it — the
     * caller sets up the timer peripheral (CubeMX MX_TIMx_Init() or
     * equivalent, prescaler/ARR for a 1ms tick, NVIC enabled) and passes
     * the handle in via sx_timer_init(), same pDriver-injection pattern
     * as sx_gpio_t/sx_spi_t. This lets one board run several independent
     * sx_timer_t instances (e.g. TIM3 for one purpose, TIM6 for another)
     * without this module knowing which TIMx is which. */
    void *pDriver;
};

void sx_timer_init(sx_timer_t *_timer, sx_timer_ops_t *ops, void *_pDriver, uint32_t _timeout_ms, sx_timer_callback_t _callback, void *_arg);
static inline int sx_timer_start(sx_timer_t *_timer){
    if(_timer->ops && _timer->ops->start){
        return _timer->ops->start(_timer);
    }
    return -1;
}
static inline int sx_timer_stop(sx_timer_t *_timer){
    if(_timer->ops && _timer->ops->stop){
        return _timer->ops->stop(_timer);
    }
    return -1;
}
static inline int sx_timer_set_period(sx_timer_t *_timer, uint32_t _timeout_ms){
    if(_timer->ops && _timer->ops->set_period){
        return _timer->ops->set_period(_timer, _timeout_ms);
    }
    return -1;
}

/* Call this from the board's own HAL_TIM_PeriodElapsedCallback() (or
 * whatever platform-equivalent ISR entry point), once per sx_timer_t
 * instance whose underlying pDriver fired. This module does not register
 * itself as a global HAL callback and keeps no internal instance registry
 * — same division of responsibility as sx_uart_t/sx_uart_rx_callback():
 * the board layer already knows which TIM_HandleTypeDef* maps to which
 * sx_timer_t* (it owns both), so it is the natural place to dispatch from,
 * exactly like HAL_UART_RxCpltCallback() in sx_board.c compares `huart`
 * against bsp_uart[] before calling sx_uart_rx_callback(). Safe to call
 * with a NULL callback (checked internally) or a NULL timer (no-op). */
static inline void sx_timer_irq_handle(sx_timer_t *_timer){
    if(_timer && _timer->callback){
        _timer->callback(_timer->arg);
    }
}

extern sx_timer_ops_t sx_timer_ops;

#ifdef __cplusplus
}
#endif

#endif // SX_TIMER_H