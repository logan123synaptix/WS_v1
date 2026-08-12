#ifndef APP_SX_SLEEP_MANAGER_H
#define APP_SX_SLEEP_MANAGER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "sx_sleep_service.h"
#include "modem_ops.h"
#include "gps.h"
#include "sht3x.h"
#include "sx_gpio.h"
#include "sx_pwm_sw.h"
#include "sps30_app.h"
#include "accel_app.h"
#include "sx_ex_storage.h"
#include "ze12a.h"

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
    /* Pump PWM struct (board.sx_pwm_sw), already pump_init()'d by the
     * caller (mirrors power_gpio in sps30_app_t) — this module only
     * drives it to 0% via pump_off() in its own sleep-step wrapper, since
     * pump_off()'s signature doesn't match sx_sleep_step_t directly. Was
     * a bare sx_gpio_t* before sx_pump.h switched to software-PWM
     * (pump_on()/pump_off() now take sx_pwm_software_t*, not the raw
     * GPIO — see sx_board.c's sx_board_get_pump_pwm()). */
    sx_pwm_software_t *pump_pwm;
    /* BNO055 app-layer accel/movement reader — sleep_steps/wake_steps call
     * its own accel_app_sleep_step_*()/accel_app_wake_step_*() pairs
     * directly (already match sx_sleep_step_t's signature, see
     * accel_app.h), this pointer is only kept so sx_sleep_manager_init()
     * can pass it as those steps' ctx. */
    accel_app_t    *accel_app;
    /* No ext_flash pointer needed here (2026-07-30 revision): W25Q128
     * power-down/up is driven through sx_storage_sleep()/sx_storage_wake()
     * (sx_ex_storage.h), which internally reach the correctly-initialized
     * static W25Q128 instance owned by that module. Passing a separate
     * board.q128 pointer here was the original bug -- see sx_ex_storage.c's
     * doc-comment on sx_storage_sleep()/wake() for the full story. */
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

    /* sx_gettick() snapshot from _modem_power_off_start(), used by
     * _modem_power_off_is_done() to compute its own tick delta and drive
     * modem_handle_poll() manually. Needed because the modem_power_off
     * sleep_step runs inside sx_sleep_service.c's blocking
     * _run_steps_blocking() loop, which does not call test_sleep_poll()
     * (and therefore never calls modem_handle_poll()) between is_done()
     * checks -- see that function's doc-comment for the full story. */
    uint32_t power_off_last_tick_ms;

    /* ===== HB_ONLY mini-wake state (see the HB_ONLY block below) ===== */

    /* Set once via sx_sleep_manager_init()'s new parameter -- tier 3
     * (this module) is not allowed to call app.c's static
     * send_heartbeat_if_due()/build_heartbeat_payload() directly (that
     * would reach across the same tier-2/tier-3 boundary the doc-comment
     * on sx_sleep_manager_wake_process() above already protects), so
     * app.c hands in a function pointer instead -- same pattern
     * sx_sleep_service_init()'s pre_stop_refresh callback already uses
     * one layer down. Called once, synchronously, at the point phase 2
     * has confirmed the modem is ready and MQTT is connected; must not
     * block internally (build+publish is fire-and-forget, same as
     * send_heartbeat_if_due()'s own existing behavior). */
    void (*publish_heartbeat)(void);

    uint8_t  hb_only_phase;          /* 0=sensor check, 1=publish, 2=done */
    uint32_t hb_only_elapsed_ms;     /* elapsed time within the current phase */
    uint8_t  hb_only_start_sent;     /* mirrors sim_start_sent, scoped to HB_ONLY's own modem_power_on/wait_ready cycle */
    uint8_t  hb_only_publish_kicked; /* publish_heartbeat() called exactly once per HB_ONLY cycle */

    /* Publish retry tracking (2026-08-11) -- see HB_ONLY_PUBLISH_MAX_
     * ATTEMPTS/HB_ONLY_PUBLISH_RETRY_GAP_MS and _hb_only_publish_process()'s
     * doc-comment for why a single publish_heartbeat() call is not always
     * enough (real-hardware race with mqtt_rpc_init()'s post-connect
     * subscribe still in flight at the driver layer). */
    uint8_t  hb_only_publish_attempts;
    uint32_t hb_only_last_publish_attempt_ms;

    /* Modem power-on cooldown gate (2026-08-12) -- see
     * HB_ONLY_MODEM_COOLDOWN_MS's doc-comment below. Tracks elapsed time
     * within phase 2 SEPARATELY from hb_only_elapsed_ms (which phase 1
     * already owns and resets to 0 when phase 2 starts -- reusing it here
     * would conflate "time since publish phase began" with "time since
     * modem was last powered off", two different clocks). Set once when
     * phase 2 starts (mirrors hb_only_start_sent's "kick off once" shape);
     * power_on_start() is not called until this many ms have elapsed. */
    uint8_t  hb_only_cooldown_done;
    uint32_t hb_only_cooldown_elapsed_ms;

    /* Gas sensor connection-status snapshot (2026-08-12, generalized
     * 2026-08-12 to cover full-wake too -- see doc-comment on
     * sx_sleep_manager_gas_snapshot_capture() below for the full
     * root-cause writeup and why this is no longer HB_ONLY-only).
     *
     * NOT indexed by GasSensorType_t's raw enum value directly -- those
     * values (0x03..0x2C per ze12a.h, non-contiguous) are too sparse for
     * a direct-index array. Indexed 0..GAS_SENSOR_COUNT-1 instead,
     * parallel to gas_sensor[] (ze12a.h) itself: slot i here corresponds
     * to gas_sensor[i], same order, same GAS_SENSOR_COUNT. Snapshotting
     * captures both the type and the flag together so app.c doesn't need
     * its own copy of ze12a.h's internal ordering to read this back --
     * see gas_snapshot_type[]. Field names kept without an "hb_only_"
     * prefix now that both wake paths share this same snapshot. */
    GasSensorType_t gas_snapshot_type[GAS_SENSOR_COUNT];
    bool             gas_snapshot_connected[GAS_SENSOR_COUNT];
} sx_sleep_manager_t;

/* wake_steps run, in order, on every wake:
 *   1. W25Q128 wake — sx_storage_wake() releases SPI Deep Power-Down on
 *      the real, correctly-initialized flash instance owned by
 *      sx_ex_storage.c. Placed first: independent SPI bus, no dependency
 *      on I2C1/UART being resumed yet, so it's safe to bring back first.
 *   2. GPS on           — power on GPS, resume its UART
 *   3. Wait for GPS fix — poll until lat/long non-zero or GPS_TIMEOUT_MS
 *   4. Resume LTE UART + power on modem
 *   5. Wait for modem ready — send start() once power_on_start() settles,
 *      poll is_ready(), hard-reset and retry once after 90s with no reply
 *   6. ZE12A back to Active Upload mode (gas_sensor_switch_to_active_mode())
 *      — cheap fire-and-forget UART command, always reports done.
 *   7. BNO055 resume — PWR_MODE=NORMAL then re-select NDOF operation mode
 *      (accel_app_wake_step_start/is_done). Needed because leaving suspend
 *      only restores power per the datasheet; the fusion operation mode
 *      does not resume on its own (see accel_app.h).
 *
 * sleep_steps run, in order, before every sx_sleep_manager_enter_sleep():
 *   1. W25Q128 sleep — sx_storage_sleep() enters SPI Deep Power-Down on
 *      the real flash instance (via sx_ex_storage.c, which internally
 *      guards against sending Power-Down while the chip is still BUSY
 *      from a prior write, e.g. network_config.c's config-save — see
 *      sx_W25Q128_sleep_on()'s doc-comment). Placed first: closes off the
 *      one power rail that had no sleep_step at all before (2026-07-30).
 *   2. Power down GPS (zero out last fix so a stale fix isn't reused)
 *   3. Power down modem
 *   4. SPS30 power-down (sps30_app_sleep_step_start/is_done — SHDLC
 *      sleep() then EN_PW_DUST low). Caller must only trigger sleep once
 *      any in-progress SPS30 measurement cycle is DONE/IDLE (see
 *      sps30_app.h) — not enforced here, this module just runs the step.
 *   5. Pump off (0% duty via pump_off(), see sx_pwm_sw.h)
 *   6. ZE12A to Question & Answer mode (gas_sensor_switch_to_qa_mode())
 *      — reduces UART/processing load; does NOT reduce the electrochemical
 *      cell's own power draw (no such command exists per the datasheet).
 *   7. BNO055 to Suspend mode (accel_app_sleep_step_start/is_done) — a
 *      real power-down per the datasheet's Suspend Mode section (all
 *      sensors + the chip's internal MCU sleep), unlike ZE12A's QA mode.
 *
 * SHT3x and ADS1115 have no sleep_step here: both only ever run in
 * single-shot mode (no continuous conversion to stop), and I2C1 loses
 * clock in STOP mode regardless — see the sps30_app/sx_temp_humi handoff
 * notes for the datasheet/driver reasoning behind this. BNO055 is on the
 * same I2C1 bus but, unlike SHT3x/ADS1115, has a real suspend state worth
 * explicitly entering/leaving (see accel_app.h), hence its own step pair. */
void sx_sleep_manager_init(sx_sleep_manager_t *mgr,
                            sx_sleep_t         *sleep,
                            modem_handle_t     *modem,
                            sx_gps_t           *gps,
                            sps30_app_t        *sps30_app,
                            sx_pwm_software_t  *pump_pwm,
                            accel_app_t        *accel_app,
                            void (*publish_heartbeat)(void));

/* Runs the sleep_steps then enters STOP mode via tier 2/1. Blocking, same
 * as the old sx_sleep_manager_enter() — returns only after waking. */
void sx_sleep_manager_enter_sleep(sx_sleep_manager_t *mgr, uint32_t sleep_sec);

/* Drives the wake_steps sequence — call once per app tick. */
void sx_sleep_manager_wake_process(sx_sleep_manager_t *mgr, uint32_t delta_ms);

uint8_t sx_sleep_manager_is_wake_done(sx_sleep_manager_t *mgr);

/* True while a wake sequence is actively running (from
 * sx_sleep_manager_wake_process()'s first tick after waking, until
 * sx_sleep_manager_reset_wake() is called once is_wake_done() is true).
 *
 * Added (2026-07-29) to fix a real hardware bug: sx_mqtt.c's reconnect/
 * recovery-ladder logic independently calls modem->ops->start() whenever
 * it sees the MQTT link down — which is exactly the case right after
 * waking, since the modem was just powered off/on. That collided with
 * this module's own _modem_wait_ready_is_done() also calling start()
 * once power_is_busy() clears, causing two start() calls in quick
 * succession during the same wake. Confirmed on real board: log showed
 * "Starting network attach sequence" immediately followed by
 * "start(): modem busy", then "+CME ERROR: SIM failure" — the modem's
 * own internal SIM detection got interrupted by the second AT command
 * landing mid-sequence, forcing multiple minutes of retry/power-cycle
 * before recovering. sx_mqtt.c now checks this function and skips its
 * own start() calls while a wake sequence owns the modem. */
uint8_t sx_sleep_manager_is_waking(sx_sleep_manager_t *mgr);

void sx_sleep_manager_reset_wake(sx_sleep_manager_t *mgr);

/* ===================== HB_ONLY mini-wake ===================== *
 *
 * Used when heartbeat_ms < sleep_ms: instead of one sx_sleep_manager_
 * enter_sleep(sleep_sec) blocking call per lap, app.c's APP_MODE_ENTER_
 * SLEEP branch splits sleep_ms into chunks of heartbeat_ms and calls
 * sx_sleep_manager_enter_sleep() once per chunk. After every chunk except
 * the one that completes the full sleep_ms, app.c drives this state
 * machine once (APP_MODE_HB_ONLY) before sleeping again, instead of
 * running the full 7-step wake_steps sequence:
 *
 *   Phase 1 (sensor check) — gas_sensor_switch_to_active_mode(), poll
 *   gas_sensor_app_poll() for HB_ONLY_ZE12A_ACTIVE_MS (long enough for
 *   the mux to dwell on all GAS_SENSOR_MUX_CHANNEL_COUNT channels at
 *   GAS_SENSOR_CHANNEL_DWELL_MS each, refreshing gas_sensor[i].isConnected
 *   before it can age out per GAS_SENSOR_TIMEOUT_MS), then immediately
 *   gas_sensor_switch_to_qa_mode() again. accel_app_wake_step_start() is
 *   kicked off at the very start of this phase and stays resumed through
 *   phase 2 as well, so motionState in the heartbeat payload reflects
 *   this mini-wake's own accel reading (2026-08-10, per user request:
 *   "Accel bật xuyên suốt cả 2 giai đoạn").
 *
 *   Phase 2 (publish) — resume UART, power on modem (mirrors
 *   _modem_power_on_start()/_modem_wait_ready_is_done() above), wait for
 *   modem ready + MQTT connected, call app.c's heartbeat publish path,
 *   wait for the publish to finish, power the modem back off, suspend
 *   accel, done.
 *
 * GPS, SPS30, and the pump are untouched throughout — they were already
 * parked by the full sleep_steps run at the start of this lap (see
 * app.c's s_full_sleep_steps_done_this_lap) and stay parked until the
 * lap's final chunk triggers the ordinary full wake_steps sequence. */

#define HB_ONLY_ZE12A_ACTIVE_MS  9500U  /* < GAS_SENSOR_TIMEOUT_MS (10000),
                                          * >= GAS_SENSOR_MUX_CHANNEL_COUNT *
                                          * GAS_SENSOR_CHANNEL_DWELL_MS
                                          * (4 * 2000 = 8000) so every
                                          * channel gets at least one full
                                          * dwell window. */

/* Modem power-off -> next-power-on cooldown gate (2026-08-12).
 *
 * SUSPECTED root cause (not yet confirmed on hardware, needs real-board
 * verification -- see below) of the "HB_ONLY: TIMEOUT response: [NULL]"
 * hang reported on real hardware: the ordinary full wake_steps sequence
 * (s_wake_steps[] above) runs ext_flash_wake + gps_on + gps_wait_fix
 * (bounded by network_config_get()->gps_timeout_ms, typically tens of
 * seconds or more) BEFORE _modem_power_on_start() ever pulses PWRKEY --
 * i.e. there's always a long, variable gap between the modem's previous
 * power_off_start() completing and the next power_on_start() pulse.
 * HB_ONLY's phase 1 (sensor check) only waits HB_ONLY_ZE12A_ACTIVE_MS
 * (9500ms, fixed) before phase 2 pulses PWRKEY again -- a much shorter,
 * fixed gap. If the A7677S needs a real hardware cooldown longer than
 * 9.5s after a power-off before it will respond to the next PWRKEY pulse
 * (power-path capacitor discharge / internal reset settling -- not
 * documented in a76xx_at_cmd.md, would need to be measured), that would
 * explain why telemetry wake (long gap, works) and heartbeat wake (short
 * gap, times out with no response at all -- not even a malformed one)
 * differ despit calling power_on_start() the exact same way.
 *
 * Value chosen conservatively (matches A7677S_OFF_SETTLE_MS's own 4500ms
 * plus generous margin, since the true minimum is unmeasured) -- ADJUST
 * once real-hardware testing confirms the actual minimum cooldown needed,
 * or confirms this hypothesis is wrong entirely (in which case look
 * elsewhere: this alone will not fix the bug if the real cause is
 * something else, e.g. a separate main-power rail this driver doesn't
 * control -- see chat history 2026-08-11/12 for the full elimination
 * process already carried out in code review).
 *
 * Deliberately a SEPARATE clock from hb_only_elapsed_ms (owned by phase 1,
 * reset to 0 when phase 2 begins) and from HB_ONLY_ZE12A_ACTIVE_MS (a
 * different concern -- gas sensor mux dwell time, not modem power
 * timing) -- conflating the two would silently change ZE12A's dwell
 * budget every time this constant is tuned, or vice versa. */
#define HB_ONLY_MODEM_COOLDOWN_MS  15000U

void sx_sleep_manager_hb_only_start(sx_sleep_manager_t *mgr);

/* Call once per tick while in APP_MODE_HB_ONLY. Internally drives both
 * phases; app.c does not need to know which phase is active. Calls back
 * into app.c to actually build+publish the heartbeat payload at the
 * right moment (see sx_sleep_manager.c for the callback wiring — kept as
 * a function pointer set once at sx_sleep_manager_init() time, same
 * pattern as pre_stop_refresh in sx_sleep_service_init()). */
void sx_sleep_manager_hb_only_process(sx_sleep_manager_t *mgr, uint32_t delta_ms);

uint8_t sx_sleep_manager_hb_only_is_done(sx_sleep_manager_t *mgr);

/* True while phase 2 (modem on, about to or currently publishing) is
 * active -- app.c's is_modem_owned_by_sleep_manager() wrapper needs to
 * OR this in, same reasoning as sx_sleep_manager_is_waking() already
 * documented above: sx_mqtt.c's recovery ladder must not call modem
 * start() while this state machine also owns the modem. False during
 * phase 1 (sensor check) and once phase 2's publish+power-off has
 * finished. */
uint8_t sx_sleep_manager_hb_only_modem_owned(sx_sleep_manager_t *mgr);

/* BUG (real hardware, reported by user, 2026-08-12): heartbeat's
 * sensorStatus[] for CO/SO2/NO2/O3/H2S was reporting FAIL even for gas
 * types with real, fresh readings confirmed in the same lap's telemetry
 * payload and ze12a.c's own per-frame log line. Root cause: app.c's
 * build_heartbeat_payload() calls gas_sensor_app_is_connected(type),
 * which reads gas_sensor[i].isConnected LIVE off ze12a.c's own
 * GAS_SENSOR_TIMEOUT_MS (10000ms) countdown. Originally fixed for
 * HB_ONLY only (phase 1 ends, then phase 2's modem cooldown/handshake/
 * connect alone easily exceeds 10000ms before build_heartbeat_payload()
 * runs, aging every channel's isConnected back out to false regardless
 * of real sensor health). BUT real logs (2026-08-12 session) showed the
 * exact same symptom on the ordinary FULL-WAKE path too -- e.g. a
 * data-topic payload with so2:50/no2:16/o3:14 immediately followed
 * (0.3s later) by that lap's heartbeat correctly showing OK, while a
 * different lap's heartbeat (further from its last valid gas frame,
 * e.g. after a long GPS-fix wait during SENSING) showed FAIL for every
 * channel despite ZE12A never having lost power or gone silent -- same
 * age-out race, just against APP_CYCLE_SENSING -> APP_CYCLE_SENDING's
 * timing instead of HB_ONLY phase 1 -> phase 2's. GAS_SENSOR_TIMEOUT_MS
 * was never actually safe to assume "not at risk" on the full-wake path
 * either, since sensing_ms + any slow step before SENDING (GPS fix wait
 * in particular, observed up to 110000ms in a real log) can just as
 * easily run the same clock out.
 *
 * Fix: generalized the HB_ONLY-only snapshot into one used by BOTH
 * paths. sx_sleep_manager_gas_snapshot_capture() below must be called
 * once, right when sensor-checking ends and before any subsequent step
 * risks running GAS_SENSOR_TIMEOUT_MS out -- HB_ONLY's phase 1 already
 * does this (sx_sleep_manager_hb_only_process()'s internal sensor-check
 * step); app.c's app_cycle_process() now does the same at the
 * APP_CYCLE_SENSING -> APP_CYCLE_SENDING transition, before SENDING's
 * build_heartbeat_payload()/build_telemetry_payload() calls. Both wake
 * paths then read back through the single getter below instead of
 * calling gas_sensor_app_is_connected() live. */
void sx_sleep_manager_gas_snapshot_capture(sx_sleep_manager_t *mgr);

/* Read back the snapshot captured by sx_sleep_manager_gas_snapshot_
 * capture() for one gas type (see that function's doc-comment for why
 * this exists instead of app.c calling gas_sensor_app_is_connected()
 * live at heartbeat-build time). Returns false for any type not found
 * in the snapshot (should not happen in practice -- the snapshot always
 * covers exactly ze12a.h's gas_sensor[GAS_SENSOR_COUNT], the same set
 * app.c's gas_status_channels[] iterates). Only meaningful once capture()
 * has run at least once this cycle; before that it reads back whatever
 * the previous cycle left behind (harmless in practice -- both call
 * sites always run capture() earlier in the same cycle before this is
 * ever read). */
bool sx_sleep_manager_gas_snapshot_connected(sx_sleep_manager_t *mgr, GasSensorType_t type);

/* Sleeps for sleep_sec WITHOUT running the 7 sleep_steps first -- used for
 * every chunk in a lap after the first (GPS/modem/SPS30/pump/ZE12A/accel
 * are already parked from that first chunk's full
 * sx_sleep_manager_enter_sleep() call, or from the most recent HB_ONLY
 * mini-wake's own power-down at the end of its publish phase). Calls
 * tier 1 directly (sx_sleep_set_rtc_wake() + sx_sleep_enter_stop() +
 * sx_sleep_cancel_rtc()), bypassing tier 2's _run_steps_blocking() for
 * the (empty, in this case) sleep_steps loop -- mirrors exactly what
 * sx_sleep_service_enter_sleep() does internally minus that loop and
 * minus its own pre_stop_refresh callback dispatch (the caller, app.c,
 * already refreshes IWDG itself right before calling this, same as it
 * does before sx_sleep_manager_enter_sleep()). Blocking, same as
 * sx_sleep_manager_enter_sleep() -- does not return until the RTC wakeup
 * timer fires. */
void sx_sleep_manager_bare_sleep(sx_sleep_manager_t *mgr, uint32_t sleep_sec);

static inline wake_reason_t sx_sleep_manager_get_wake_reason(sx_sleep_manager_t *mgr)
{
    return sx_sleep_service_get_wake_reason(&mgr->svc);
}

#ifdef __cplusplus
}
#endif

#endif