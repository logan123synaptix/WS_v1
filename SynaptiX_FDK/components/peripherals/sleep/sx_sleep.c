#include "sx_sleep.h"
#include "sx_config.h"
#include "rtc.h"
#include "logger.h"

#if SX_PLATFORM == SX_PLATFORM_STM32H5
    #include "stm32h5xx_hal.h"
    #include "stm32h5xx_hal_pwr_ex.h"
    #include "stm32h5xx_hal_rtc_ex.h"
    /* STANDBY mode (2026-07-30 switch from STOP mode -- see doc-comment on
     * _enter_stop() below): the chip fully resets on wake, so none of the
     * per-peripheral DeInit()/Re-Init() dance STOP mode needed is relevant
     * here anymore -- the reset handles it. No board-specific peripheral
     * headers needed in this file any more. */
#elif SX_PLATFORM == SX_PLATFORM_STM32H7
    #include "stm32h7xx_hal.h"
    #include "stm32h7xx_hal_pwr_ex.h"
    #include "stm32h7xx_hal_rtc_ex.h"
#elif SX_PLATFORM == SX_PLATFORM_STM32F4
    #include "stm32f4xx_hal.h"
    #include "stm32f4xx_hal_pwr_ex.h"
    #include "stm32f4xx_hal_rtc_ex.h"
#elif SX_PLATFORM == SX_PLATFORM_STM32F1
    #include "stm32f1xx_hal.h"
    #include "stm32f1xx_hal_pwr.h"
    #include "stm32f1xx_hal_rtc_ex.h"
#endif

static const char *TAG = "SX_SLEEP";
static sx_sleep_t *s_instance = NULL;

typedef HAL_StatusTypeDef (*rtc_set_wakeup_fn_t)(RTC_HandleTypeDef *hrtc, uint32_t WakeUpCounter, uint32_t WakeUpClock, uint32_t WakeUpAutoClr);
typedef HAL_StatusTypeDef (*rtc_cancel_wakeup_fn_t)(RTC_HandleTypeDef *hrtc);

static rtc_set_wakeup_fn_t    s_set_wakeup    = HAL_RTCEx_SetWakeUpTimer_IT;
static rtc_cancel_wakeup_fn_t s_cancel_wakeup = HAL_RTCEx_DeactivateWakeUpTimer;

static void _enter_stop  (sx_sleep_t *mgr);
static void _set_rtc_wake(sx_sleep_t *mgr, uint32_t period_sec);
static void _cancel_rtc  (sx_sleep_t *mgr);

sx_sleep_ops_t sx_sleep_ops = {
    .enter_stop   = _enter_stop,
    .set_rtc_wake = _set_rtc_wake,
    .cancel_rtc   = _cancel_rtc,
};

/* STANDBY-mode entry (2026-07-30, switched from STOP mode).
 *
 * With STANDBY, the CPU and SRAM are fully powered off -- the only thing
 * that survives is the RTC/backup domain. There is no "resume where we
 * left off": HAL_PWR_EnterSTANDBYMode() below never returns. Waking from
 * STANDBY is indistinguishable from a hardware reset at the code level --
 * execution restarts at the reset vector, straight into main(), same as
 * a cold power-on. This means:
 *
 *   - None of STOP mode's per-peripheral DeInit()/Re-Init() dance is
 *     needed any more. The reset itself puts every peripheral back to its
 *     power-on-reset state; there is nothing to "restore" because nothing
 *     survived to begin with.
 *   - SX_SUSPEND_TICS()/SX_RESUME_TICS() (SysTick suspend/resume) no
 *     longer apply either -- SysTick's state is moot when the whole core
 *     resets.
 *   - post_wake_hook will NEVER run from here, since this function does
 *     not return on the STANDBY path. Any "wake-side" work (bringing
 *     GPS/modem/sensors back up) now belongs in the normal boot path
 *     (main() -> board_init()/app_init()), since with STANDBY every wake
 *     IS a boot. See main.c's PWR_FLAG_SBF check for how the boot path
 *     tells a wake-from-STANDBY apart from a cold power-on/reset button,
 *     if it needs to.
 *   - wake_reason (WAKE_REASON_RTC/EXTI, set via
 *     HAL_RTCEx_WakeUpTimerEventCallback() below) can likewise never be
 *     set this way any more: that callback only fires for an interrupt
 *     handled while the CPU is still alive, which is exactly what STOP
 *     mode allowed and STANDBY does not. Determining *why* the chip
 *     booted (RTC wakeup vs. cold power-on vs. reset button) after a
 *     STANDBY cycle has to happen via PWR_FLAG_SBF (+ optionally
 *     RCC_FLAG_LPWRRST) read at the very start of main(), not through
 *     this callback -- see main.c. */
static void _enter_stop(sx_sleep_t *mgr)
{
    s_instance = mgr;
    if (s_instance) {
        s_instance->wake_reason = WAKE_REASON_UNKNOWN;
    }

    if (mgr->pre_stop_hook) {
        mgr->pre_stop_hook(mgr->hook_ctx);
    }

    /* Enter STANDBY. Does not return -- the next code to run after this
     * point is main(), from the reset vector, on the next wake (RTC
     * timer) or reset (NRST/power-cycle). */
    HAL_PWR_EnterSTANDBYMode();
}

static void _set_rtc_wake(sx_sleep_t *mgr, uint32_t period_sec){
    // (void)mgr;
    // s_cancel_wakeup(&hrtc);
    // s_set_wakeup(&hrtc, period_sec - 1, RTC_WAKEUPCLOCK_CK_SPRE_16BITS, 0);
    (void)mgr;
    HAL_StatusTypeDef ret1, ret2;
    
    ret1 = HAL_RTCEx_DeactivateWakeUpTimer(&hrtc);
    ret2 = HAL_RTCEx_SetWakeUpTimer_IT(&hrtc, period_sec - 1, RTC_WAKEUPCLOCK_CK_SPRE_16BITS, 0);
    
    log_info("RTC", "DeactivateWakeUpTimer ret=%d", ret1);  
    log_info("RTC", "SetWakeUpTimer ret=%d counter=%lu",    
             ret2, period_sec - 1);
}

static void _cancel_rtc(sx_sleep_t *mgr){
    (void)mgr;
    s_cancel_wakeup(&hrtc);
}

void HAL_RTCEx_WakeUpTimerEventCallback(RTC_HandleTypeDef *hrtc_cb){
    (void)hrtc_cb;
    log_debug(TAG, "RTC WakeUp callback triggered!");
    if (s_instance) {
        s_instance->wake_reason = WAKE_REASON_RTC;
        log_info(TAG, "[RTC CB] Set wake_reason = WAKE_REASON_RTC");
    } else {
        log_error(TAG, "[RTC CB] ERROR: s_instance is NULL!");
    }
}


sx_sleep_t* sx_sleep_get_instance(void)
{
    return s_instance;
}


void sx_sleep_set_exti_wake(void)
{
    log_debug(TAG, "sx_sleep_set_exti_wake() called!");
    if (s_instance) {
        s_instance->wake_reason = WAKE_REASON_EXTI;
        log_info(TAG, "[EXTI] Set wake_reason = WAKE_REASON_EXTI");
    } else {
        log_error(TAG, "[EXTI] ERROR: s_instance is NULL!");
    }
}