#ifndef SX_SLEEP_H
#define SX_SLEEP_H

#ifdef __cplusplus
extern "C" {
#endif
 
#include <stdint.h>

typedef enum {
    WAKE_REASON_UNKNOWN = 0,
    WAKE_REASON_RTC,
    WAKE_REASON_EXTI,
} wake_reason_t;

typedef struct sx_sleep sx_sleep_t;

typedef struct sx_sleep_ops {
    void (*enter_stop)  (sx_sleep_t *mgr);
    void (*set_rtc_wake)(sx_sleep_t *mgr, uint32_t period_sec);
    void (*cancel_rtc)  (sx_sleep_t *mgr);
} sx_sleep_ops_t;

/* This module (components/peripherals/sleep) only knows about the STOP-mode
 * power sequence + RTC wakeup timer. It has ZERO knowledge of what
 * peripherals a given board has (UART, USB, SPI/I2C DMA, ADC, ...) — that
 * is entirely board-specific and does not belong here. Anything that must
 * be quiesced before STOP mode and restored after waking is the caller's
 * job, plugged in via the two hooks below:
 *
 *   pre_stop_hook  — called right before actually entering STOP (after
 *                    wake_reason is reset, before ticks are suspended).
 *                    Board code puts whatever it needs here: abort UARTs,
 *                    disable USB IRQ, halt DMA, etc.
 *   post_wake_hook — called right after waking + SystemClock_Config() +
 *                    tick resume. Mirror of pre_stop_hook, for restoring
 *                    whatever it disabled (e.g. re-enable USB IRQ).
 *
 * Both receive hook_ctx as-is; this module never dereferences it. NULL
 * hook = nothing to do, which is a fully valid configuration (e.g. a board
 * with no peripherals needing special handling before sleep). */
typedef void (*sx_sleep_hook_t)(void *hook_ctx);

struct sx_sleep {
    sx_sleep_ops_t *ops;
    void                   *pDriver;
    wake_reason_t           wake_reason;

    sx_sleep_hook_t         pre_stop_hook;
    sx_sleep_hook_t         post_wake_hook;
    void                   *hook_ctx;
};

static inline void sx_sleep_init(sx_sleep_t *mgr,
                                          sx_sleep_ops_t *ops,
                                          void *pDriver,
                                          sx_sleep_hook_t pre_stop_hook,
                                          sx_sleep_hook_t post_wake_hook,
                                          void *hook_ctx)
{
    mgr->ops         = ops;
    mgr->pDriver     = pDriver;
    mgr->wake_reason = WAKE_REASON_UNKNOWN;
    mgr->pre_stop_hook  = pre_stop_hook;
    mgr->post_wake_hook = post_wake_hook;
    mgr->hook_ctx       = hook_ctx;
}

static inline void sx_sleep_enter_stop(sx_sleep_t *mgr)
{
    if (mgr->ops && mgr->ops->enter_stop)
        mgr->ops->enter_stop(mgr);
}

static inline void sx_sleep_set_rtc_wake(sx_sleep_t *mgr, uint32_t period_sec)
{
    if (mgr->ops && mgr->ops->set_rtc_wake)
        mgr->ops->set_rtc_wake(mgr, period_sec);
}

static inline void sx_sleep_cancel_rtc(sx_sleep_t *mgr)
{
    if (mgr->ops && mgr->ops->cancel_rtc)
        mgr->ops->cancel_rtc(mgr);
}

static inline wake_reason_t sx_sleep_get_wake_reason(sx_sleep_t *mgr)
{
    return mgr->wake_reason;
}

/* Global sleep instance accessor for interrupt handlers to set wake reason */
sx_sleep_t* sx_sleep_get_instance(void);
void sx_sleep_set_exti_wake(void);

extern sx_sleep_ops_t sx_sleep_ops;

#ifdef __cplusplus
}
#endif

#endif