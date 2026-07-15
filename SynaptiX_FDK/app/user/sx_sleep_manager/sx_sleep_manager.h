#ifndef APP_SX_SLEEP_MANAGER_H
#define APP_SX_SLEEP_MANAGER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "sx_sleep_service.h"
#include "modem_ops.h"
#include "gps.h"
#include "sht3x.h"
#include "sx_gpio.h"
#include "sps30_app.h"

/* Tier 3 of the 3-tier sleep architecture (see sx_sleep_service.h for the
 * full tier breakdown). This is the ONLY place in the sleep stack that
 * knows GPS/modem are the concrete things being sequenced — tier 2
 * (sx_sleep_service) just runs whatever sx_sleep_step_t array it's given.
 *
 * Replaces the old services/sleepmanager/sx_sleep_manager.c/.h (hard-coded
 * SX_WAKE_STEP_* enum switch-case) with the same wake sequence — GPS on ->
 * wait for fix -> resume LTE/GPS UARTs -> power on + wait for modem ready
 * — but expressed as sx_sleep_step_t entries for sx_sleep_service, and a
 * sleep-side step (power down GPS + modem) instead of that logic being
 * hard-coded inside the old sx_sleep_manager_enter(). */

typedef struct {
    modem_handle_t *modem;
    sx_gps_t       *gps;
    sx_gpio_t      pump_io;
    /* SPS30 app-layer state machine — sleep_steps calls its own
     * sps30_app_sleep_step_start()/is_done() pair directly (already
     * matches sx_sleep_step_t's signature, see sps30_app.h), this
     * pointer is only kept so sx_sleep_manager_init() can pass it as
     * that step's ctx. */
    sps30_app_t    *sps30_app;
    /* Pump power-enable GPIO (EN_PW_PUMP), already sx_gpio_init()'d by
     * the caller (mirrors power_gpio in sps30_app_t) — this module only
     * drives it low via pump_off() in its own sleep-step wrapper, since
     * pump_off()'s signature doesn't match sx_sleep_step_t directly. */
    sx_gpio_t      *pump_gpio;
    sx_sleep_service_t svc;

    /* Elapsed time for the currently-running GPS-fix-wait step; owned by
     * this module since sx_sleep_service's generic per-step timeout alone
     * can't express "keep waiting for a GPS fix, but give up after
     * GPS_TIMEOUT_MS specifically, distinct from other steps' timeout". */
    uint32_t gps_wait_elapsed_ms;

    /* Same idea for the SIM/modem wake step — needs its own longer timeout
     * (90s) than the shared step_timeout_ms passed to
     * sx_sleep_service_init(), and needs to track whether start() has
     * already been sent once (modem_ops' start() must only be called
     * after power_on_start()'s async sequence reaches READY). */
    uint32_t sim_wait_elapsed_ms;
    uint8_t  sim_start_sent;
} sx_sleep_manager_t;

/* wake_steps run, in order, on every wake:
 *   1. GPS on           — power on GPS, resume its UART
 *   2. Wait for GPS fix — poll until lat/long non-zero or GPS_TIMEOUT_MS
 *   3. Resume LTE UART + power on modem
 *   4. Wait for modem ready — send start() once power_on_start() settles,
 *      poll is_ready(), hard-reset and retry once after 90s with no reply
 *   5. ZE12A back to Active Upload mode (gas_sensor_switch_to_active_mode())
 *      — cheap fire-and-forget UART command, always reports done.
 *
 * sleep_steps run, in order, before every sx_sleep_manager_enter_sleep():
 *   1. Power down GPS (zero out last fix so a stale fix isn't reused)
 *   2. Power down modem
 *   3. SPS30 power-down (sps30_app_sleep_step_start/is_done — SHDLC
 *      sleep() then EN_PW_DUST low). Caller must only trigger sleep once
 *      any in-progress SPS30 measurement cycle is DONE/IDLE (see
 *      sps30_app.h) — not enforced here, this module just runs the step.
 *   4. Pump off (EN_PW_PUMP low, via pump_off())
 *   5. ZE12A to Question & Answer mode (gas_sensor_switch_to_qa_mode())
 *      — reduces UART/processing load; does NOT reduce the electrochemical
 *      cell's own power draw (no such command exists per the datasheet).
 *
 * SHT3x and ADS1115 have no sleep_step here: both only ever run in
 * single-shot mode (no continuous conversion to stop), and I2C1 loses
 * clock in STOP mode regardless — see the sps30_app/sx_temp_humi handoff
 * notes for the datasheet/driver reasoning behind this. */
void sx_sleep_manager_init(sx_sleep_manager_t *mgr,
                            sx_sleep_t         *sleep,
                            modem_handle_t     *modem,
                            sx_gps_t           *gps,
                            sps30_app_t        *sps30_app,
                            sx_gpio_t          *pump_gpio);

/* Runs the sleep_steps then enters STOP mode via tier 2/1. Blocking, same
 * as the old sx_sleep_manager_enter() — returns only after waking. */
void sx_sleep_manager_enter_sleep(sx_sleep_manager_t *mgr, uint32_t sleep_sec);

/* Drives the wake_steps sequence — call once per app tick. */
void sx_sleep_manager_wake_process(sx_sleep_manager_t *mgr, uint32_t delta_ms);

uint8_t sx_sleep_manager_is_wake_done(sx_sleep_manager_t *mgr);

void sx_sleep_manager_reset_wake(sx_sleep_manager_t *mgr);

static inline wake_reason_t sx_sleep_manager_get_wake_reason(sx_sleep_manager_t *mgr)
{
    return sx_sleep_service_get_wake_reason(&mgr->svc);
}

#ifdef __cplusplus
}
#endif

#endif