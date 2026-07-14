#ifndef SX_SLEEP_H
#define SX_SLEEP_H

#ifdef __cplusplus
extern "C" {
#endif
 
#include <stdint.h>
#include "sx_uart.h"

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

struct sx_sleep {
    sx_sleep_ops_t *ops;
    void                   *pDriver;
    wake_reason_t           wake_reason;

    /* UARTs to abort (HAL_UART_Abort + clear pending IRQ, via
     * sx_uart_abort()) right before entering STOP mode. Generic by
     * design: this module knows nothing about which UARTs a given board
     * has (LTE/GPS/dust/gas/...) — the caller (board bring-up code)
     * populates this list with whichever sx_uart_t instances need a
     * clean abort before sleep. NULL/0 is valid (no UARTs to abort). */
    sx_uart_t             **uarts_to_abort;
    uint8_t                 uarts_to_abort_count;
};

static inline void sx_sleep_init(sx_sleep_t *mgr,
                                          sx_sleep_ops_t *ops,
                                          void *pDriver,
                                          sx_uart_t **uarts_to_abort,
                                          uint8_t uarts_to_abort_count)
{
    mgr->ops         = ops;
    mgr->pDriver     = pDriver;
    mgr->wake_reason = WAKE_REASON_UNKNOWN;
    mgr->uarts_to_abort       = uarts_to_abort;
    mgr->uarts_to_abort_count = uarts_to_abort_count;
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