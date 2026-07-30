#include "sx_sleep.h"
#include "sx_config.h"
#include "rtc.h"
#include "logger.h"

#if SX_PLATFORM == SX_PLATFORM_STM32H5
    #include "stm32h5xx_hal.h"
    #include "stm32h5xx_hal_pwr_ex.h"
    #include "stm32h5xx_hal_rtc_ex.h"
    /* Board-specific peripheral handles (hi2c1, hspi1, huart1..6) and
     * MX_*_Init() declarations, needed to DeInit/re-Init them around STOP
     * mode -- see the peripheral clock-gating fix in _enter_stop() below. */
    #include "i2c.h"
    #include "spi.h"
    #include "usart.h"
    #include "tim.h"
    #include "lptim.h"
    #include "icache.h"
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

#define SX_SUSPEND_TICS()          HAL_SuspendTick()
#define SX_RESUME_TICS()           HAL_ResumeTick()

static const char *TAG = "SX_SLEEP";
static sx_sleep_t *s_instance = NULL;

typedef void (*pwr_enter_stop_fn_t) (uint32_t Regulator, uint8_t STOPEntry);
typedef HAL_StatusTypeDef (*rtc_set_wakeup_fn_t)(RTC_HandleTypeDef *hrtc, uint32_t WakeUpCounter, uint32_t WakeUpClock, uint32_t WakeUpAutoClr);
typedef HAL_StatusTypeDef (*rtc_cancel_wakeup_fn_t)(RTC_HandleTypeDef *hrtc);

static pwr_enter_stop_fn_t    s_enter_stop    = HAL_PWR_EnterSTOPMode;
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

/* Generic STOP-mode entry: this module knows nothing about which
 * peripherals exist on a given board (UART, USB, DMA, ...). Anything that
 * needs quiescing before STOP / restoring after wake is delegated to the
 * caller-supplied pre_stop_hook/post_wake_hook (see sx_sleep.h) — this
 * function only handles the power-mode + tick-suspend sequence and the
 * pending-SysTick-exception clear, which are intrinsic to STOP mode itself
 * on every board, not board-specific. */
static void _enter_stop(sx_sleep_t *mgr)
{
    s_instance = mgr;
    if (s_instance) {
        s_instance->wake_reason = WAKE_REASON_UNKNOWN;
    }

    if (mgr->pre_stop_hook) {
        mgr->pre_stop_hook(mgr->hook_ctx);
    }

    SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;

    __DSB();
    __ISB();

    /* Bug fix history (2026-07-30), kept for the next session:
     *
     * v0 (reverted): disabled HSE/PLL1/HSI48, on the theory that STOP mode
     * leaves source oscillators running. WRONG per the user's own ablation
     * test: erase-chip baseline 25mA; full MX_*_Init()+app running (through
     * this exact sleep path) 56mA; app_init/board_init/app_process
     * commented out but MX_*_Init() calls in main() KEPT -- still 56mA,
     * unchanged. That proves the ~31mA gap has nothing to do with this
     * function or with HSE/PLL (SystemClock_Config() runs identically at
     * boot regardless of app logic). Only commenting out the MX_*_Init()
     * calls themselves brought it down to 25mA -- so the gap is entirely
     * peripheral clock-enable bits (RCC_APBxENR/AHBxENR) left on from boot,
     * which STOP mode does NOT gate on its own (it only gates
     * SYSCLK/HCLK/PCLK at the core level).
     *
     * v1 (reverted): plain __HAL_RCC_xxx_CLK_DISABLE() for I2C1/SPI1/six
     * UARTs/TIM1/LPTIM1/ICACHE. Only dropped 56mA -> 50mA, nowhere near the
     * ~31mA the ablation test isolated. Cutting a clock-enable bit doesn't
     * change pin GPIO mode: I2C1's SDA/SCL (PB6/PB7, Core/Src/i2c.c) are
     * GPIO_MODE_AF_OD + NOPULL, relying on a real external pull-up
     * (confirmed with the user). With clock cut but pins still in AF_OD,
     * a line stuck LOW at the moment of cut (mid-transaction, or slave
     * clock-stretching) stays stuck for the whole STOP duration while the
     * external pull-up fights it -- burning current the entire time.
     *
     * v2 (current): call each peripheral's own HAL_*_DeInit() instead.
     * Confirmed by reading Core/Src/i2c.c's HAL_I2C_MspDeInit() (called by
     * HAL_I2C_DeInit()): it disables the clock AND calls HAL_GPIO_DeInit()
     * on PB6/PB7, resetting them to analog-input (reset-state default,
     * lowest leakage) instead of leaving them half-dead in AF_OD. Same
     * MspDeInit shape confirmed for SPI1 and all UARTs (CubeMX-generated).
     *
     * Deliberately NOT touched:
     *   - RTC: confirmed wake source (HAL_RTCEx_SetWakeUpTimer_IT below,
     *     Core/Src/rtc.c) -- disabling it would prevent wakeup entirely.
     *   - GPIO (all ports, GPIOx clock): MX_GPIO_Init() (Core/Src/gpio.c)
     *     actively drives several output pins to a fixed level as part of
     *     init (GPS_CPW, GPS_RST, SPI1_CS, EN_PW_DUST, I2C1_RST, LTE_RST,
     *     LTE_PWR_KEY). Re-running it after wake would silently force those
     *     pins back to boot-time level regardless of app state (e.g.
     *     LTE_RST held HIGH while the modem is powered -- re-init would
     *     yank it low and reset the modem mid-cycle). Cutting GPIOx clock
     *     could also let an actively-driven pin float during STOP instead
     *     of holding level. NOT YET CONFIRMED whether SPI1_CS specifically
     *     is left HIGH (deselected) before HAL_SPI_DeInit() runs below --
     *     if W25Q128 read/write glitches appear after this fix, check
     *     whether sleep_step order guarantees CS is deselected before this
     *     point (should be, since W25Q128 sleep_step runs earlier in the
     *     sequence, but not independently re-verified this session). */
    HAL_I2C_DeInit(&hi2c1);
    HAL_SPI_DeInit(&hspi1);
    HAL_UART_DeInit(&huart1);
    HAL_UART_DeInit(&huart2);
    HAL_UART_DeInit(&huart3);
    HAL_UART_DeInit(&huart4);
    HAL_UART_DeInit(&huart5);
    HAL_UART_DeInit(&huart6);
    /* Same root cause as the LPTIM1 fix below: HAL_TIM_Base_Init()
     * (stm32h5xx_hal_tim.c ~line 296) only calls Base_MspInit() -- which
     * enables the peripheral clock -- when htim->State == HAL_TIM_STATE_RESET.
     * A raw __HAL_RCC_TIM1_CLK_DISABLE() doesn't touch htim1.State, so the
     * post-wake MX_TIM1_Init() would skip MspInit and hit the exact same
     * class of hang as LPTIM1 did (confirmed via gdb backtrace) once wake
     * reaches this far. Use HAL_TIM_Base_DeInit() instead, for the same
     * reason HAL_LPTIM_DeInit() is used below instead of a raw disable. */
    HAL_TIM_Base_DeInit(&htim1);
    /* Must use HAL_LPTIM_DeInit(), NOT a raw __HAL_RCC_LPTIM1_CLK_DISABLE().
     * Confirmed via ST-Link + gdb backtrace at the actual hang (2026-07-30):
     *   #0 Error_Handler () at Core/Src/main.c:277
     *   #1 MX_LPTIM1_Init () at Core/Src/lptim.c:52
     *   #2 _enter_stop (...) at sx_sleep.c:187
     * Root cause: HAL_LPTIM_Init() (stm32h5xx_hal_lptim.c ~line 268) only
     * calls HAL_LPTIM_MspInit() -- which does __HAL_RCC_LPTIM1_CLK_ENABLE()
     * -- when hlptim1.State == HAL_LPTIM_STATE_RESET. A raw CLK_DISABLE()
     * before STOP does NOT touch hlptim1.State (it stays READY/BUSY from
     * the boot-time init), so on the post-wake MX_LPTIM1_Init() call the
     * State check is false, MspInit is skipped, the peripheral clock is
     * never turned back on, and HAL_LPTIM_Init() falls into
     * LPTIM_WaitForFlag(LPTIM_FLAG_REPOK) polling a register with no clock
     * -- the flag never sets, it times out, HAL_LPTIM_Init() returns
     * HAL_TIMEOUT, and MX_LPTIM1_Init() calls Error_Handler() (infinite
     * while(1) with IRQs disabled). HAL_LPTIM_DeInit() (used here) calls
     * HAL_LPTIM_MspDeInit() and resets State to HAL_LPTIM_STATE_RESET,
     * so the next MX_LPTIM1_Init() takes the MspInit branch again and
     * clock is correctly re-enabled -- same pattern already used for
     * I2C1/SPI1/UART above. */
    HAL_LPTIM_DeInit(&hlptim1);
    HAL_ICACHE_Disable();

    SX_SUSPEND_TICS();
    s_enter_stop(PWR_LOWPOWERREGULATOR_ON, PWR_STOPENTRY_WFI);

    /* ── After wake ──
     * MUST resume SysTick BEFORE calling SystemClock_Config(), not just
     * before MX_*_Init(). SystemClock_Config() -> HAL_RCC_OscConfig() waits
     * for HSERDY using `tickstart = HAL_GetTick()` + `HAL_GetTick() -
     * tickstart > RCC_HSE_TIMEOUT_VALUE` (stm32h5xx_hal_rcc.c ~line 571-573).
     * With SysTick still suspended (from SX_SUSPEND_TICS() before STOP),
     * HAL_GetTick() is frozen, so tickstart == HAL_GetTick() forever and the
     * timeout math never trips. If HSERDY isn't already set the instant
     * HAL_RCC_OscConfig() checks it after leaving STOP (real HSE needs some
     * settling time), this spins forever with no way out -- true infinite
     * hang, no log possible since UART isn't re-initialized yet. This
     * matches the observed symptom exactly: log stops right after
     * "[RTC CB] Set wake_reason = WAKE_REASON_RTC" (proves WFI/RTC wake
     * itself works fine) with nothing else ever printed, no matter which
     * peripherals are or aren't DeInit'd beforehand -- because the hang is
     * here, before SystemClock_Config() returns, before any MX_*_Init() or
     * UART is touched. The previous session's fix (moving SX_RESUME_TICS()
     * before MX_*_Init() but still after SystemClock_Config()) was the same
     * idea applied one call too late -- SystemClock_Config() has the exact
     * same HAL_GetTick()-based wait pattern as HAL_UART_Init() did. */
    SX_RESUME_TICS();

    extern void SystemClock_Config(void);
    SystemClock_Config();

    /* Re-run each peripheral's own MX_*_Init() (Core/Src/*.c) instead of
     * hand-rolling CLK_ENABLE + re-configuring registers ourselves -- this
     * guarantees the exact same init sequence/parameters as boot, with one
     * call site to keep in sync if pin/bus config ever changes. These are
     * plain HAL_*_Init() wrappers (checked i2c.c/spi.c: no GPIO pin-level
     * writes in here, only bus timing/mode config -- safe to re-run;
     * GPIO alternate-function pinmux happens in each HAL_*_MspInit(),
     * also safe, since AF mode doesn't change output level for pins not
     * already driven elsewhere). */
    MX_I2C1_Init();
    MX_SPI1_Init();
    MX_USART1_UART_Init();
    MX_USART2_UART_Init();
    MX_USART3_UART_Init();
    MX_UART4_Init();
    MX_UART5_Init();
    MX_USART6_UART_Init();
    MX_TIM1_Init();
    MX_LPTIM1_Init();
    MX_ICACHE_Init();

    if (mgr->post_wake_hook) {
        mgr->post_wake_hook(mgr->hook_ctx);
    }
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