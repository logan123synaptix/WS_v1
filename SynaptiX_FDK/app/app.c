#include "app.h"
#include <math.h>
#include "app_config.h"
#include "logger.h"
#include "sx_delay.h"
#include "sx_board.h"
#include "sps30_app.h"
#include "sx_temp_humi.h"
#include "sx_sleep_manager.h"
#include "gas_sensor_app.h"
#include "accel_app.h"
#include "power_monitor_app.h"
#include "sx_pump.h"
#include "thingsboard_client.h"
#include "sx_user_mqtt.h"
#include "network_config.h"
#include "sx_ex_storage.h"
#include "shell_app.h"
#include "time_sync.h"
#include "mqtt_rpc.h"
#include "ze12a.h"
#include "gps.h"
#include "cJSON.h"
#include "iwdg.h"
#include <string.h>
#include <stdio.h>
#include <time.h>

static const char *TAG = "APP";

/* App-layer instances live here, not in Board_t (sx_board.h) — Board_t
 * only owns concrete driver/hardware handles (sht3x, sps30_uart, gps...);
 * these are the "how this app uses that hardware" layer, per discussion
 * with the user on where sps30_app_t/sx_temp_humi_t/sx_sleep_manager_t
 * (tier 3 of the sleep architecture) should live. */
static sps30_app_t         s_sps30_app;
static sx_temp_humi_t      s_temp_humi;
static accel_app_t         s_accel_app;
static power_monitor_app_t s_power_monitor;
static sx_sleep_manager_t  s_sleep_mgr;
static time_sync_t         s_time_sync;
/* No gas_sensor_app_t instance — gas_sensor_app.c has no state of its
 * own, see gas_sensor_app.h's doc-comment (all runtime state already
 * lives in ze12a.c's statics + the gas_sensor[] array it owns). */

/* ===================== main FULL_POWER cycle state machine =====================
 *
 * Adapted from WS_v0's SynaptiX/apps/app.c sensor_task (WEATHER_STATION_IDLE
 * -> ON_PUMP -> SENSING -> SENDING), per the user's request — reference read
 * in full, NOT ported verbatim. Deliberate differences from WS_v0, all per
 * discussion with the user:
 *
 *  1. WS_v0 ran this as a dedicated FreeRTOS task (`sensor_task`) plus a
 *     second independent task (`reading_sensor_task`) polling gas/temp-humi/
 *     gnss/accel every tick. WS_v1 is non-RTOS: this is instead one switch
 *     inside app_process(), called once per main-loop tick; the "always
 *     poll regardless of state" sensors (gas_sensor_app_poll/accel_app_poll,
 *     already wired below) keep running unconditionally the same way
 *     reading_sensor_task did, just from the same function instead of a
 *     second task.
 *
 *  2. WS_v0's pump used PWM with a configurable duty cycle
 *     (bsp_pwm_set_duty/app_setting.dutyCyclePercent). WS_v1's pump driver
 *     (sx_pump.h) now also drives the pump via software PWM (sx_pwm_sw.h,
 *     see sx_board.c's TIM1/sx_pwm_sw wiring). As of 2026-07-17,
 *     APP_CYCLE_ON_PUMP below drives it via pump_set_power() at
 *     network_config_t's pump_duty_percent (runtime-editable via the CLI's
 *     "-duty"/RPC's "-duty", same split as WS_v0's separate "-pump"/
 *     "-duty" flags) rather than pump_on()'s hardcoded 100% — pump_on()
 *     itself is no longer called anywhere in this file (or elsewhere in
 *     the app layer; sx_sleep_manager.c only calls pump_off()), though it
 *     remains defined in sx_pump.c/.h in case a future caller wants an
 *     explicit "full drive regardless of configured duty" primitive.
 *
 *  3. Timing (pump-on duration, sensing duration, sleep duration) used to
 *     be APP_PUMP_ON_MS/APP_SENSING_MS/APP_CYCLE_PERIOD_MS fixed #defines
 *     (app_config.h). As of 2026-07-16 these are runtime-editable via
 *     network_config_t's pump_on_ms/sensing_ms/sleep_ms fields (flash-
 *     persisted, same struct as the MQTT broker settings) and a USB CDC
 *     CLI (app/user/shell_app/ — "settings -c -pump/-sensing/-data ..."),
 *     closer in spirit to WS_v0's runtime-adjustable app_setting struct
 *     though there is no MQTT-RPC input channel yet, only the CLI. The
 *     app_config.h #defines now only seed the flash default on a fresh
 *     board — see network_config.h's build_defaults(). As of 2026-07-16
 *     a second input channel exists too — MQTT RPC (app/user/mqtt_rpc/,
 *     "setParams" method, same flag set/units as the CLI's "settings -c")
 *     — both end up at the same network_config_set_*()/_save() calls.
 *
 *  4. WS_v0's SENDING state falls back to saving telemetry JSON to SPIF/
 *     littlefs when the publish fails, for later retry. As of 2026-07-16
 *     WS_v1 does the same — see OFFLINE_QUEUE_DIR/offline_queue_save()/
 *     offline_queue_resend_one() below. One file per failed publish
 *     (OFFLINE_QUEUE_DIR/telemetry_<tick>.json), and one queued file is
 *     resent per SENDING pass when connected (same "one at a time" shape
 *     as WS_v0's push_last_telemetry_json(), not a batch-drain), oldest
 *     first by directory scan order.
 *
 *  5. sps30_app_t's cycle here is driven directly (start_cycle/poll/
 *     is_cycle_done/reset), matching its existing non-blocking contract in
 *     sps30_app.h — WS_v0 called the equivalent sps30_start()/sps30_app_poll()/
 *     sps30_stop() directly inline in its own SENSING state, same shape.
 *
 *  6. No sleep call is wired in yet at the end of SENDING. Per the user:
 *     build and test this cycle first (pump/sensing/sending timing, real
 *     telemetry content) before adding sx_sleep_manager_enter_sleep() /
 *     APP_MODE_ENTER_SLEEP transitions — that is deliberately left as a
 *     follow-up, not an oversight. app_mode stays APP_MODE_FULL_POWER for
 *     the entire cycle below; the other app_mode_t values (ENTER_SLEEP/
 *     SLEEP/WAKE_PUBLISH) are declared in app.h but not yet driven from
 *     here. app_request_sleep() is currently unused (the
 *     tud_umount_cb()-triggered call was removed from sx_board.c per the
 *     user — leftover logic from other code, not part of this project's
 *     intended sleep flow) — actually wiring up a real caller is part of
 *     the same follow-up. */

typedef enum {
    APP_CYCLE_ON_PUMP = 0,
    APP_CYCLE_SENSING,
    APP_CYCLE_SENDING,
    APP_CYCLE_WAIT_PUBLISH,
    APP_CYCLE_SLEEPING,
    APP_CYCLE_WAKING,
} app_cycle_state_t;

/* Starts in ON_PUMP rather than IDLE — per the user, the whole board
 * (STM32 + peripherals) actually sleeps between cycles now, so there is
 * no separate "wait for the next cycle" IDLE state anymore: SLEEPING
 * itself (via sx_sleep_manager_enter_sleep()'s RTC wakeup timer, using
 * APP_CYCLE_PERIOD_MS as the sleep duration) is what used to be IDLE's
 * job. On boot (first lap, before any sleep has happened yet) this just
 * starts the pump immediately rather than waiting out a "time until next
 * lap" window — acceptable since there is no prior telemetry to protect
 * a cadence around on the very first lap. */
static app_cycle_state_t s_cycle_state   = APP_CYCLE_ON_PUMP;
static uint32_t          s_cycle_tick_ms = 0;
static volatile app_mode_t s_app_mode    = APP_MODE_FULL_POWER;

/* Chunked-sleep state for the heartbeat_ms/sleep_ms split (2026-08-10) --
 * see the doc-comment above APP_MODE_ENTER_SLEEP's branch in app_process()
 * for the full design. Both reset to 0 whenever a lap's sleep_ms total is
 * reached (i.e. right before flipping to APP_MODE_WAKEUP), so a fresh lap
 * always starts its first chunk with the full 7 sleep_steps again. */
static uint32_t s_lap_slept_ms                   = 0;
static uint8_t  s_full_sleep_steps_done_this_lap = 0;

#define TELEMETRY_JSON_BUFF_SIZE 512

/* Bug fix (2026-07-31): APP_CYCLE_SENDING used to fall straight through
 * to APP_CYCLE_SLEEPING / APP_MODE_ENTER_SLEEP in the same tick as
 * calling sx_user_mqtt_publish(). sx_user_mqtt_publish() only enqueues
 * the AT command to the modem (sx_mqtt_publish() -> modem->ops->
 * mqtt_publish(), async, completes later via cb_publish_done() ->
 * sx_user_mqtt_is_publishing() going back to 0) -- it does not mean the
 * publish has actually reached the broker. sx_sleep_manager_enter_sleep()
 * then runs 'modem_power_off' (PWRKEY pulse) as one of its steps, which
 * can cut the modem before the AT command finishes, silently dropping
 * the telemetry -- confirmed on real hardware: "Telemetry published"
 * logged, followed immediately by "modem_power_off" starting, with
 * nothing arriving at the broker for that cycle.
 * APP_CYCLE_WAIT_PUBLISH polls sx_user_mqtt_is_publishing() every tick
 * (sx_user_mqtt_poll() already runs unconditionally every tick in
 * app_process(), regardless of app_mode/cycle_state, so publish
 * actually progresses while this state is active) and only proceeds to
 * sleep once it clears. Bounded by a timeout so a stuck/never-completing
 * publish (e.g. modem wedged) cannot block the device from ever
 * sleeping again.
 *
 * Bug fix (2026-08-01): first cut of this timeout was 10000ms, picked
 * arbitrarily without checking the modem layer's own timeout. Confirmed
 * on real hardware this was far too short — every cycle logged "Publish
 * still pending after 10000 ms, sleeping anyway" and the reading never
 * reached the broker, i.e. the fix was cutting the modem off mid-publish
 * exactly like the original bug, just 10s later instead of immediately.
 * a7677s.c's AT+CMQTTPUB flow (cb_mqtt_pub_payload_data ->
 * cb_mqtt_pub_send) is given A7677S_TIMEOUT_MQTT_PUB = 70000ms by the
 * driver itself to wait for the modem's +CMQTTPUB response (which in
 * turn tells the modem to allow up to A7677S_MQTT_PUB_TIMEOUT_S = 60s
 * for the actual over-the-air publish, datasheet section 18.2.12) — a
 * 10s app-level timeout could never win a race against that. Set to
 * match A7677S_TIMEOUT_MQTT_PUB exactly (see a7677s.h) rather than
 * picking another arbitrary number, plus a small margin so the app-level
 * timeout fires strictly after, not at the same time as, the driver's
 * own timeout (letting the driver's own error path run first and mark
 * s_publishing back to 0 via cb_publish_done, instead of both timeouts
 * racing each other).
 *
 * Deliberately NOT #include-ing a7677s.h here to pull in the real
 * A7677S_TIMEOUT_MQTT_PUB constant — app.c is written against the
 * modem_ops_t abstraction (via sx_mqtt.h/sx_user_mqtt.h) and reaching
 * into one specific driver's private header would break that layering
 * (see sx_mqtt.c's own doc-comment: swapping modems should only mean
 * writing a new driver against the same contract, not touching this
 * file). The value below is copied by hand instead — if a7677s.h's
 * A7677S_TIMEOUT_MQTT_PUB (currently 70000U) ever changes, or a
 * different modem driver with a different publish timeout is swapped
 * in, this must be updated to match by hand. */
#define APP_WAIT_PUBLISH_TIMEOUT_MS   (70000U + 2000U)
static char s_telemetry_json[TELEMETRY_JSON_BUFF_SIZE];

/* MQTT_STATION_DATA_TOPIC/MQTT_STATION_HEARTBEAT_TOPIC (app_config.h) are
 * prefixes only — the actual topic is "<prefix><device_id>" so multiple
 * stations sharing one broker publish to distinct topics, same pattern as
 * mqtt_rpc.c's build_rpc_request_topic()/build_rpc_response_topic(). A
 * device_id change via CLI/RPC takes effect on the next publish
 * immediately (unlike mqtt_rpc's subscribe, this isn't a one-time
 * subscription — every call rebuilds the topic fresh). */
#define TOPIC_BUFF_SIZE 64  /* longest prefix here + NETWORK_CONFIG_DEVICE_ID_MAX_LEN=32 fits well within this */

static void build_telemetry_topic(char *out, size_t out_size)
{
    snprintf(out, out_size, "%s%s", MQTT_STATION_DATA_TOPIC, network_config_get()->device_id);
}

static void build_heartbeat_topic(char *out, size_t out_size)
{
    snprintf(out, out_size, "%s%s", MQTT_STATION_HEARTBEAT_TOPIC, network_config_get()->device_id);
}

/* ===================== offline telemetry queue =====================
 * Ported from WS_v0's push_last_telemetry_json()/its SENDING-state save-
 * on-fail block (SynaptiX/apps/app.c), re-targeted at this project's
 * sx_storage_* API (network_config.c's flash layer) for both single-file
 * ops (write/read/delete/size) and directory scanning via
 * sx_storage_list() (sx_ex_storage.h) — added specifically for this
 * feature, since sx_storage_list_dir() only logs and returns nothing
 * usable to a caller. An earlier version of this code called
 * lfs_dir_open()/_read()/_close() directly instead, working around a
 * file_io.h opendir() macro bug (missing the required lfs_dir_t*
 * argument); that macro has since been fixed at the source and
 * sx_storage_list() now wraps it, so this file no longer touches
 * littlefs or file_io.h directly.
 *
 * One file per failed publish, named by a monotonic tick counter rather
 * than wall-clock time (WS_v0 used uptime_seconds for the same "make each
 * filename unique" purpose — this project has no wall clock at the point
 * SENDING runs early in the cycle, only s_cycle_tick_ms which resets each
 * lap, so a separate ever-incrementing counter is used instead).
 *
 * Resend is one file per SENDING pass, oldest-first by directory scan
 * order (littlefs doesn't guarangee a particular order, but in practice
 * files are returned in creation order for this filesystem) — same
 * "one at a time, not a batch drain" shape as WS_v0, so a long backlog
 * drains gradually across several cycles rather than trying to flush
 * everything (and blocking the cycle) the moment connectivity returns. */
#define OFFLINE_QUEUE_DIR       "/queue"
#define OFFLINE_QUEUE_MAX_FILES 20   /* oldest file dropped once this many are queued, so a long outage can't fill the flash */

static uint32_t s_offline_queue_seq = 0;

/* Last-known-good GPS fix, persisted to exflash so a fix acquired earlier
 * survives being published on later SENDING passes where GPS has since
 * lost fix (board.gps.latitude/longtitude in RAM stay at their last value
 * across a fix loss too, but only within the current boot — this file
 * survives a reset, and is what "fix_gps: 0" payloads read from). Per the
 * user (2026-08-02): written only on a 0->1 fix transition (edge, not
 * level — see s_gps_was_fixed below), erasing the previous file first
 * (sx_storage_delete() then sx_storage_write(), not a plain overwrite)
 * before writing the new one. One record only, no history. */
#define GPS_LOG_PATH "/log_gps"

typedef struct {
    float latitude;
    float longtitude;
} gps_log_record_t;

/* Tracks the previous tick's fix state so gps_process() being called every
 * tick (see app_process()) only triggers a flash write on the instant fix
 * appears (0->1), not on every tick fix stays true — a stable fix held for
 * many seconds/minutes must not re-write flash every tick. Reset to false
 * at boot (no fix yet), so the very first fix of a boot is always treated
 * as a 0->1 transition and saved. */
static bool s_gps_was_fixed = false;

/* Called once per 0->1 fix transition (see s_gps_was_fixed's doc-comment
 * above) with the just-acquired coordinates. Explicit delete-then-write
 * per the user's request, rather than relying on sx_storage_write()'s own
 * overwrite behavior. */
static void gps_log_save_fix(float latitude, float longtitude)
{
    sx_storage_delete(GPS_LOG_PATH);

    gps_log_record_t rec = { .latitude = latitude, .longtitude = longtitude };
    if (sx_storage_write(GPS_LOG_PATH, &rec, sizeof(rec)) == SX_STORAGE_OK) {
        log_info(TAG, "GPS fix acquired, saved to %s: lat=%f lon=%f",
                 GPS_LOG_PATH, (double)latitude, (double)longtitude);
    } else {
        log_error(TAG, "Failed to save GPS fix to %s", GPS_LOG_PATH);
    }
}

/* Reads the last-saved fix back for a fix_gps:0 payload. Returns true and
 * fills *out_lat/*out_lon on success, false if no fix has ever been saved
 * (fresh board / file never created) or the read fails. */
static bool gps_log_read_last(float *out_lat, float *out_lon)
{
    if (!sx_storage_exists(GPS_LOG_PATH)) {
        return false;
    }

    gps_log_record_t rec;
    if (sx_storage_read(GPS_LOG_PATH, &rec, sizeof(rec)) != SX_STORAGE_OK) {
        log_error(TAG, "Failed to read %s despite it existing", GPS_LOG_PATH);
        return false;
    }

    *out_lat = rec.latitude;
    *out_lon = rec.longtitude;
    return true;
}

/* Actually builds + publishes one heartbeat, unconditionally -- no
 * elapsed-time check. Split out of send_heartbeat_if_due() (2026-08-10)
 * so sx_sleep_manager.c's HB_ONLY mini-wake can call it directly: HB_ONLY
 * already decides *when* to publish by construction (it only runs after
 * sleeping exactly one heartbeat_ms-sized chunk -- see app.c's
 * APP_MODE_ENTER_SLEEP branch), so re-checking elapsed time here via
 * send_heartbeat_if_due()'s (now_ms - s_last_heartbeat_tick_ms) test would
 * be actively wrong: HAL_GetTick() (SysTick) is frozen throughout STOP
 * mode (same root cause as the heartbeat-never-fires bug this whole
 * HB_ONLY feature exists to work around -- see the top-level handoff
 * notes), so the elapsed-tick delta between consecutive HB_ONLY wakes
 * would almost always read as less than heartbeat_ms and silently skip
 * every single one. Still updates s_last_heartbeat_tick_ms so
 * send_heartbeat_if_due()'s own (unrelated, SENDING-time) elapsed check
 * doesn't immediately re-fire right after an HB_ONLY publish. Defined
 * further down (after build_heartbeat_payload()), not here -- see that
 * definition for the actual body; this forward-declares it so
 * app_init()'s sx_sleep_manager_init() call (which happens earlier in
 * this file) can pass it as a function pointer. */
static void publish_heartbeat_now(void);

/* Heartbeat now fires by wall-clock elapsed time (network_config_get()'s
 * heartbeat_ms), not by counting SENDING passes — see heartbeat_ms's
 * doc-comment in network_config.h for why counting cycles was replaced.
 * s_last_heartbeat_tick_ms is a HAL_GetTick() snapshot of the last time a
 * heartbeat was actually sent (or attempted — see send_heartbeat_if_due()
 * below); 0 at boot so the very first SENDING pass after power-up always
 * sends one immediately rather than waiting a full heartbeat_ms first. */
static uint32_t s_last_heartbeat_tick_ms = 0;

/* Saves payload as a new file in OFFLINE_QUEUE_DIR. Silently gives up (logs
 * only) if the write itself fails — same as WS_v0's fopen()==NULL branch,
 * there is no further fallback beyond this one layer of persistence. */
static void offline_queue_save(const char *payload)
{
    /* Drop the oldest queued file first if already at the cap, so this
     * save always has room — checked before, not after, so a full queue
     * never silently fails to save the newest reading instead of the
     * oldest one.
     *
     * "Oldest" here just means "first regular file the directory scan
     * returns" (see sx_storage_list()'s doc-comment on littlefs not
     * guaranteeing order, but returning creation order in practice). */
    sx_storage_entry_t entries[OFFLINE_QUEUE_MAX_FILES + 1];
    int32_t count = sx_storage_list(OFFLINE_QUEUE_DIR, entries, OFFLINE_QUEUE_MAX_FILES + 1);

    if (count > 0 && (uint32_t)count >= OFFLINE_QUEUE_MAX_FILES) {
        /* First non-dir entry found is treated as oldest. */
        for (int32_t i = 0; i < count; i++) {
            if (!entries[i].is_dir) {
                char path[96];
                snprintf(path, sizeof(path), "%s/%s", OFFLINE_QUEUE_DIR, entries[i].name);
                log_warn(TAG, "Offline queue full (%ld files), dropping oldest: %s",
                         (long)count, path);
                sx_storage_delete(path);
                break;
            }
        }
    }

    char path[64];
    snprintf(path, sizeof(path), "%s/telemetry_%lu.json",
             OFFLINE_QUEUE_DIR, (unsigned long)s_offline_queue_seq++);

    if (sx_storage_write(path, payload, strlen(payload)) == SX_STORAGE_OK) {
        log_info(TAG, "Saved telemetry to offline queue: %s", path);
    } else {
        log_error(TAG, "Failed to save telemetry to offline queue: %s", path);
    }
}

/* Resends at most one queued file over MQTT, deleting it on success.
 * Called only while sx_user_mqtt_is_connected() (checked by the caller),
 * same precondition WS_v0's push_last_telemetry_json() effectively relied
 * on via tb_publish()'s own connectivity check. No-op if the queue is
 * empty. Returns 1 if a file was found (sent or not), 0 if the queue was
 * empty — callers don't currently use the return value but it's cheap to
 * report and useful for future logging/metrics. */
static int offline_queue_resend_one(void)
{
    sx_storage_entry_t entries[OFFLINE_QUEUE_MAX_FILES + 1];
    int32_t count = sx_storage_list(OFFLINE_QUEUE_DIR, entries, OFFLINE_QUEUE_MAX_FILES + 1);
    if (count <= 0) {
        return 0; /* queue empty or list failed */
    }

    const char *name = NULL;
    for (int32_t i = 0; i < count; i++) {
        if (!entries[i].is_dir) {
            name = entries[i].name;
            break;
        }
    }
    if (name == NULL) {
        return 0; /* queue empty (only subdirs, if any) */
    }

    char path[96];
    snprintf(path, sizeof(path), "%s/%s", OFFLINE_QUEUE_DIR, name);

    static char buf[TELEMETRY_JSON_BUFF_SIZE];
    int32_t size = sx_storage_size(path);
    if (size <= 0 || (uint32_t)size >= sizeof(buf)) {
        log_error(TAG, "Offline queue file %s has bad size (%ld), dropping", path, (long)size);
        sx_storage_delete(path);
        return 1;
    }
    memset(buf, 0, sizeof(buf));
    if (sx_storage_read(path, buf, (uint32_t)size) != SX_STORAGE_OK) {
        log_error(TAG, "Failed to read offline queue file %s, dropping", path);
        sx_storage_delete(path);
        return 1;
    }

    char topic[TOPIC_BUFF_SIZE];
    build_telemetry_topic(topic, sizeof(topic));
    sx_user_mqtt_publish(topic, buf);
    log_info(TAG, "Resent queued telemetry: %s", path);
    sx_storage_delete(path);
    return 1;
}

/* Formats board.rtc (rx8130ce, see sx_ex_rtc.h) as an ISO-8601 UTC string
 * ("YYYY-MM-DDTHH:MM:SSZ") into a caller-owned buffer, returning true on
 * success. Returns false (buffer left untouched) if the RTC hasn't been
 * validly set yet — rx8130ce_is_time_valid() reflects the chip's own
 * voltage-loss flag (RX8130CE_FLAG_VLF), so this also catches a
 * brand-new/battery-drained RTC that time_sync.c hasn't stamped yet.
 * WS_v0 had no equivalent normalized format (its ntp_get_time() just
 * stored the modem's raw AT-command string verbatim) — ISO-8601 is this
 * project's own choice, not a port.
 * rx8130ce_time_t's year field is 2-digit (e.g. 25 = 2025, see
 * sx_ex_rtc.h), so 2000 is added here. */
/* Vietnam local time offset, in hours (UTC+7, no DST). */
#define VN_UTC_OFFSET_HOURS   7

static bool format_timestamp(char *out, size_t out_size)
{
    bool valid = false;
    if (rx8130ce_is_time_valid(&board.rtc, &valid) != RX8130CE_OK || !valid) {
        return false;
    }

    rx8130ce_time_t t;
    if (rx8130ce_get_time(&board.rtc, &t) != RX8130CE_OK) {
        return false;
    }

    /* board.rtc is kept in UTC (time_sync.c writes it from modem NITZ /
     * GPS, both already normalized to UTC). Convert to Vietnam local time
     * (UTC+7) via mktime()/gmtime() rather than adding 7 to t.hour
     * directly -- a plain add can push hour past 23 (e.g. 22:00 UTC ->
     * "29:00") or, without day/month/year rollover, land on the wrong
     * date entirely. mktime()/gmtime() normalize the whole struct tm
     * (day/month/year rollover included), same pattern already used in
     * time_sync.c and a7677s.c's cb_cclk() for the reverse conversion. */
    struct tm tm_utc = {0};
    tm_utc.tm_year = (2000 + t.year) - 1900;
    tm_utc.tm_mon  = t.month - 1;
    tm_utc.tm_mday = t.day;
    tm_utc.tm_hour = t.hour + VN_UTC_OFFSET_HOURS;
    tm_utc.tm_min  = t.min;
    tm_utc.tm_sec  = t.sec;
    tm_utc.tm_isdst = 0;

    time_t as_time = mktime(&tm_utc);
    if (as_time == (time_t)-1) {
        return false;
    }
    struct tm *vn = gmtime(&as_time);
    if (!vn) {
        return false;
    }

    snprintf(out, out_size, "%04d-%02d-%02dT%02d:%02d:%02d+07:00",
             vn->tm_year + 1900, vn->tm_mon + 1, vn->tm_mday,
             vn->tm_hour, vn->tm_min, vn->tm_sec);
    return true;
}

/* Rounds a GPS coordinate to 6 decimal places (~11cm precision at the
 * equator) before it goes into a JSON payload. Per the user (2026-08-04):
 * Google Maps itself only supports 6 decimal places, so publishing more
 * than that is pointless precision that also happens to expose float's
 * binary-representation noise as long trailing digits (e.g.
 * "105.73182678222656") now that cJSON.c's print_number() was fixed to
 * print full round-trippable precision instead of silently truncating to
 * 4 decimals. This keeps board.gps.latitude/longtitude themselves as
 * float (no change to gps.h/minmea.h) and only rounds at the point of
 * building outgoing JSON, so nothing about GPS storage/comparison logic
 * elsewhere (sx_sleep_manager.c's fix-liveness check, gps_log_save_fix(),
 * etc.) is affected. */
static double gps_round_coord(double value)
{
    return round(value * 1e6) / 1e6;
}

/* Builds the telemetry JSON payload, nested structure matching WS_v0's
 * dataPayload() exactly (per the user, 2026-08-05): location/environment/
 * sensors as nested objects rather than flat top-level fields. Missing/
 * not-yet-ready readings are still emitted as JSON null within their
 * nested object, same "don't fake a zero" convention as before — only the
 * shape changed, not the readiness gating logic itself.
 *
 * fix_gps DROPPED per the user (2026-08-05) — no fallback to the last
 * saved GPS_LOG_PATH fix either; latitude/longitude are null whenever
 * board.gps does not have a live fix right now, full stop. */
static const char *build_telemetry_payload(void)
{
    cJSON *root = cJSON_CreateObject();

    cJSON_AddStringToObject(root, "deviceID", network_config_get()->device_id);

    char timestamp[32];
    if (format_timestamp(timestamp, sizeof(timestamp))) {
        cJSON_AddStringToObject(root, "timestamp", timestamp);
    } else {
        cJSON_AddNullToObject(root, "timestamp");
    }

    cJSON_AddStringToObject(root, "motionState",
                             accel_app_is_movement_detected(&s_accel_app) ? "moving" : "stationary");

    cJSON *location = cJSON_CreateObject();
    if (board.gps.latitude != 0.0f && board.gps.longtitude != 0.0f) {
        cJSON_AddNumberToObject(location, "latitude", gps_round_coord(board.gps.latitude));
        cJSON_AddNumberToObject(location, "longitude", gps_round_coord(board.gps.longtitude));
    } else {
        cJSON_AddNullToObject(location, "latitude");
        cJSON_AddNullToObject(location, "longitude");
    }
    cJSON_AddItemToObject(root, "location", location);

    cJSON *environment = cJSON_CreateObject();
    if (sx_temp_humi_is_ready(&s_temp_humi)) {
        cJSON_AddNumberToObject(environment, "temperature", sx_temp_humi_get_temperature(&s_temp_humi));
        cJSON_AddNumberToObject(environment, "humidity", sx_temp_humi_get_humidity(&s_temp_humi));
    } else {
        cJSON_AddNullToObject(environment, "temperature");
        cJSON_AddNullToObject(environment, "humidity");
    }
    cJSON_AddItemToObject(root, "environment", environment);

    cJSON *sensor = cJSON_CreateObject();
    if (sps30_app_has_measurement(&s_sps30_app)) {
        const sps30_app_measurement_t *m = sps30_app_get_measurement(&s_sps30_app);
        cJSON_AddNumberToObject(sensor, "pm10", m->mc_10p0);
        cJSON_AddNumberToObject(sensor, "pm2_5", m->mc_2p5);
    } else {
        cJSON_AddNullToObject(sensor, "pm10");
        cJSON_AddNullToObject(sensor, "pm2_5");
    }

    /* Gas sensor channels — see gas_sensor_app.h: value is only meaningful
     * when is_connected() is true, so null it out otherwise rather than
     * publishing a stale/zeroed reading.
     *
     * NOTE (2026-08-12): deliberately still reads is_connected() LIVE
     * here, unlike build_heartbeat_payload()'s sensorStatus[] (which now
     * always reads the phase-1/SENSING-end snapshot — see
     * sx_sleep_manager_gas_snapshot_capture()). This function decides
     * whether to publish an actual NUMBER or null, so snapshotting it
     * the same way would risk publishing a stale numeric reading as if
     * still live merely because it was connected earlier in the cycle —
     * a worse failure mode than sensorStatus's cosmetic FAIL text. The
     * two call sites intentionally answer different questions ("was this
     * channel healthy a moment ago" vs "is this specific number safe to
     * publish right now") and should not be unified. */
    static const struct { GasSensorType_t type; const char *key; } gas_channels[] = {
        { GAS_SENSOR_CO,  "co"  },
        { GAS_SENSOR_SO2, "so2" },
        { GAS_SENSOR_NO2, "no2" },
        { GAS_SENSOR_O3,  "o3"  },
        { GAS_SENSOR_H2S, "h2s" },
    };
    for (size_t i = 0; i < sizeof(gas_channels) / sizeof(gas_channels[0]); i++) {
        if (gas_sensor_app_is_connected(gas_channels[i].type)) {
            cJSON_AddNumberToObject(sensor, gas_channels[i].key,
                                     gas_sensor_app_get_value(gas_channels[i].type));
        } else {
            cJSON_AddNullToObject(sensor, gas_channels[i].key);
        }
    }
    cJSON_AddItemToObject(root, "sensors", sensor);

    memset(s_telemetry_json, 0, sizeof(s_telemetry_json));
    cJSON_PrintPreallocated(root, s_telemetry_json, TELEMETRY_JSON_BUFF_SIZE, 0);
    cJSON_Delete(root);
    return s_telemetry_json;
}

#define HEARTBEAT_JSON_BUFF_SIZE 1024
static char s_heartbeat_json[HEARTBEAT_JSON_BUFF_SIZE];

/* Builds the periodic device-health payload — RESTORED to full WS_v0
 * heartBeatPayload() shape per the user (2026-08-05), reversing the
 * 2026-08-02 trim mentioned below. Adds back: firmwareVersion, uptime,
 * nested network{signalStrength,operator}, nested power{source} (soc
 * DROPPED — no state-of-charge percentage source exists in this
 * codebase, only raw railVoltage/railCurrent via power_monitor_app.h, and
 * the user confirmed (2026-08-05) to just drop soc rather than compute a
 * rough estimate from voltage), nested memory{total,storageUsed}
 * (HARDCODED same as WS_v0's dataPayload() — 2048/128 KB — per the user
 * (2026-08-05) explicitly choosing not to wire up a real flash-usage
 * calculation here, keep it simple), and a sensorStatus array with one
 * {sensor, status} entry per physical sensor type reporting OK/FAIL from
 * each app's readiness getter.
 *
 * Previously (2026-08-02) trimmed to 5 fields; that trim's reasoning
 * ("heartbeat as a lightweight ping, full detail lives on telemetry") no
 * longer applies now that the user wants WS_v0 parity — see git history/
 * chat log around 2026-08-02 if that older reasoning needs revisiting. */
static const char *build_heartbeat_payload(void)
{
    cJSON *root = cJSON_CreateObject();

    cJSON_AddStringToObject(root, "deviceID", network_config_get()->device_id);

    char timestamp[32];
    if (format_timestamp(timestamp, sizeof(timestamp))) {
        cJSON_AddStringToObject(root, "timestamp", timestamp);
    } else {
        cJSON_AddNullToObject(root, "timestamp");
    }

    cJSON_AddStringToObject(root, "firmwareVersion", APP_FW_VERSION);

    /* uptime in ms since boot — HAL_GetTick() directly, same source
     * s_last_heartbeat_tick_ms below already uses for its own timing, no
     * separate uptime-tracking state needed. */
    cJSON_AddNumberToObject(root, "uptime", (double)HAL_GetTick());

    cJSON *network = cJSON_CreateObject();
    /* sx_user_mqtt_get_rssi()/get_operator() read board.modem.ops-> under
     * the hood — no readiness gate at this layer, published as-is same as
     * before the WS_v0-parity restore (see a7677s.c's cb_cops_query() and
     * a7677s_get_rssi() doc-comments for their own default-until-ready
     * caveats). */
    cJSON_AddNumberToObject(network, "signalStrength", sx_user_mqtt_get_rssi());
    const char *op = sx_user_mqtt_get_operator();
    if (op && op[0] != '\0') {
        cJSON_AddStringToObject(network, "operator", op);
    } else {
        cJSON_AddNullToObject(network, "operator");
    }
    cJSON_AddItemToObject(root, "network", network);

    cJSON *power = cJSON_CreateObject();
    cJSON_AddStringToObject(power, "source", "battery");
    cJSON_AddItemToObject(root, "power", power);

    cJSON *memory = cJSON_CreateObject();
    cJSON_AddNumberToObject(memory, "total", 2048);      // in KB, hardcoded per the user (2026-08-05)
    cJSON_AddNumberToObject(memory, "storageUsed", 128); // in KB, hardcoded per the user (2026-08-05)
    cJSON_AddItemToObject(root, "memory", memory);

    cJSON_AddStringToObject(root, "motionState",
                             accel_app_is_movement_detected(&s_accel_app) ? "moving" : "stationary");

    cJSON *sensorStatus = cJSON_CreateArray();

    cJSON *temphum = cJSON_CreateObject();
    cJSON_AddStringToObject(temphum, "sensor", "Temperature_Humidity_Sensor");
    cJSON_AddStringToObject(temphum, "status", sx_temp_humi_is_ready(&s_temp_humi) ? "OK" : "FAIL");
    cJSON_AddItemToArray(sensorStatus, temphum);

    cJSON *pm = cJSON_CreateObject();
    cJSON_AddStringToObject(pm, "sensor", "PM_Sensor");
    cJSON_AddStringToObject(pm, "status", sps30_app_has_measurement(&s_sps30_app) ? "OK" : "FAIL");
    cJSON_AddItemToArray(sensorStatus, pm);

    /* Same {type, sensor-name} table shape as build_telemetry_payload()'s
     * gas_channels[] above, just mapped to OK/FAIL here instead of a
     * numeric value — kept as a separate local array rather than sharing
     * one, since the two need different label text ("SO2_Sensor" here vs
     * "so2" JSON key there) and reusing gas_channels[] verbatim would
     * mean overloading one field's meaning across two different payload
     * shapes, more confusing than the small duplication. */
    static const struct { GasSensorType_t type; const char *name; } gas_status_channels[] = {
        { GAS_SENSOR_CO,  "CO_Sensor"  },
        { GAS_SENSOR_SO2, "SO2_Sensor" },
        { GAS_SENSOR_NO2, "NO2_Sensor" },
        { GAS_SENSOR_O3,  "O3_Sensor"  },
        { GAS_SENSOR_H2S, "H2S_Sensor" },
    };
    /* BUG FIX (2026-08-12), reported on real hardware: gas_sensor_app_
     * is_connected() reads gas_sensor[i].isConnected LIVE off ze12a.c's
     * own GAS_SENSOR_TIMEOUT_MS (10s) countdown. Originally only
     * suspected during HB_ONLY (phase 2's modem cooldown/handshake/
     * connect alone runs well past 10s), so this used to read the
     * snapshot only for HB_ONLY and stay live for full-wake. But a real
     * log this same session showed the identical symptom on full-wake
     * too: a data-topic payload with fresh so2/no2/o3 readings,
     * immediately followed by that lap's heartbeat correctly OK, versus
     * a different lap (further from its last valid gas frame — e.g.
     * after a long GPS-fix wait during SENSING) whose heartbeat showed
     * FAIL for every channel despite ZE12A never losing power. SENSING's
     * duration plus any slow step before SENDING can just as easily run
     * GAS_SENSOR_TIMEOUT_MS out on the full-wake path — "sensor check
     * runs right up until this point" was true for the SENSING window
     * itself but not for however long SENDING takes to actually reach
     * this line. Now ALWAYS read the snapshot (captured at the
     * APP_CYCLE_SENSING -> APP_CYCLE_SENDING transition for full-wake,
     * or at phase 1's end for HB_ONLY — see
     * sx_sleep_manager_gas_snapshot_capture()'s call sites) instead of
     * ever calling gas_sensor_app_is_connected() live here, for both
     * wake paths uniformly. */
    for (size_t i = 0; i < sizeof(gas_status_channels) / sizeof(gas_status_channels[0]); i++) {
        bool connected = sx_sleep_manager_gas_snapshot_connected(&s_sleep_mgr, gas_status_channels[i].type);
        cJSON *g = cJSON_CreateObject();
        cJSON_AddStringToObject(g, "sensor", gas_status_channels[i].name);
        cJSON_AddStringToObject(g, "status", connected ? "OK" : "FAIL");
        cJSON_AddItemToArray(sensorStatus, g);
    }

    cJSON_AddItemToObject(root, "sensorStatus", sensorStatus);

    memset(s_heartbeat_json, 0, sizeof(s_heartbeat_json));
    cJSON_PrintPreallocated(root, s_heartbeat_json, HEARTBEAT_JSON_BUFF_SIZE, 0);
    cJSON_Delete(root);
    return s_heartbeat_json;
}

/* Actual definition (forward-declared earlier in this file, right after
 * build_heartbeat_topic() -- see that declaration's doc-comment for why).
 * Builds + publishes one heartbeat unconditionally, no elapsed-time
 * check. */
static void publish_heartbeat_now(void)
{
    if (!sx_user_mqtt_is_connected()) {
        log_warn(TAG, "MQTT not connected, heartbeat skipped this cycle");
        return;
    }

    s_last_heartbeat_tick_ms = HAL_GetTick();

    const char *payload = build_heartbeat_payload();
    char topic[TOPIC_BUFF_SIZE];
    build_heartbeat_topic(topic, sizeof(topic));
    sx_user_mqtt_publish(topic, payload);
    log_info(TAG, "Heartbeat published: %s", payload);
}

/* Unlike telemetry, a heartbeat that fails to send (not connected) is
 * simply skipped for this cycle, not queued — same as WS_v0's
 * push_last_telemetry_json() (its heartbeat publish returns early on
 * failure with no persistence, only telemetry uses the /data queue). A
 * heartbeat describes current device health; a stale one delivered late
 * from a queue would misreport uptime/signal/power as of some earlier
 * moment, so there is little value in persisting it the way a telemetry
 * *reading* (still historically valid whenever it arrives) has. */
static void send_heartbeat_if_due(void)
{
    uint32_t now_ms = HAL_GetTick();
    uint32_t heartbeat_ms = network_config_get()->heartbeat_ms;

    /* (uint32_t) subtraction wraps correctly across HAL_GetTick()'s ~49.7
     * day rollover, same pattern as the s_cycle_tick_ms accumulation
     * elsewhere in this file. */
    if ((now_ms - s_last_heartbeat_tick_ms) < heartbeat_ms) {
        return;
    }

    /* publish_heartbeat_now() already re-checks MQTT connectivity and
     * updates s_last_heartbeat_tick_ms itself -- but deliberately does
     * NOT skip re-arming s_last_heartbeat_tick_ms when disconnected here,
     * unlike this function's old inline behavior (which left
     * s_last_heartbeat_tick_ms untouched on failure so the next SENDING
     * pass would retry immediately). To preserve that specific retry
     * behavior for the ordinary SENDING path, check connectivity here
     * too before delegating, rather than relying on
     * publish_heartbeat_now()'s own check. */
    if (!sx_user_mqtt_is_connected()) {
        log_warn(TAG, "MQTT not connected, heartbeat skipped this cycle");
        /* Do NOT update s_last_heartbeat_tick_ms here — leave it due so
         * the next SENDING pass (whenever that is) retries immediately
         * instead of waiting another full heartbeat_ms with no MQTT. */
        return;
    }

    publish_heartbeat_now();
}

/* ===================== FULL_POWER cycle: ON_PUMP -> SENSING -> SENDING
 * ===================== SLEEPING -> WAKING -> back to ON_PUMP
 *
 * Per the user (2026-07-15): the whole board sleeps between cycles, not
 * just individual peripherals independently — SLEEPING here calls
 * sx_sleep_manager_enter_sleep() (tier 3), which runs every registered
 * sleep_step (GPS/modem power-down, SPS30 SHDLC-sleep+EN_PW_DUST-low,
 * pump off, ZE12A to QA mode, BNO055 to suspend — see
 * sx_sleep_manager.c) and then actually parks the STM32 itself in STOP
 * mode via tier 1 (sx_sleep_enter_stop()). sx_sleep_manager_enter_sleep()
 * is blocking: it does not return until the RTC wakeup timer fires and
 * the MCU resumes — so app_process() below only calls it once per
 * SLEEPING-state entry and then waits out the following WAKING state
 * once execution resumes on the other side of that call. */
static void app_cycle_process(uint32_t delta_ms)
{
    switch (s_cycle_state) {
    case APP_CYCLE_ON_PUMP:
        s_cycle_tick_ms += delta_ms;
        if (s_cycle_tick_ms == delta_ms) {
            /* First tick after entering this state (including the very
             * first lap after boot, and every lap after WAKING) —
             * kick the pump on exactly once, at the configured duty
             * (not a hardcoded full-on) — see network_config_t's
             * pump_duty_percent doc-comment and shell_commands.c's/
             * mqtt_rpc.c's "-duty" flag for how it's set. */
            pump_set_power(sx_board_get_pump_pwm(), network_config_get()->pump_duty_percent);
            log_info(TAG, "Pump on at %u%% duty", network_config_get()->pump_duty_percent);
        }
        if (s_cycle_tick_ms >= network_config_get()->pump_on_ms) {
            s_cycle_tick_ms = 0;
            s_cycle_state = APP_CYCLE_SENSING;
            /* BUG FIX (2026-08-11, per user request): pump must be OFF
             * during SENSING, not left running through the whole sensing
             * window and only switched off at the end. Turned off here,
             * right at the ON_PUMP -> SENSING transition, before
             * sps30_app_start_cycle() kicks off the SPS30 measurement --
             * previously this call was at the SENSING -> SENDING
             * transition below (see the removed pump_off() there), so the
             * pump used to stay on for the pump_on_ms + sensing_ms
             * duration instead of just pump_on_ms. */
            pump_off(sx_board_get_pump_pwm());
            sps30_app_start_cycle(&s_sps30_app);
            log_info(TAG, "Pump off, sensing started");
        }
        break;

    case APP_CYCLE_SENSING:
        sps30_app_poll(&s_sps30_app, delta_ms);
        s_cycle_tick_ms += delta_ms;
        if (s_cycle_tick_ms >= network_config_get()->sensing_ms) {
            s_cycle_tick_ms = 0;
            s_cycle_state = APP_CYCLE_SENDING;
            /* BUG FIX (2026-08-12): snapshot each gas channel's
             * isConnected RIGHT HERE, at the SENSING -> SENDING
             * transition, before build_telemetry_payload()/
             * build_heartbeat_payload() in APP_CYCLE_SENDING (and
             * whatever MQTT connect/publish work runs before send_
             * heartbeat_if_due() actually gets to build its JSON) has a
             * chance to run GAS_SENSOR_TIMEOUT_MS out on a channel that
             * was genuinely fine moments ago. Same pattern as HB_ONLY's
             * phase-1-end snapshot — see
             * sx_sleep_manager_gas_snapshot_capture()'s doc-comment. */
            sx_sleep_manager_gas_snapshot_capture(&s_sleep_mgr);
            log_info(TAG, "Sensing done, sending data");
        }
        break;

    case APP_CYCLE_SENDING: {
        const char *payload = build_telemetry_payload();
        /* Plain MQTT publish, not thingsboard_client_publish_telemetry()
         * — see app_init()'s comment on why Thingsboard isn't used yet.
         * Topic is MQTT_STATION_DATA_TOPIC + device_id (see
         * build_telemetry_topic()), so multiple stations sharing one
         * broker land on distinct topics. */
        if (sx_user_mqtt_is_connected()) {
            /* Drain one backlog file first, same "one at a time" shape as
             * WS_v0's push_last_telemetry_json() — see its doc-comment
             * above build_telemetry_payload(). Done before publishing
             * this cycle's fresh reading so the backlog gets a turn every
             * cycle instead of only when nothing new needs sending. */
            offline_queue_resend_one();

            char topic[TOPIC_BUFF_SIZE];
            build_telemetry_topic(topic, sizeof(topic));
            sx_user_mqtt_publish(topic, payload);
            log_info(TAG, "Telemetry queued for publish: %s", payload);
        } else {
            offline_queue_save(payload);
            log_warn(TAG, "MQTT not connected, telemetry queued: %s", payload);
        }
        send_heartbeat_if_due();
        sps30_app_reset(&s_sps30_app);
        s_cycle_state = APP_CYCLE_WAIT_PUBLISH;
        /* Do NOT set APP_MODE_ENTER_SLEEP yet — see
         * APP_WAIT_PUBLISH_TIMEOUT_MS's comment above. app_mode stays
         * APP_MODE_FULL_POWER so app_cycle_process() keeps being called
         * every tick (needed to poll this new state), while
         * sx_user_mqtt_poll() in app_process() (unconditional, runs
         * regardless of cycle_state) drives the actual publish to
         * completion in the background. */
        s_cycle_tick_ms = 0;
        break;
    }

    case APP_CYCLE_WAIT_PUBLISH:
        s_cycle_tick_ms += delta_ms;
        if (!sx_user_mqtt_is_publishing() ||
            s_cycle_tick_ms >= APP_WAIT_PUBLISH_TIMEOUT_MS) {
            if (s_cycle_tick_ms >= APP_WAIT_PUBLISH_TIMEOUT_MS &&
                sx_user_mqtt_is_publishing()) {
                log_warn(TAG, "Publish still pending after %lu ms, sleeping anyway",
                         (unsigned long)APP_WAIT_PUBLISH_TIMEOUT_MS);
            } else {
                log_info(TAG, "Publish confirmed done after %lu ms",
                         (unsigned long)s_cycle_tick_ms);
            }

            /* FOTA is not wired up on this branch (main) — fota.c/fota.h
             * do not exist in this repo yet (a previous commit here,
             * c9352dc "build fail", called fota_is_pending()/
             * fota_download()/fota_init()/fota_on_message() without ever
             * adding those files, so main would not build). Removed
             * rather than stubbed out — see ft/fota_ws for the branch
             * actually wiring FOTA in. If/when FOTA lands on main for
             * real, this is the right spot for it: AFTER telemetry
             * publish is confirmed done (or timed out) above, BEFORE
             * APP_CYCLE_SLEEPING below, so a slow/failed download can
             * never delay or corrupt this cycle's telemetry publish. */

            s_cycle_tick_ms = 0;
            s_cycle_state   = APP_CYCLE_SLEEPING;
            s_app_mode      = APP_MODE_ENTER_SLEEP;
            /* app_process() below acts on APP_MODE_ENTER_SLEEP right
             * after this switch returns — see the comment there for why
             * the actual sx_sleep_manager_enter_sleep() call lives in
             * app_process() rather than here. */
        }
        break;

    case APP_CYCLE_SLEEPING:
        /* Handled directly in app_process() (APP_MODE_ENTER_SLEEP branch)
         * — sx_sleep_manager_enter_sleep() is a blocking call, not
         * something to poll here across ticks. This case only exists so
         * s_cycle_state has a defined value while that blocking call is
         * in flight/about to happen; app_cycle_process() itself is not
         * re-entered until it returns (see app_process()). */
        break;

    case APP_CYCLE_WAKING:
        if (sx_sleep_manager_is_wake_done(&s_sleep_mgr)) {
            sx_sleep_manager_reset_wake(&s_sleep_mgr);
            /* Bug fix (2026-08-01): re-sync the RTC from this wake's fresh
             * modem NITZ reading (a7677s.c's CCLK init step already reran
             * during the wake steps just completed, logging "Network time
             * synced (UTC): ..." — see time_sync.h's top comment) instead
             * of leaving it frozen at whatever the very first sync wrote.
             * Confirmed on real hardware: without this, ordinary RX8130CE
             * quartz drift built up to ~38s of offset between the RTC-
             * derived telemetry timestamp and the wall-clock time the
             * payload actually reached the broker, after only tens of
             * minutes of runtime. time_sync_reset() only clears the
             * done flag; the actual re-sync happens via time_sync_poll(),
             * already called unconditionally every tick a few lines below
             * in app_process() — no separate poll call needed here. */
            time_sync_reset(&s_time_sync);
            s_cycle_tick_ms = 0;
            s_cycle_state   = APP_CYCLE_ON_PUMP;
            s_app_mode      = APP_MODE_FULL_POWER;
            log_info(TAG, "Wake sequence complete - resuming cycle");
        }
        break;

    default:
        s_cycle_state = APP_CYCLE_ON_PUMP;
        break;
    }
}

/* Wrapper matching sx_user_mqtt_set_modem_owned_elsewhere_check()'s plain
 * uint8_t(*)(void) callback signature -- sx_sleep_manager_is_waking()
 * itself needs &s_sleep_mgr, which a bare function pointer can't carry.
 * Ported from test_sleep.c's is_modem_owned_by_sleep_manager() (fix
 * confirmed on real hardware, 2026-07-29) -- app.c had never wired this
 * up, so sx_mqtt.c's recovery-ladder start() calls were racing directly
 * against sx_sleep_manager_wake_process()'s own modem power-on/attach
 * sequence. Confirmed on real hardware, 2026-08-01: "Power Off start"
 * followed immediately by "start(): power not ready yet" from the
 * recovery ladder firing mid power-off, then "mqtt_connect: modem busy"
 * after wake -- MQTT never reconnects, telemetry stays queued offline. */
static uint8_t is_modem_owned_by_sleep_manager(void)
{
    return sx_sleep_manager_is_waking(&s_sleep_mgr) ||
           sx_sleep_manager_hb_only_modem_owned(&s_sleep_mgr);
}

/* mqtt_cfg (below, in app_init()) has exactly ONE on_connected slot and ONE
 * on_message slot (sx_user_mqtt_cfg_t, sx_user_mqtt.h). Currently only one
 * consumer (mqtt_rpc.c) is wired in here — FOTA is not on this branch yet
 * (see the removed-FOTA comment in APP_CYCLE_WAIT_PUBLISH above for why).
 * If/when a second consumer is added, the safe pattern is: each consumer
 * filters its own topic internally and does nothing for topics it doesn't
 * own, so simply calling all of them unconditionally from these two
 * dispatchers is safe — same reasoning fota.c's wiring used before removal. */
static void mqtt_on_connected_dispatch(void)
{
    mqtt_rpc_init();
}

static void mqtt_on_message_dispatch(const char *topic, const char *message)
{
    mqtt_rpc_on_message(topic, message);
}

void app_init(void){
    log_info(TAG, "APP initializing ....");

    sps30_app_init(&s_sps30_app, sx_board_get_sps30_power_gpio());
    sx_temp_humi_init(&s_temp_humi, &board.sht3x);
    accel_app_init(&s_accel_app, &board.imu);
    power_monitor_app_init(&s_power_monitor, &board.ads1115);

    sx_sleep_manager_init(&s_sleep_mgr, &board.sleep, &board.modem, &board.gps,
                           &s_sps30_app, sx_board_get_pump_pwm(), &s_accel_app,
                           publish_heartbeat_now);

    /* Per the user (2026-07-15): Thingsboard is NOT used for now — no real
     * Thingsboard broker exists yet, and app_config.h's USE_THINGSBOARD==1
     * branch doesn't even define a broker host macro (see the TODO this
     * replaces, and network_config.h's doc-comment for the full reasoning).
     * Using the plain-MQTT broker from network_config instead (flash-backed,
     * runtime-editable — host/port/client_id/user/pass/APN can all change
     * without reflashing, though the actual CDC/MSC input mechanism to edit
     * them live is deliberately not wired up yet, per the user — "note lại
     * chưa cần code ngay"). Swap back to thingsboard_client_init() once a
     * real Thingsboard broker is available; thingsboard_client.c itself is
     * untouched and ready for that. */
    network_config_init();
    const network_config_t *net_cfg = network_config_get();

    /* Offline telemetry queue directory — see APP_CYCLE_SENDING and
     * offline_queue_resend_one()/offline_queue_save() below. Created once
     * here; sx_storage_mkdir() on an already-existing dir is expected to
     * just report it exists (same flash already sx_storage_init()'d by
     * sx_board_init() before app_init() runs, per network_config's own
     * assumption above). */
    sx_storage_mkdir(OFFLINE_QUEUE_DIR);

    /* Re-apply APN from network_config, overriding the APN/USERNAME_APN/
     * PASSWORD_APN macros sx_board_init() already set via
     * a7677s_set_full_apn() before app_init() runs (see Core/Src/main.c:
     * sx_board_init() then app_init()) — this way, once network_config's
     * flash-stored APN differs from the compile-time default (e.g. after
     * a future CDC/MSC edit + reboot), the modem picks up the stored value
     * rather than the hardcoded one. No-op on a fresh flash (network_config
     * falls back to the same APN/USERNAME_APN/PASSWORD_APN defaults
     * sx_board_init() already used). */
    a7677s_set_full_apn(&board.a7677s, net_cfg->apn,
                         net_cfg->apn_username[0] ? net_cfg->apn_username : NULL,
                         net_cfg->apn_password[0] ? net_cfg->apn_password : NULL);

    sx_user_mqtt_cfg_t mqtt_cfg = {0};
    mqtt_cfg.broker     = net_cfg->host;
    mqtt_cfg.port       = net_cfg->port;
    mqtt_cfg.client_id  = net_cfg->client_id;
    mqtt_cfg.username   = net_cfg->username[0] ? net_cfg->username : NULL;
    mqtt_cfg.password   = net_cfg->password[0] ? net_cfg->password : NULL;
    mqtt_cfg.keepalive  = net_cfg->keepalive_s;
    mqtt_cfg.use_ssl    = net_cfg->use_tls ? 1 : 0;
    /* MQTT RPC config-set channel (app/user/mqtt_rpc/) — a second input
     * path alongside the USB CDC CLI (shell_app), both ending up at the
     * same network_config_set_*()/network_config_save() calls.
     *
     * on_connected (not a direct mqtt_rpc_init() call right after
     * *_init() below) is deliberate: sx_user_mqtt_nontls_init()/
     * _tls_init() only start the connection sequence, they don't block
     * until connected (the actual handshake happens across later
     * sx_user_mqtt_poll() ticks) — sx_user_mqtt_subscribe() itself is a
     * no-op (logs a warning, does nothing else) if called before the
     * client reports connected. Hooking on_connected guarantees the
     * subscribe happens exactly when it can succeed, including on any
     * future reconnect after a drop. */
    mqtt_cfg.on_message   = mqtt_on_message_dispatch;
    mqtt_cfg.on_connected = mqtt_on_connected_dispatch;
    if (net_cfg->use_tls) {
        sx_user_mqtt_tls_init(&mqtt_cfg,
                               net_cfg->ca_cert_len ? (char *)net_cfg->ca_cert : NULL,
                               net_cfg->client_cert_len ? (char *)net_cfg->client_cert : NULL,
                               net_cfg->client_key_len ? (char *)net_cfg->client_key : NULL);
    } else {
        sx_user_mqtt_nontls_init(&mqtt_cfg);
    }

    /* Must run after both sx_sleep_manager_init() (above) and the MQTT
     * init call just above -- see is_modem_owned_by_sleep_manager()'s
     * doc-comment and sx_mqtt.h's modem_owned_elsewhere typedef doc-comment
     * for the real-hardware race this guards against. Without this,
     * sx_mqtt.c's escalate_recovery() calls modem->ops->start() at the
     * same time as sx_sleep_manager_wake_process()'s own power-on/attach
     * sequence, confusing the modem mid-attach and leaving MQTT stuck
     * disconnected after wake. */
    sx_user_mqtt_set_modem_owned_elsewhere_check(is_modem_owned_by_sleep_manager);

    /* Time sync (RTC set once from modem NITZ, falling back to GPS UTC) —
     * see app/user/time_sync/. board.modem/board.gps/board.rtc are all
     * already initialized by sx_board_init() before app_init() runs. */
    time_sync_init(&s_time_sync, &board.modem, &board.gps, &board.rtc);

    /* UART6 console CLI ("settings -i/-c", "restart", "help") — see
     * app/user/shell_app/. Placed after network_config_init() so the
     * shell always has a live, already-loaded config to read/write from
     * the moment it becomes usable. Uses board.log_uart (UART6) as the
     * transport, not USB, because this board revision has no USB
     * connector — shell I/O shares the wire with log output, see
     * shell_app.h's caveat comment. */
    shell_app_init(&board.log_uart);

    s_cycle_state   = APP_CYCLE_ON_PUMP;
    s_cycle_tick_ms = 0;
    s_app_mode      = APP_MODE_FULL_POWER;
}
void app_process(uint32_t delta_ms){
    /* IWDG (~30s timeout, see iwdg.c / main.c's
     * ensure_iwdg_frozen_in_stop_option_byte()) must be refreshed here,
     * every tick, but ONLY while the board is actually awake and running
     * (FULL_POWER or WAKEUP) — NOT during APP_MODE_ENTER_SLEEP, since that
     * branch below is what blocks inside sx_sleep_manager_enter_sleep()
     * for the whole STOP-mode duration; refreshing unconditionally here
     * would be a no-op while parked anyway (execution is paused), but
     * gating it explicitly documents intent and matches the "watchdog
     * only lives during full-power/wakeup" requirement. If FULL_POWER
     * work (app_cycle_process(), MQTT recovery ladder retries, etc.) ever
     * blocks longer than ~30s without returning here, IWDG resets the
     * board — that is the protection working as designed, not a bug. */
    if (s_app_mode == APP_MODE_FULL_POWER || s_app_mode == APP_MODE_WAKEUP ||
        s_app_mode == APP_MODE_HB_ONLY) {
        HAL_IWDG_Refresh(&hiwdg);
    }

    /* Only the sensors that need per-tick driving from the app layer.
     * gas_sensor_app_poll() must run every tick regardless of the main
     * app state machine so ZE12A's mux round-robin + byte assembly keeps
     * advancing and its GAS_SENSOR_TIMEOUT_MS connection-loss detection
     * stays accurate, the same reasoning as gps_process()/sim polling
     * elsewhere in this project running unconditionally every tick. */
    gas_sensor_app_poll(delta_ms);

    /* Same reasoning as gas_sensor_app_poll() above. */
    accel_app_poll(&s_accel_app, delta_ms);
    sx_temp_humi_poll(&s_temp_humi, delta_ms);
    power_monitor_app_poll(&s_power_monitor, delta_ms);
    /* Was missing entirely before — GPS must keep parsing NMEA sentences
     * every tick regardless of app_mode/cycle state, same reasoning as
     * every other "*_poll() every tick" call here (mirrors WS_v0's
     * reading_sensor_task calling gnss_poll(1) unconditionally). */
    gps_process(&board.gps, delta_ms);

    /* 0->1 fix edge detect (see s_gps_was_fixed/gps_log_save_fix()'s
     * doc-comments above) — must run every tick right after gps_process()
     * so a fix acquired mid-cycle (not just at SENDING) is caught and
     * saved immediately, per the user's request. */
    {
        bool is_fixed_now = (board.gps.latitude != 0.0f && board.gps.longtitude != 0.0f);
        if (is_fixed_now && !s_gps_was_fixed) {
            gps_log_save_fix(board.gps.latitude, board.gps.longtitude);
        }
        s_gps_was_fixed = is_fixed_now;
    }

    /* RTC time sync from modem NITZ (falling back to GPS) -- must run every
    * tick like the other *_poll() calls above, or the RTC never gets set
    * and format_timestamp() keeps reading garbage reset-state time. */
    time_sync_poll(&s_time_sync);

    /* Plain MQTT poll, not thingsboard_client_poll() — see app_init()'s
     * comment on why Thingsboard isn't used yet. */
    sx_user_mqtt_poll(delta_ms);

    /* UART6 console CLI — same "every tick regardless of app_mode"
     * reasoning as the other *_poll() calls above. No-op while the
     * board is actually parked in STOP mode (execution is paused then
     * anyway). */
    shell_app_poll();

    /* Main cycle only runs in APP_MODE_FULL_POWER. The other app_mode_t
     * states drive the sleep/wake transition below:
     *
     *  - APP_MODE_ENTER_SLEEP: set once by app_cycle_process()'s SENDING
     *    case right after publish. sleep_ms is split into chunks of
     *    heartbeat_ms (2026-08-10, per the user: heartbeat every 15 min,
     *    telemetry every 30 min, independently runtime-configurable). The
     *    FIRST chunk of a lap runs the full sx_sleep_manager_enter_sleep()
     *    (7 sleep_steps: GPS/modem/SPS30/pump/ZE12A/accel power-down, then
     *    STOP) exactly as before, since those peripherals are still on
     *    from ON_PUMP/SENSING/SENDING. Every chunk AFTER the first uses
     *    sx_sleep_manager_bare_sleep() instead (RTC wake + STOP only, no
     *    sleep_steps) since those peripherals are already parked. Each
     *    wake between chunks either: (a) the lap's sleep_ms total isn't
     *    reached yet -> runs APP_MODE_HB_ONLY (mini-wake: sensor check +
     *    heartbeat publish, see sx_sleep_manager.c's HB_ONLY block) and
     *    goes back into APP_MODE_ENTER_SLEEP for the next chunk; or
     *    (b) sleep_ms IS reached -> proceeds to APP_MODE_WAKEUP exactly as
     *    before, resuming the ordinary full wake sequence.
     *  - APP_MODE_HB_ONLY: drives sx_sleep_manager_hb_only_process() every
     *    tick until sx_sleep_manager_hb_only_is_done(), then returns to
     *    APP_MODE_ENTER_SLEEP for the next chunk.
     *  - APP_MODE_WAKEUP: drives sx_sleep_manager_wake_process() every
     *    tick until sx_sleep_manager_is_wake_done() reports the wake
     *    step sequence (GPS on, modem on + wait ready, gas sensor back to
     *    active mode, accel resume) has finished; app_cycle_process()'s
     *    APP_CYCLE_WAKING case then resets state and flips s_app_mode back
     *    to FULL_POWER, resuming the ON_PUMP->SENSING->SENDING lap. */
    if (s_app_mode == APP_MODE_FULL_POWER) {
        app_cycle_process(delta_ms);
    } else if (s_app_mode == APP_MODE_ENTER_SLEEP) {
        uint32_t sleep_ms     = network_config_get()->sleep_ms;
        uint32_t heartbeat_ms = network_config_get()->heartbeat_ms;

        /* heartbeat_ms >= sleep_ms (or 0, meaning "disabled"/misconfigured)
         * means there is no room for a mini-wake inside this lap at all --
         * fall back to the original single-shot behavior (heartbeat only
         * ever fires alongside telemetry at SENDING, once per lap), same
         * as before this feature existed. Guards against a 0ms or
         * degenerate chunk_ms below. */
        uint32_t remaining_ms = (sleep_ms > s_lap_slept_ms) ? (sleep_ms - s_lap_slept_ms) : 0;
        uint32_t chunk_ms;
        if (heartbeat_ms == 0 || heartbeat_ms >= sleep_ms) {
            chunk_ms = remaining_ms;
        } else {
            chunk_ms = (remaining_ms < heartbeat_ms) ? remaining_ms : heartbeat_ms;
        }

        log_info(TAG, "Entering sleep chunk: %lu ms (slept %lu/%lu ms so far this lap)",
                 (unsigned long)chunk_ms, (unsigned long)s_lap_slept_ms, (unsigned long)sleep_ms);

        /* Top off IWDG's countdown right before the blocking sleep call
         * below, whichever variant runs -- covers sx_sleep_manager_
         * enter_sleep()'s 7 sleep_steps on the first chunk, and is a
         * cheap no-op-equivalent refresh immediately before
         * sx_sleep_manager_bare_sleep()'s own STOP call on later chunks
         * (which does not call sx_sleep_service.c's pre_stop_refresh
         * itself -- see that function's doc-comment). */
        HAL_IWDG_Refresh(&hiwdg);

        if (!s_full_sleep_steps_done_this_lap) {
            sx_sleep_manager_enter_sleep(&s_sleep_mgr, chunk_ms / 1000);
            s_full_sleep_steps_done_this_lap = 1;
        } else {
            sx_sleep_manager_bare_sleep(&s_sleep_mgr, chunk_ms / 1000);
        }
        /* Execution resumes here after the RTC wakeup timer fires. */
        s_lap_slept_ms += chunk_ms;

        if (s_lap_slept_ms >= sleep_ms) {
            s_app_mode    = APP_MODE_WAKEUP;
            s_cycle_state = APP_CYCLE_WAKING;
            s_lap_slept_ms                    = 0;
            s_full_sleep_steps_done_this_lap  = 0;
            log_info(TAG, "Woke up - running wake sequence");
        } else {
            s_app_mode = APP_MODE_HB_ONLY;
            sx_sleep_manager_hb_only_start(&s_sleep_mgr);
        }
    } else if (s_app_mode == APP_MODE_HB_ONLY) {
        sx_sleep_manager_hb_only_process(&s_sleep_mgr, delta_ms);
        if (sx_sleep_manager_hb_only_is_done(&s_sleep_mgr)) {
            s_app_mode = APP_MODE_ENTER_SLEEP;
        }
    } else if (s_app_mode == APP_MODE_WAKEUP) {
        sx_sleep_manager_wake_process(&s_sleep_mgr, delta_ms);
        app_cycle_process(delta_ms);
    }
}