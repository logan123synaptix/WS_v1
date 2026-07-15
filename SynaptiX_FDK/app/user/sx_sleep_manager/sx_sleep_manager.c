#include "sx_sleep_manager.h"
#include "ze12a.h"
#include "app_config.h"
#include "sx_board.h"
#include "sx_delay.h"
#include "sx_pump.h"
#include "logger.h"

static const char *TAG = "SX_SLEEP_MGR";

/* ===================== wake steps ===================== */

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

/* Step 2: wait for GPS fix, up to GPS_TIMEOUT_MS. This step owns its own
 * timeout tracking (mgr->gps_wait_elapsed_ms) rather than relying on
 * sx_sleep_service's shared step_timeout_ms — GPS is deliberately given a
 * much longer allowance (130s) than the modem wake step (90s), and the
 * two must stay independent: a timeout here still counts as "done" (this
 * step gives up and moves on with an unfixed GPS position, i.e. lat/long
 * still 0.0f), it does not abort the wake sequence. */
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
    if (mgr->gps_wait_elapsed_ms >= GPS_TIMEOUT_MS) {
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

/* ===================== sleep steps ===================== */

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
    mgr->modem->ops->power_off_start(mgr->modem->ctx);
    sx_delay_ms(500);
}

static uint8_t _modem_power_off_is_done(void *ctx)
{
    (void)ctx;
    return 1;
}

/* Step 3: SPS30 power-down — sps30_app.h's start()/is_done() pair already
 * matches sx_sleep_step_t's signature exactly, wired in directly below
 * with mgr->sps30_app as ctx. No wrapper needed for this one. */

/* Step 4: pump off. pump_off()'s signature (sx_gpio_t*) doesn't match
 * sx_sleep_step_t (void (*)(void*)), so this thin wrapper adapts it —
 * same reasoning as the ZE12A wrapper above/below. */
static void _pump_off_start(void *ctx)
{
    sx_sleep_manager_t *mgr = (sx_sleep_manager_t *)ctx;
    pump_off(mgr->pump_gpio);
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

/* ===================== step tables ===================== */

/* NOTE on elapsed-time bookkeeping: sx_sleep_service's generic
 * step_elapsed_ms (passed to no one here) is intentionally NOT used for
 * the GPS-wait/SIM-wait steps' own timeouts — see the comments above.
 * Instead, this module ticks mgr->gps_wait_elapsed_ms/sim_wait_elapsed_ms
 * itself, in sx_sleep_manager_wake_process() below, before delegating to
 * sx_sleep_service_wake_process(). */
static sx_sleep_step_t s_wake_steps[5];
static sx_sleep_step_t s_sleep_steps[5];

void sx_sleep_manager_init(sx_sleep_manager_t *mgr,
                            sx_sleep_t         *sleep,
                            modem_handle_t     *modem,
                            sx_gps_t           *gps,
                            sps30_app_t        *sps30_app,
                            sx_gpio_t          *pump_gpio)
{
    mgr->modem     = modem;
    mgr->gps       = gps;
    mgr->sps30_app = sps30_app;
    mgr->pump_gpio = pump_gpio;
    mgr->gps_wait_elapsed_ms = 0;
    mgr->sim_wait_elapsed_ms = 0;
    mgr->sim_start_sent      = 0;

    s_wake_steps[0] = (sx_sleep_step_t){ .start = _gps_on_start,          .is_done = _gps_on_is_done,          .ctx = mgr, .name = "gps_on" };
    s_wake_steps[1] = (sx_sleep_step_t){ .start = _gps_wait_start,        .is_done = _gps_wait_is_done,        .ctx = mgr, .name = "gps_wait_fix" };
    s_wake_steps[2] = (sx_sleep_step_t){ .start = _modem_power_on_start,  .is_done = _modem_power_on_is_done,  .ctx = mgr, .name = "modem_power_on" };
    s_wake_steps[3] = (sx_sleep_step_t){ .start = _modem_wait_ready_start,.is_done = _modem_wait_ready_is_done,.ctx = mgr, .name = "modem_wait_ready" };
    s_wake_steps[4] = (sx_sleep_step_t){ .start = _gas_sensor_active_mode_start, .is_done = _gas_sensor_active_mode_is_done, .ctx = mgr, .name = "gas_sensor_active_mode" };

    s_sleep_steps[0] = (sx_sleep_step_t){ .start = _gps_power_off_start,   .is_done = _gps_power_off_is_done,   .ctx = mgr, .name = "gps_power_off" };
    s_sleep_steps[1] = (sx_sleep_step_t){ .start = _modem_power_off_start, .is_done = _modem_power_off_is_done, .ctx = mgr, .name = "modem_power_off" };
    s_sleep_steps[2] = (sx_sleep_step_t){ .start = sps30_app_sleep_step_start, .is_done = sps30_app_sleep_step_is_done, .ctx = mgr->sps30_app, .name = "sps30_power_off" };
    s_sleep_steps[3] = (sx_sleep_step_t){ .start = _pump_off_start,        .is_done = _pump_off_is_done,        .ctx = mgr, .name = "pump_off" };
    s_sleep_steps[4] = (sx_sleep_step_t){ .start = _gas_sensor_qa_mode_start, .is_done = _gas_sensor_qa_mode_is_done, .ctx = mgr, .name = "gas_sensor_qa_mode" };

    /* step_timeout_ms = 0 (no shared timeout) passed to sx_sleep_service:
     * every step here manages its own completion criteria (gps_on/
     * modem_power_on are one-shot kick-offs that report done immediately;
     * gps_wait/modem_wait_ready track their own elapsed time and decide
     * for themselves when to give up — see comments above). A shared
     * generic timeout would either cut GPS's 130s allowance short or let
     * the modem step run needlessly long; keeping it per-step avoids
     * that mismatch entirely. */
    sx_sleep_service_init(&mgr->svc, sleep,
                           s_wake_steps, 5,
                           s_sleep_steps, 5,
                           0);

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

void sx_sleep_manager_reset_wake(sx_sleep_manager_t *mgr)
{
    mgr->gps_wait_elapsed_ms = 0;
    mgr->sim_wait_elapsed_ms = 0;
    mgr->sim_start_sent      = 0;
    sx_sleep_service_reset_wake(&mgr->svc);
}