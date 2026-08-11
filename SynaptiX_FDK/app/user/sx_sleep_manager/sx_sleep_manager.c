#include "sx_sleep_manager.h"
#include "ze12a.h"
#include "gas_sensor_app.h"
#include "app_config.h"
#include "network_config.h"
#include "sx_board.h"
#include "iwdg.h"
#include "sx_delay.h"
#include "sx_pump.h"
#include "sx_ex_storage.h"
#include "sx_user_mqtt.h"
#include "logger.h"

static const char *TAG = "SX_SLEEP_MGR";

/* ===================== wake steps ===================== */

/* Step 0: W25Q128 wake — release SPI Deep Power-Down via sx_storage_wake()
 * (sx_ex_storage.h), which reaches the real, correctly-initialized flash
 * instance. Fire-and-forget: the SPI command itself is synchronous, so
 * this step is always done after one tick. No ctx needed — both
 * sx_storage_sleep()/wake() are void(void). */
static void _ext_flash_wake_start(void *ctx)
{
    (void)ctx;
    sx_storage_wake();
}

static uint8_t _ext_flash_wake_is_done(void *ctx)
{
    (void)ctx;
    return 1;
}

/* Step 1: GPS on. */
static void _gps_on_start(void *ctx)
{
    sx_sleep_manager_t *mgr = (sx_sleep_manager_t *)ctx;
    board_gps_uart_resume_it();
    log_info(TAG, "Power on GPS first");
    gps_power_on(mgr->gps);
    mgr->gps_wait_elapsed_ms = 0;
}

static uint8_t _gps_on_is_done(void *ctx)
{
    (void)ctx;
    return 1; /* fire-and-forget kick-off, always "done" after one tick */
}

/* Step 2: wait for GPS fix, up to network_config_get()->gps_timeout_ms
 * (runtime-editable via the CLI's "settings -c -gpstimeout", defaults to
 * app_config.h's GPS_TIMEOUT_MS -- see network_config.c's build_defaults()).
 * This step owns its own timeout tracking (mgr->gps_wait_elapsed_ms)
 * rather than relying on sx_sleep_service's shared step_timeout_ms — GPS
 * is deliberately given a much longer allowance than the modem wake step
 * (90s), and the two must stay independent: a timeout here still counts
 * as "done" (this step gives up and moves on with an unfixed GPS
 * position, i.e. lat/long still 0.0f), it does not abort the wake
 * sequence. */
static void _gps_wait_start(void *ctx)
{
    (void)ctx;
    /* nothing to kick off — GPS was already powered on in the previous
     * step; this step just polls for a fix. */
}

static uint8_t _gps_wait_is_done(void *ctx)
{
    sx_sleep_manager_t *mgr = (sx_sleep_manager_t *)ctx;

    if (mgr->gps->latitude != 0.0f && mgr->gps->longtitude != 0.0f) {
        log_info(TAG, "GPS fix OK after %lu ms — proceeding",
                 mgr->gps_wait_elapsed_ms);
        return 1;
    }
    if (mgr->gps_wait_elapsed_ms >= network_config_get()->gps_timeout_ms) {
        log_warn(TAG, "GPS timeout after %lu ms — proceeding without fix",
                 mgr->gps_wait_elapsed_ms);
        return 1;
    }
    return 0;
}

/* Step 3: resume LTE UART + power on modem (async). */
static void _modem_power_on_start(void *ctx)
{
    sx_sleep_manager_t *mgr = (sx_sleep_manager_t *)ctx;
    log_info(TAG, "Resume UART + Power on SIM");
    sx_board_uart_resume_it();
    mgr->modem->ops->comm_reset(mgr->modem->ctx);
    mgr->modem->ops->power_on_start(mgr->modem->ctx);
    mgr->sim_wait_elapsed_ms = 0;
    mgr->sim_start_sent      = 0;
}

static uint8_t _modem_power_on_is_done(void *ctx)
{
    (void)ctx;
    return 1; /* fire-and-forget kick-off */
}

/* Step 4: wait for modem ready, up to 90s (this step's own timeout,
 * independent of GPS's 130s and of any shared sx_sleep_service timeout —
 * same reasoning as _gps_wait_is_done() above). start() must only be sent
 * once power_on_start()'s async sequence is no longer busy (mirrors the
 * same requirement in sx_board_init()). A 90s timeout with no reply
 * triggers exactly one hard_reset() + retry, matching the old
 * sx_sleep_manager_enter()'s behavior. */
#define SX_SLEEP_MGR_SIM_WAIT_TIMEOUT_MS  90000U

static void _modem_wait_ready_start(void *ctx)
{
    (void)ctx;
    /* nothing to kick off here — power_on_start() already ran in the
     * previous step; this step polls power_is_busy()/is_ready() and sends
     * start() exactly once when the modem stops being busy. */
}

static uint8_t _modem_wait_ready_is_done(void *ctx)
{
    sx_sleep_manager_t *mgr = (sx_sleep_manager_t *)ctx;

    if (!mgr->sim_start_sent &&
        !mgr->modem->ops->power_is_busy(mgr->modem->ctx)) {
        mgr->modem->ops->start(mgr->modem->ctx);
        mgr->sim_start_sent = 1;
    }

    if (mgr->modem->ops->is_ready(mgr->modem->ctx)) {
        log_info(TAG, "SIM ready after wake");
        return 1;
    }

    if (mgr->sim_wait_elapsed_ms >= SX_SLEEP_MGR_SIM_WAIT_TIMEOUT_MS) {
        log_warn(TAG, "SIM timeout — hard reset (RST pin)");
        mgr->sim_wait_elapsed_ms = 0;
        mgr->sim_start_sent      = 0;
        mgr->modem->ops->hard_reset(mgr->modem->ctx);
        /* hard_reset() re-enters the same async power sequence as
         * power_on_start() — the power_is_busy() check above will
         * naturally wait for it and re-send start() once, same as after
         * the initial power_on_start(). Not "done" yet; keep polling. */
    }
    return 0;
}

/* Step 5: ZE12A back to Active Upload mode. Cheap one-shot UART command
 * (no reply to wait for per the datasheet's table 6) — always reports
 * done on the first poll, same fire-and-forget style as gps_on/
 * modem_power_on above. */
static void _gas_sensor_active_mode_start(void *ctx)
{
    (void)ctx;
    /* Re-arm UART_EXTEND in case board_sleep_pre_stop_hook() aborted it
     * — mirrors the LTE/GPS resume calls in the wake steps above; ZE12A
     * has no "start_cycle" equivalent of its own, so this wake step is
     * the first point that needs the UART live again. */
    board_extend_uart_resume_it();
    gas_sensor_switch_to_active_mode();
}

static uint8_t _gas_sensor_active_mode_is_done(void *ctx)
{
    (void)ctx;
    return 1;
}

/* Step 6: BNO055 resume — accel_app.h's start()/is_done() pair already
 * matches sx_sleep_step_t's signature exactly, wired in directly below
 * with mgr->accel_app as ctx. No wrapper needed for this one, same as
 * SPS30's sleep-side step. */

/* ===================== sleep steps ===================== */

/* Step 0: W25Q128 sleep — enter SPI Deep Power-Down via sx_storage_sleep()
 * (sx_ex_storage.h). Bug fix (2026-07-30): originally called
 * sx_W25Q128_sleep_on() directly against a separate, never-initialized
 * board.q128 (spi pointer garbage/NULL from zero-init) — that caused a
 * HardFault (board froze right after this step's log line, no ST-Link
 * available to confirm, but every softer explanation -- BUSY-wait
 * timeout, HAL SPI timeout -- was already ruled out since both have
 * their own timeouts and neither would explain a silent freeze with no
 * further log at all). Going through sx_storage_sleep()/wake() instead
 * reaches the correctly-initialized static instance owned by
 * sx_ex_storage.c, with the real SPI/CS wiring set up in
 * sx_storage_init(). */
static void _ext_flash_sleep_start(void *ctx)
{
    (void)ctx;
    sx_storage_sleep();
}

static uint8_t _ext_flash_sleep_is_done(void *ctx)
{
    (void)ctx;
    return 1;
}

/* Step 1: power down GPS, clear last fix so a stale position isn't reused
 * next wake. */
static void _gps_power_off_start(void *ctx)
{
    sx_sleep_manager_t *mgr = (sx_sleep_manager_t *)ctx;
    gps_power_off(mgr->gps);
    sx_delay_ms(100);
    mgr->gps->latitude   = 0.0f;
    mgr->gps->longtitude = 0.0f;
}

static uint8_t _gps_power_off_is_done(void *ctx)
{
    (void)ctx;
    return 1;
}

/* Step 2: power down modem. */
static void _modem_power_off_start(void *ctx)
{
    sx_sleep_manager_t *mgr = (sx_sleep_manager_t *)ctx;
    /* Bug fix (2026-08-01): mgr->modem->ops->power_off_start() below only
     * cuts the modem's physical power (PWRKEY pulse) -- it has no idea
     * sx_mqtt.c's mqtt->state even exists, let alone that it needs
     * resetting. If a publish was in flight or the connection was simply
     * still SX_MQTT_STATE_CONNECTED at the moment sleep began (the common
     * case -- see app.c's APP_CYCLE_WAIT_PUBLISH, which normally only
     * reaches sleep once idle), mqtt->state stays CONNECTED straight
     * through the power-off, the whole STOP-mode sleep, and the
     * subsequent wake/re-power-on -- nothing else ever moves it back to
     * DISCONNECTED (sx_mqtt_report_failure()'s escalate_recovery() only
     * manages a retry counter and possible hard-reset escalation, it does
     * not touch mqtt->state either, see sx_mqtt.c). Confirmed on real
     * hardware: after wake, _on_modem_ready() calls sx_mqtt_connect(),
     * which is a no-op because mqtt->state != DISCONNECTED/ERROR ("connect:
     * already connected or in progress"), so no new MQTT connection is
     * ever actually established even though the modem was fully
     * power-cycled and definitely has no live connection -- every publish
     * that cycle then fails with "mqtt_publish: not connected".
     * sx_user_mqtt_force_disconnect() (thin wrapper over
     * sx_mqtt_force_disconnect(), see sx_mqtt.h) resets mqtt->state to
     * DISCONNECTED without sending any AT command -- safe to call
     * unconditionally here even though the modem is about to lose power
     * anyway, since this only touches MCU-side state. */
    sx_user_mqtt_force_disconnect();
    mgr->modem->ops->power_off_start(mgr->modem->ctx);
    mgr->power_off_last_tick_ms = sx_gettick();
}

static uint8_t _modem_power_off_is_done(void *ctx)
{
    sx_sleep_manager_t *mgr = (sx_sleep_manager_t *)ctx;

    /* Bug fix (2026-07-30), second half: the power_is_busy() gate added
     * above only works if the driver's power state machine actually gets
     * ticked forward. power_off_start() only *starts* the async
     * PWRKEY-pulse-then-settle sequence -- advancing through its states
     * (A7677S_PWR_OFF_PULSE -> OFF_SETTLE -> IDLE) happens inside
     * a7677s_poll(ctx, ts), which normally only gets called via
     * modem_handle_poll() from sx_mqtt_poll() -> sx_user_mqtt_poll() ->
     * test_sleep_poll() each main-loop tick.
     *
     * sleep_steps (this one included) run inside
     * sx_sleep_service.c's _run_steps_blocking(), a tight blocking
     * while(!is_done()) { sx_delay_ms(10); } loop -- test_sleep_poll()
     * (and therefore modem_handle_poll()) never runs again until this
     * whole loop returns. Confirmed on real board (2026-07-30): with only
     * the power_is_busy() gate and no poll() call here, power_elapsed
     * inside the driver never advanced past 0 and this step hung forever
     * ("Power Off start" logged, then nothing -- VDD_EXT stayed at 1.8V
     * indefinitely instead of settling to 0V).
     *
     * Fix: tick the modem's state machine ourselves from here, using
     * sx_gettick() deltas, same units/mechanism _gps_wait_is_done()/
     * _modem_wait_ready_is_done() use via wake_process()'s delta_ms --
     * just sourced locally since this step has no delta_ms of its own. */
    uint32_t now = sx_gettick();
    uint32_t ts  = now - mgr->power_off_last_tick_ms;
    mgr->power_off_last_tick_ms = now;
    modem_handle_poll(mgr->modem, ts);

    return !mgr->modem->ops->power_is_busy(mgr->modem->ctx);
}

/* Step 3: SPS30 power-down — sps30_app.h's start()/is_done() pair already
 * matches sx_sleep_step_t's signature exactly, wired in directly below
 * with mgr->sps30_app as ctx. No wrapper needed for this one. */

/* Step 4: pump off (0% duty). pump_off()'s signature (sx_pwm_software_t*)
 * doesn't match sx_sleep_step_t (void (*)(void*)), so this thin wrapper
 * adapts it — same reasoning as the ZE12A wrapper above/below. */
static void _pump_off_start(void *ctx)
{
    sx_sleep_manager_t *mgr = (sx_sleep_manager_t *)ctx;
    pump_off(mgr->pump_pwm);
}

static uint8_t _pump_off_is_done(void *ctx)
{
    (void)ctx;
    return 1;
}

/* Step 5: ZE12A to Question & Answer mode, before SPS30/pump so the UART
 * command still has a quiet bus (SPS30's own UART is separate, no
 * conflict either way — order here just mirrors the gps/modem powerdown
 * pattern of "signal peripherals down last"). */
static void _gas_sensor_qa_mode_start(void *ctx)
{
    (void)ctx;
    gas_sensor_switch_to_qa_mode();
}

static uint8_t _gas_sensor_qa_mode_is_done(void *ctx)
{
    (void)ctx;
    return 1;
}

/* Step 6: BNO055 to Suspend mode — accel_app.h's start()/is_done() pair
 * already matches sx_sleep_step_t's signature exactly, wired in directly
 * below with mgr->accel_app as ctx. Placed last since it shares I2C1 with
 * SHT3x/ADS1115/RTC, which stay untouched regardless (see doc-comment in
 * sx_sleep_manager.h) — order relative to them doesn't matter, only that
 * it runs before STOP mode like every other sleep_step here. */

/* ===================== step tables ===================== */

/* NOTE on elapsed-time bookkeeping: sx_sleep_service's generic
 * step_elapsed_ms (passed to no one here) is intentionally NOT used for
 * the GPS-wait/SIM-wait steps' own timeouts — see the comments above.
 * Instead, this module ticks mgr->gps_wait_elapsed_ms/sim_wait_elapsed_ms
 * itself, in sx_sleep_manager_wake_process() below, before delegating to
 * sx_sleep_service_wake_process(). */
static sx_sleep_step_t s_wake_steps[7];
static sx_sleep_step_t s_sleep_steps[7];

/* Matches sx_sleep_service_t's pre_stop_refresh function pointer shape
 * exactly (void(void)) -- see that field's doc-comment in
 * sx_sleep_service.h for the full reasoning. This is the one place in
 * the 3-tier sleep stack that is allowed to know hiwdg/IWDG exists, since
 * this file (tier 3) is already the project-specific layer wiring GPS/
 * modem/etc into tier 2's generic step engine. */
static void _iwdg_refresh(void)
{
    HAL_IWDG_Refresh(&hiwdg);
}

void sx_sleep_manager_init(sx_sleep_manager_t *mgr,
                            sx_sleep_t         *sleep,
                            modem_handle_t     *modem,
                            sx_gps_t           *gps,
                            sps30_app_t        *sps30_app,
                            sx_pwm_software_t  *pump_pwm,
                            accel_app_t        *accel_app,
                            void (*publish_heartbeat)(void))
{
    mgr->modem     = modem;
    mgr->gps       = gps;
    mgr->sps30_app = sps30_app;
    mgr->pump_pwm  = pump_pwm;
    mgr->accel_app = accel_app;
    mgr->gps_wait_elapsed_ms = 0;
    mgr->sim_wait_elapsed_ms = 0;
    mgr->sim_start_sent      = 0;

    mgr->publish_heartbeat      = publish_heartbeat;
    mgr->hb_only_phase          = 0;
    mgr->hb_only_elapsed_ms     = 0;
    mgr->hb_only_start_sent     = 0;
    mgr->hb_only_publish_kicked = 0;

    s_wake_steps[0] = (sx_sleep_step_t){ .start = _ext_flash_wake_start, .is_done = _ext_flash_wake_is_done, .ctx = NULL, .name = "ext_flash_wake" };
    s_wake_steps[1] = (sx_sleep_step_t){ .start = _gps_on_start,          .is_done = _gps_on_is_done,          .ctx = mgr, .name = "gps_on" };
    s_wake_steps[2] = (sx_sleep_step_t){ .start = _gps_wait_start,        .is_done = _gps_wait_is_done,        .ctx = mgr, .name = "gps_wait_fix" };
    s_wake_steps[3] = (sx_sleep_step_t){ .start = _modem_power_on_start,  .is_done = _modem_power_on_is_done,  .ctx = mgr, .name = "modem_power_on" };
    s_wake_steps[4] = (sx_sleep_step_t){ .start = _modem_wait_ready_start,.is_done = _modem_wait_ready_is_done,.ctx = mgr, .name = "modem_wait_ready" };
    s_wake_steps[5] = (sx_sleep_step_t){ .start = _gas_sensor_active_mode_start, .is_done = _gas_sensor_active_mode_is_done, .ctx = mgr, .name = "gas_sensor_active_mode" };
    s_wake_steps[6] = (sx_sleep_step_t){ .start = accel_app_wake_step_start, .is_done = accel_app_wake_step_is_done, .ctx = mgr->accel_app, .name = "accel_resume" };

    s_sleep_steps[0] = (sx_sleep_step_t){ .start = _ext_flash_sleep_start, .is_done = _ext_flash_sleep_is_done, .ctx = NULL, .name = "ext_flash_sleep" };
    s_sleep_steps[1] = (sx_sleep_step_t){ .start = _gps_power_off_start,   .is_done = _gps_power_off_is_done,   .ctx = mgr, .name = "gps_power_off" };
    s_sleep_steps[2] = (sx_sleep_step_t){ .start = _modem_power_off_start, .is_done = _modem_power_off_is_done, .ctx = mgr, .name = "modem_power_off" };
    s_sleep_steps[3] = (sx_sleep_step_t){ .start = sps30_app_sleep_step_start, .is_done = sps30_app_sleep_step_is_done, .ctx = mgr->sps30_app, .name = "sps30_power_off" };
    s_sleep_steps[4] = (sx_sleep_step_t){ .start = _pump_off_start,        .is_done = _pump_off_is_done,        .ctx = mgr, .name = "pump_off" };
    s_sleep_steps[5] = (sx_sleep_step_t){ .start = _gas_sensor_qa_mode_start, .is_done = _gas_sensor_qa_mode_is_done, .ctx = mgr, .name = "gas_sensor_qa_mode" };
    s_sleep_steps[6] = (sx_sleep_step_t){ .start = accel_app_sleep_step_start, .is_done = accel_app_sleep_step_is_done, .ctx = mgr->accel_app, .name = "accel_suspend" };

    /* step_timeout_ms = 0 (no shared timeout) passed to sx_sleep_service:
     * every step here manages its own completion criteria (gps_on/
     * modem_power_on are one-shot kick-offs that report done immediately;
     * gps_wait/modem_wait_ready track their own elapsed time and decide
     * for themselves when to give up — see comments above). A shared
     * generic timeout would either cut GPS's 130s allowance short or let
     * the modem step run needlessly long; keeping it per-step avoids
     * that mismatch entirely. */
    /* pre_stop_refresh = _iwdg_refresh (2026-08-10): refreshes IWDG right
     * before sx_sleep_service_enter_sleep() actually calls
     * sx_sleep_enter_stop() -- i.e. after all 7 sleep_steps above have
     * finished running, immediately before IWDG's counter gets frozen by
     * the FLASH_OPTR.IWDG_STOP option byte (see main.c's
     * ensure_iwdg_frozen_in_stop_option_byte()). Complements, not
     * replaces, app.c's own HAL_IWDG_Refresh() calls during
     * APP_MODE_FULL_POWER/APP_MODE_WAKEUP and its one refresh right
     * before calling sx_sleep_manager_enter_sleep() — this one exists
     * specifically to cover the (currently short, but not guaranteed to
     * stay that way) time these 7 sleep_steps themselves take to run,
     * which app.c's earlier refresh cannot see into. */
    sx_sleep_service_init(&mgr->svc, sleep,
                           s_wake_steps, 7,
                           s_sleep_steps, 7,
                           0,
                           _iwdg_refresh);

    log_info(TAG, "init OK");
}

void sx_sleep_manager_enter_sleep(sx_sleep_manager_t *mgr, uint32_t sleep_sec)
{
    sx_sleep_service_enter_sleep(&mgr->svc, sleep_sec);
}

void sx_sleep_manager_wake_process(sx_sleep_manager_t *mgr, uint32_t delta_ms)
{
    /* Tick both this module's own per-step elapsed counters unconditionally.
     * Deliberately NOT peeking at mgr->svc.current_step to tick only the
     * "active" one — that field is tier 2's internal iteration state
     * (see sx_sleep_service.h: "do not touch directly"), and reaching into
     * it here would break the tier-2/tier-3 boundary this refactor exists
     * to establish. Ticking both is harmless: gps_wait_elapsed_ms is only
     * read by _gps_wait_is_done() (active during wake_steps[1]) and
     * sim_wait_elapsed_ms only by _modem_wait_ready_is_done() (active
     * during wake_steps[3]) — each is reset to 0 in step 0/2's start()
     * (_gps_on_start()/_modem_power_on_start()) just before the following
     * wait-step begins reading it; the one tick spent in the "on" step
     * itself (fire-and-forget, always is_done()==1) adds negligible skew
     * versus a 90-130s timeout. */
    mgr->gps_wait_elapsed_ms += delta_ms;
    mgr->sim_wait_elapsed_ms += delta_ms;

    sx_sleep_service_wake_process(&mgr->svc, delta_ms);
}

uint8_t sx_sleep_manager_is_wake_done(sx_sleep_manager_t *mgr)
{
    return sx_sleep_service_is_wake_done(&mgr->svc);
}

uint8_t sx_sleep_manager_is_waking(sx_sleep_manager_t *mgr)
{
    return !sx_sleep_service_is_wake_done(&mgr->svc);
}

void sx_sleep_manager_reset_wake(sx_sleep_manager_t *mgr)
{
    mgr->gps_wait_elapsed_ms = 0;
    mgr->sim_wait_elapsed_ms = 0;
    mgr->sim_start_sent      = 0;
    sx_sleep_service_reset_wake(&mgr->svc);
}

/* ===================== HB_ONLY mini-wake ===================== *
 * See the doc-comment block in sx_sleep_manager.h for the full design.
 * Deliberately hand-rolled here as its own tiny state machine rather than
 * routed through sx_sleep_service.c's generic wake_steps runner (tier 2):
 * tier 2 always runs its full fixed-size step array in order (7 wake
 * steps), with no way to run "just modem+accel" -- reusing the individual
 * _modem_power_on_start()/_modem_wait_ready_is_done() etc. step functions
 * directly here, exactly the way s_wake_steps[]/s_sleep_steps[] already
 * wire them up for the full sequence, keeps one source of truth for the
 * actual modem power-on/ready/power-off mechanics without duplicating
 * that logic or bypassing tier 2 for the full-wake case. */

/* IWDG refresh here mirrors app.c's own APP_MODE_FULL_POWER/WAKEUP
 * refresh -- HB_ONLY is a third "board is awake and doing work" mode from
 * the watchdog's point of view, so it needs the same per-tick refresh.
 * app.c's app_process() gates its refresh on s_app_mode; since HB_ONLY is
 * now a distinct app_mode_t value that gate already covers it once app.c
 * is updated (see app.c's changes) -- no separate refresh needed here. */

void sx_sleep_manager_hb_only_start(sx_sleep_manager_t *mgr)
{
    log_info(TAG, "HB_ONLY: starting mini-wake (sensor check + heartbeat)");
    mgr->hb_only_phase          = 0;
    mgr->hb_only_elapsed_ms     = 0;
    mgr->hb_only_start_sent     = 0;
    mgr->hb_only_publish_kicked = 0;

    /* Accel resumed for the whole mini-wake (both phases) per the user's
     * explicit choice (2026-08-10): "Accel bật xuyên suốt cả 2 giai
     * đoạn ... để motionState luôn đúng". accel_app_wake_step_start()/
     * is_done() already match sx_sleep_step_t's signature (see
     * accel_app.h) but are called directly here rather than through a
     * sx_sleep_step_t array, same reasoning as the rest of this file. */
    accel_app_wake_step_start(mgr->accel_app);

    /* Phase 1: sensor check. ZE12A never loses power during sleep (only
     * its UART mode changes between Active Upload and Q&A, see
     * gas_sensor_switch_to_qa_mode()'s doc-comment) -- switching back to
     * Active Upload here just makes it start broadcasting readings again
     * so gas_sensor_app_poll() below can refresh gas_sensor[i].isConnected
     * before HB_ONLY_ZE12A_ACTIVE_MS's dwell-time budget runs out. */
    gas_sensor_switch_to_active_mode();
}

/* Phase 1: keep polling the gas sensor driver so gas_sensor[i].isConnected
 * gets refreshed as frames arrive, for HB_ONLY_ZE12A_ACTIVE_MS -- long
 * enough for the mux to dwell on every channel at least once (see the
 * macro's doc-comment in sx_sleep_manager.h). Returns 1 once that budget
 * has elapsed (not once every channel is confirmed connected -- a
 * genuinely disconnected channel would never confirm, so waiting on that
 * would hang this phase forever; the point of this phase is to give
 * connected channels a fair chance to refresh, not to enforce that all of
 * them must be connected). */
static uint8_t _hb_only_sensor_check_process(sx_sleep_manager_t *mgr, uint32_t delta_ms)
{
    gas_sensor_app_poll(delta_ms);
    accel_app_poll(mgr->accel_app, delta_ms);

    mgr->hb_only_elapsed_ms += delta_ms;
    if (mgr->hb_only_elapsed_ms >= HB_ONLY_ZE12A_ACTIVE_MS) {
        /* Back to Q&A mode immediately, per the user's explicit choice
         * (2026-08-10): "xác nhận cảm biến nào ok thì lập tức tắt luôn
         * cảm biến đấy" -- i.e. don't leave ZE12A broadcasting in Active
         * mode any longer than this phase needs. */
        gas_sensor_switch_to_qa_mode();
        log_info(TAG, "HB_ONLY: sensor check done, entering publish phase");
        return 1;
    }
    return 0;
}

/* Phase 2: publish. Mirrors _modem_power_on_start()/_modem_wait_ready_
 * is_done() above almost exactly (same UART-resume-before-power-on-start
 * ordering bug this file's wake steps already had to get right once --
 * see _modem_power_on_start()'s doc-comment) but scoped to mgr->hb_only_*
 * fields instead of mgr->sim_*, since this can run interleaved with (in
 * practice: between, never concurrently with) an ordinary wake sequence's
 * own sim_wait_elapsed_ms/sim_start_sent usage. */
static uint8_t _hb_only_publish_process(sx_sleep_manager_t *mgr, uint32_t delta_ms)
{
    if (mgr->hb_only_elapsed_ms == 0) {
        /* First tick of this phase -- kick off the modem, same sequence
         * as _modem_power_on_start(). */
        log_info(TAG, "HB_ONLY: resume UART + power on modem");
        sx_board_uart_resume_it();
        mgr->modem->ops->comm_reset(mgr->modem->ctx);
        mgr->modem->ops->power_on_start(mgr->modem->ctx);
    }
    mgr->hb_only_elapsed_ms += delta_ms;

    /* BUG FIX (2026-08-11), confirmed on real hardware: this used to call
     * modem_handle_poll(mgr->modem, delta_ms) manually here, on the
     * (wrong) assumption that this function needed to drive the modem's
     * state machine itself, the same way _modem_power_off_is_done() above
     * has to. That assumption doesn't hold here: unlike
     * _modem_power_off_is_done() (which runs inside sx_sleep_service.c's
     * BLOCKING _run_steps_blocking() loop, where app_process() -- and
     * therefore sx_user_mqtt_poll() -> sx_mqtt_poll() ->
     * modem_handle_poll() -- never runs again until that whole loop
     * returns), this function runs directly from app_process() every
     * single tick, which ALREADY calls sx_user_mqtt_poll(delta_ms)
     * unconditionally (see app.c, runs regardless of s_app_mode) ->
     * sx_mqtt_poll() -> modem_handle_poll(mqtt->modem, ts) on its own.
     * The extra manual call here ticked the modem's state machine TWICE
     * per app tick, at effectively double real-time speed -- confirmed by
     * the actual failure mode on hardware: endless "TIMEOUT response:
     * [NULL]" spam right after "Power On start", the AT command sequence
     * racing far ahead of the UART's real response latency. Removed;
     * sx_user_mqtt_poll()'s own single call is sufficient. */

    if (!mgr->hb_only_start_sent && !mgr->modem->ops->power_is_busy(mgr->modem->ctx)) {
        mgr->modem->ops->start(mgr->modem->ctx);
        mgr->hb_only_start_sent = 1;
    }

    if (!mgr->hb_only_start_sent) {
        /* Still waiting for power_on_start()'s async sequence to settle
         * before start() can be sent -- same 90s ceiling as
         * _modem_wait_ready_is_done() uses for consistency, though in
         * practice this branch clears in well under a second. */
        if (mgr->hb_only_elapsed_ms >= SX_SLEEP_MGR_SIM_WAIT_TIMEOUT_MS) {
            log_warn(TAG, "HB_ONLY: modem power-on timeout, giving up this cycle");
            return 1;
        }
        return 0;
    }

    if (!mgr->modem->ops->is_ready(mgr->modem->ctx) ||
        !sx_user_mqtt_is_connected()) {
        if (mgr->hb_only_elapsed_ms >= SX_SLEEP_MGR_SIM_WAIT_TIMEOUT_MS) {
            log_warn(TAG, "HB_ONLY: modem/MQTT not ready after timeout, giving up this cycle");
            return 1;
        }
        return 0;
    }

    if (!mgr->hb_only_publish_kicked) {
        log_info(TAG, "HB_ONLY: modem ready, publishing heartbeat");
        if (mgr->publish_heartbeat != NULL) {
            mgr->publish_heartbeat();
        }
        mgr->hb_only_publish_kicked = 1;
        return 0; /* give sx_user_mqtt_is_publishing() a tick to turn on */
    }

    if (sx_user_mqtt_is_publishing()) {
        return 0; /* still sending -- keep waiting */
    }

    /* Publish finished (or was skipped because MQTT wasn't connected --
     * either way there is nothing left to wait for). Power the modem back
     * down before returning to sleep, same as _modem_power_off_start()/
     * _modem_power_off_is_done() above, reusing that exact pair instead
     * of re-deriving the same PWRKEY-pulse + poll-driving logic here. */
    log_info(TAG, "HB_ONLY: publish done, powering modem back off");
    _modem_power_off_start(mgr);
    while (!_modem_power_off_is_done(mgr)) {
        sx_delay_ms(10);
    }
    return 1;
}

void sx_sleep_manager_hb_only_process(sx_sleep_manager_t *mgr, uint32_t delta_ms)
{
    uint8_t phase_done;

    if (mgr->hb_only_phase == 0) {
        phase_done = _hb_only_sensor_check_process(mgr, delta_ms);
        if (phase_done) {
            mgr->hb_only_phase      = 1;
            mgr->hb_only_elapsed_ms = 0; /* re-used as phase 2's own elapsed counter */
        }
    } else if (mgr->hb_only_phase == 1) {
        phase_done = _hb_only_publish_process(mgr, delta_ms);
        if (phase_done) {
            accel_app_sleep_step_start(mgr->accel_app);
            mgr->hb_only_phase = 2;
            log_info(TAG, "HB_ONLY: mini-wake complete");
        }
    }
}

uint8_t sx_sleep_manager_hb_only_is_done(sx_sleep_manager_t *mgr)
{
    return mgr->hb_only_phase == 2;
}

uint8_t sx_sleep_manager_hb_only_modem_owned(sx_sleep_manager_t *mgr)
{
    /* Only phase 1 (publish) actually touches the modem -- phase 0
     * (sensor check) never powers it on, so the recovery ladder is free
     * to act during phase 0 same as any other idle period. */
    return mgr->hb_only_phase == 1;
}

void sx_sleep_manager_bare_sleep(sx_sleep_manager_t *mgr, uint32_t sleep_sec)
{
    if (sleep_sec == 0) sleep_sec = 1;

    log_info(TAG, "Bare STOP: setting RTC wakeup = %lu sec (no sleep_steps -- "
                   "already parked from this lap's first chunk or the last "
                   "HB_ONLY publish)", (unsigned long)sleep_sec);
    sx_sleep_set_rtc_wake(mgr->svc.sleep, sleep_sec);

    log_info(TAG, ">>> Entering STOP mode NOW (bare)");
    sx_delay_ms(10);

    sx_sleep_enter_stop(mgr->svc.sleep);

    log_info(TAG, "<<< Woke from STOP mode (bare)");
    sx_sleep_cancel_rtc(mgr->svc.sleep);
}