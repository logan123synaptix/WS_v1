#ifndef SX_TIMER_H
#define SX_TIMER_H


#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef struct sx_timer sx_timer_t;

typedef void (*sx_timer_callback_t)(void *arg);

/* This driver computes ARR itself from two numbers the caller provides:
 *   - tick_hz:    the timer's REAL counter frequency in Hz, i.e. the timer
 *                 input clock divided by (Prescaler+1) — e.g. for STM32H5
 *                 TIM1 at a 32MHz timer clock with PSC=31999, tick_hz=1000
 *                 (1kHz, 1ms/tick); with PSC=31, tick_hz=1000000 (1MHz,
 *                 1us/tick). This is just arithmetic from values already
 *                 in the CubeMX-generated MX_TIMx_Init() (timer clock is a
 *                 property of the board's clock tree + which APBx bus the
 *                 TIMx sits on; Prescaler is read directly from that same
 *                 init function) — this module does not query the clock
 *                 tree itself, the caller supplies the already-known Hz.
 *   - timeout_ms: the desired period in real milliseconds.
 * ARR is then derived as round(tick_hz * timeout_ms / 1000) - 1, so the
 * caller never has to hand-tune a prescaler to hit exactly 1ms/tick (the
 * previous version of this driver hardcoded ARR = timeout_ms - 1, silently
 * assuming the caller had already set up a 1ms tick — wrong period with no
 * warning if that assumption didn't hold, e.g. after a prescaler chosen for
 * some other frequency). Any tick_hz/prescaler choice works correctly now,
 * which is the point: this makes the driver portable across projects/boards
 * with different clock trees or prescaler choices, not just one tuned by
 * hand to land on 1ms.
 *
 * Same "driver only touches ARR, never PSC at runtown" contract as before —
 * changing PSC while a timer is running is a known HAL/hardware footgun
 * (new value doesn't reliably take effect until the next update event, and
 * can corrupt in-flight timing), so tick_hz is fixed at sx_timer_init() time
 * and assumed constant for the life of the sx_timer_t (matching whatever
 * Prescaler the caller's CubeMX init already set and never changes). */
typedef struct sx_timer_ops{
    int (*start)(sx_timer_t *timer);
    int (*stop)(sx_timer_t *timer);
    int (*set_period)(sx_timer_t *timer, uint32_t timeout_ms);
} sx_timer_ops_t;

struct sx_timer{
    uint32_t timeout_ms;
    uint32_t tick_hz;    /* real counter frequency, Hz - see doc-comment above */
    sx_timer_callback_t callback;
    void *arg;
    sx_timer_ops_t *ops;
    /* Caller-owned hardware timer handle (e.g. TIM_HandleTypeDef* for
     * STM32 HAL). This module never allocates or configures it — the
     * caller sets up the timer peripheral (CubeMX MX_TIMx_Init() or
     * equivalent, any Prescaler/ARR, NVIC enabled) and passes the handle
     * in via sx_timer_init() along with the real tick_hz that Prescaler
     * choice produces, same pDriver-injection pattern as sx_gpio_t/
     * sx_spi_t. This lets one board run several independent sx_timer_t
     * instances (e.g. TIM3 for one purpose, TIM6 for another, each with
     * its own tick_hz) without this module knowing which TIMx is which. */
    void *pDriver;
};

/* _tick_hz: real counter frequency in Hz (timer input clock / (PSC+1)) —
 * see the doc-comment above sx_timer_ops_t for how to derive this from an
 * existing CubeMX MX_TIMx_Init(). Must be > 0. */
void sx_timer_init(sx_timer_t *_timer, sx_timer_ops_t *ops, void *_pDriver, uint32_t _tick_hz, uint32_t _timeout_ms, sx_timer_callback_t _callback, void *_arg);

/* Convenience init: give this the timer's INPUT clock in Hz (a fixed,
 * board-known constant - e.g. 32000000 for TIM1 on an STM32H5 at HCLK=32MHz
 * with APB2 prescaler=DIV1, see sx_board.c's comment where this is called)
 * and the interrupt frequency you actually want in Hz (e.g. 10000 for
 * 10kHz), and this computes and applies BOTH Prescaler and ARR itself via
 * the HAL, overwriting whatever MX_TIMx_Init()/tim.c set — no manual
 * CubeMX/tim.c tuning needed at all, unlike sx_timer_init() above which
 * still requires the caller to have picked a Prescaler by hand and pass
 * the resulting tick_hz in.
 *
 * Must be called BEFORE sx_timer_start() (ideally right after the HAL's
 * own MX_TIMx_Init(), before HAL_TIM_Base_Start_IT() ever runs) - this is
 * the one place PSC is touched, deliberately once at setup time, never
 * while the timer is running (changing PSC on a running timer is the HAL/
 * hardware footgun noted above; this function does not call HAL_TIM_Base_
 * Start_IT itself, only sx_timer_start()/_sx_timer_start() does, so as
 * long as you call this before your first sx_timer_start() the ordering
 * is safe).
 *
 * Internally finds the smallest Prescaler that keeps ARR within the
 * timer's 16-bit range (0..65535) - i.e. it maximizes tick_hz (best
 * resolution) for the requested freq_hz, rather than picking an arbitrary
 * Prescaler. Returns 0 on success, -1 if input_clock_hz/freq_hz/timer/timer
 * ops/pDriver are invalid, or if no valid Prescaler exists (freq_hz too low
 * for a 16-bit ARR even at Prescaler=65535, or freq_hz > input_clock_hz). */
int sx_timer_init_freq(sx_timer_t *_timer, sx_timer_ops_t *ops, void *_pDriver,
                        uint32_t _input_clock_hz, uint32_t _freq_hz,
                        sx_timer_callback_t _callback, void *_arg);

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