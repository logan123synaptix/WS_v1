#include "sx_sleep_manager.h"
#include "sx_user_mqtt.h"
#include "sx_board.h"
#include "app_config.h"
#include "logger.h"

static const char *TAG = "SX_SLEEP_MGR";

void sx_sleep_manager_init(sx_sleep_manager_t *mgr,
                           sx_sleep_t         *sleep,
                           modem_handle_t     *modem,
                           sx_gps_t           *gps)
{
    mgr->sleep              = sleep;
    mgr->module.modem       = modem;
    mgr->module.gps         = gps;
    mgr->wake_step          = SX_WAKE_STEP_IDLE;
    mgr->published          = 0;
    mgr->elapsed.wake_elapsed_ms = 0;
    mgr->elapsed.gps_elapsed_ms  = 0;
    mgr->elapsed.sim_elapsed_ms  = 0;
    mgr->sleep_ms        = 0;   
    mgr->wake_timeout_ms = 0;
    mgr->sim_start_sent  = 0;

    log_info(TAG, "init OK — waiting for config values");
}

void sx_sleep_manager_enter(sx_sleep_manager_t *mgr)
{
    uint32_t sleep_ms  = (mgr->sleep_ms > 0) ? mgr->sleep_ms : SX_TIME_IN_SLEEP;
    uint32_t sleep_sec = sleep_ms / 1000U;
    if (sleep_sec == 0) sleep_sec = 1;

    log_info(TAG, "Entering sleep for %lu ms (%lu sec)", sleep_ms, sleep_sec);

    gps_power_off(mgr->module.gps);
    sx_delay_ms(100);
    mgr->module.gps->latitude   = 0.0f;
    mgr->module.gps->longtitude = 0.0f;
    mgr->module.modem->ops->power_off_start(mgr->module.modem->ctx);
    sx_delay_ms(500);

    log_info(TAG, "Setting RTC wakeup = %lu sec", sleep_sec);  
    sx_sleep_set_rtc_wake(mgr->sleep, sleep_sec);
    
    log_info(TAG, ">>> Entering STOP mode NOW");                
    sx_delay_ms(10);  
    
    sx_sleep_enter_stop(mgr->sleep);
    
    log_info(TAG, "<<< Woke from STOP mode");                  
    sx_sleep_cancel_rtc(mgr->sleep);
}

void sx_sleep_manager_wake_process(sx_sleep_manager_t *mgr, uint32_t delta_ms)
{
    switch (mgr->wake_step)
    {
    case SX_WAKE_STEP_IDLE:
        mgr->wake_step = SX_WAKE_STEP_GPS_ON_FIRST;
        break;

    case SX_WAKE_STEP_GPS_ON_FIRST:
        board_gps_uart_resume_it();
        log_info(TAG, "Power on GPS first");
        gps_power_on(mgr->module.gps);
        //board_gps_uart_resume_it();
        mgr->elapsed.gps_elapsed_ms = 0;
        mgr->wake_step = SX_WAKE_STEP_GPS_WAIT;
        break;

    case SX_WAKE_STEP_GPS_WAIT:
        mgr->elapsed.gps_elapsed_ms += delta_ms;

        if (mgr->module.gps->latitude  != 0.0f &&
            mgr->module.gps->longtitude != 0.0f)
        {
            log_info(TAG, "GPS fix OK after %lu ms — now starting SIM",
                     mgr->elapsed.gps_elapsed_ms);
            mgr->wake_step = SX_WAKE_STEP_UART_RESUME;
        }
        else if (mgr->elapsed.gps_elapsed_ms >= GPS_TIMEOUT_MS)
        {
            log_warn(TAG, "GPS timeout — proceed without fix, starting SIM");
            mgr->wake_step = SX_WAKE_STEP_UART_RESUME;
        }
        break;

    case SX_WAKE_STEP_UART_RESUME:
        log_info(TAG, "Resume UART + Power on SIM");

        sx_board_uart_resume_it();

        mgr->module.modem->ops->comm_reset(mgr->module.modem->ctx);
        mgr->module.modem->ops->power_on_start(mgr->module.modem->ctx);

        mgr->elapsed.sim_elapsed_ms = 0;
        mgr->sim_start_sent         = 0;
        mgr->wake_step = SX_WAKE_STEP_SIM_WAKE;
        break;

    case SX_WAKE_STEP_SIM_WAKE:
        mgr->elapsed.sim_elapsed_ms += delta_ms;

        /* power_on_start() (called on entry to this step) is asynchronous —
         * its PWRKEY pulse + boot-probe sequence completes later, tracked
         * via power_is_busy(). start() must not be called until that
         * finishes (power_state == READY), same requirement as
         * sx_board_init()'s power-on sequencing. Send it exactly once, as
         * soon as power is no longer busy. */
        if (!mgr->sim_start_sent &&
            !mgr->module.modem->ops->power_is_busy(mgr->module.modem->ctx)) {
            mgr->module.modem->ops->start(mgr->module.modem->ctx);
            mgr->sim_start_sent = 1;
        }

        if (mgr->module.modem->ops->is_ready(mgr->module.modem->ctx)) {
            log_info(TAG, "SIM ready after wake");
            mgr->wake_step = SX_WAKE_STEP_DONE;
        } else if (mgr->elapsed.sim_elapsed_ms >= 90000U) {
            log_warn(TAG, "SIM timeout — hard reset (RST pin)");
            mgr->elapsed.sim_elapsed_ms = 0;
            mgr->sim_start_sent         = 0;
            mgr->module.modem->ops->hard_reset(mgr->module.modem->ctx);
            /* hard_reset() re-enters the same async power sequence as
             * power_on_start() (see a7677s.c's A7677S_PWR_RST_PULSE ->
             * A7677S_PWR_WAIT_BOOT handoff) — the power_is_busy() check
             * above will naturally wait for it and re-send start() once,
             * same as after the initial power_on_start(). */
        }
        break;

    case SX_WAKE_STEP_DONE:
        break;

    default:
        break;
    }
}

uint8_t sx_sleep_manager_is_wake_done(sx_sleep_manager_t *mgr)
{
    return (mgr->wake_step == SX_WAKE_STEP_DONE);
}

uint8_t sx_sleep_manager_wake_tick(sx_sleep_manager_t *mgr, uint32_t delta_ms)
{
    uint32_t timeout_ms = (mgr->wake_timeout_ms > 0)
                          ? mgr->wake_timeout_ms
                          : SX_TIME_IN_WAKE;

    mgr->elapsed.wake_elapsed_ms += delta_ms;
    
    if (mgr->elapsed.wake_elapsed_ms >= timeout_ms) {  
        return 1;
    }
    return 0;
}

void sx_sleep_manager_reset_wake(sx_sleep_manager_t *mgr)
{
    mgr->wake_step                   = SX_WAKE_STEP_IDLE;
    mgr->published                   = 0;
    mgr->elapsed.wake_elapsed_ms     = 0;
    mgr->elapsed.gps_elapsed_ms      = 0;
    mgr->elapsed.sim_elapsed_ms      = 0;
    mgr->sim_start_sent              = 0;
}