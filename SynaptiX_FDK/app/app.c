#include "app.h"
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
#include "file_io.h"
#include <string.h>

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
 *  2. WS_v0's bump used PWM with a configurable duty cycle
 *     (bsp_pwm_set_duty/app_setting.dutyCyclePercent). WS_v1's pump driver
 *     (sx_pump.h) is on/off only (pump_on()/pump_off() via EN_PW_PUMP) — no
 *     PWM duty cycle exists on this board's pump circuit, so ON_PUMP here
 *     just does pump_on() at full drive.
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

#define TELEMETRY_JSON_BUFF_SIZE 512
static char s_telemetry_json[TELEMETRY_JSON_BUFF_SIZE];

/* ===================== offline telemetry queue =====================
 * Ported from WS_v0's push_last_telemetry_json()/its SENDING-state save-
 * on-fail block (SynaptiX/apps/app.c), re-targeted at this project's
 * sx_storage_* API (network_config.c's flash layer) for single-file ops
 * (write/read/delete/size) and direct lfs_dir_open()/_read()/_close()
 * calls (see offline_queue_save()'s note on why — file_io.h's opendir()/
 * readdir()/closedir() macros looked like the natural fit but don't
 * actually match lfs_dir_open()'s real signature) for the directory scan,
 * since sx_storage.h has no directory-iteration API of its own
 * (sx_storage_list_dir() only logs, returns nothing usable).
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
     * NOTE: file_io.h's opendir()/readdir()/closedir() macros were tried
     * here first but don't actually work — opendir(path) expands to
     * lfs_dir_open(CONFIG_LITTLEFS, path), which is missing the lfs_dir_t*
     * argument lfs_dir_open()'s real signature
     * (lfs_dir_open(lfs_t*, lfs_dir_t*, const char*)) requires, and the
     * macro doesn't return anything DIR*-shaped to hold across
     * readdir()/closedir() calls anyway. That's a pre-existing bug in
     * file_io.h, not touched here (unclear what else may already assume
     * its current shape) — calling lfs_dir_open/_read/_close directly
     * instead, same as WS_v0's push_last_telemetry_json() does. */
    lfs_dir_t dir;
    if (lfs_dir_open(CONFIG_LITTLEFS, &dir, OFFLINE_QUEUE_DIR) == LFS_ERR_OK) {
        struct lfs_info info;
        uint32_t count = 0;
        char oldest_name[LFS_NAME_MAX + 1] = {0};
        while (lfs_dir_read(CONFIG_LITTLEFS, &dir, &info) > 0) {
            if (info.type != LFS_TYPE_REG) {
                continue;
            }
            count++;
            if (oldest_name[0] == '\0') {
                strncpy(oldest_name, info.name, sizeof(oldest_name) - 1);
            }
        }
        lfs_dir_close(CONFIG_LITTLEFS, &dir);

        if (count >= OFFLINE_QUEUE_MAX_FILES && oldest_name[0] != '\0') {
            char path[96];
            snprintf(path, sizeof(path), "%s/%s", OFFLINE_QUEUE_DIR, oldest_name);
            log_warn(TAG, "Offline queue full (%lu files), dropping oldest: %s",
                     (unsigned long)count, path);
            sx_storage_delete(path);
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
    lfs_dir_t dir;
    if (lfs_dir_open(CONFIG_LITTLEFS, &dir, OFFLINE_QUEUE_DIR) != LFS_ERR_OK) {
        return 0;
    }

    struct lfs_info info;
    char name[LFS_NAME_MAX + 1] = {0};
    while (lfs_dir_read(CONFIG_LITTLEFS, &dir, &info) > 0) {
        if (info.type == LFS_TYPE_REG) {
            strncpy(name, info.name, sizeof(name) - 1);
            break;
        }
    }
    lfs_dir_close(CONFIG_LITTLEFS, &dir);

    if (name[0] == '\0') {
        return 0; /* queue empty */
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

    sx_user_mqtt_publish("weatherstation/telemetry", buf);
    log_info(TAG, "Resent queued telemetry: %s", path);
    sx_storage_delete(path);
    return 1;
}

/* Builds the telemetry JSON payload from whatever each sensor app's
 * *_is_ready()/*_is_connected()/*_has_*() getters currently report —
 * missing/not-yet-ready readings are emitted as JSON null, same
 * "don't fake a zero" convention as WS_v0's dataPayload(). */
static const char *build_telemetry_payload(void)
{
    cJSON *root = cJSON_CreateObject();

    if (sx_temp_humi_is_ready(&s_temp_humi)) {
        cJSON_AddNumberToObject(root, "temperature", sx_temp_humi_get_temperature(&s_temp_humi));
        cJSON_AddNumberToObject(root, "humidity", sx_temp_humi_get_humidity(&s_temp_humi));
    } else {
        cJSON_AddNullToObject(root, "temperature");
        cJSON_AddNullToObject(root, "humidity");
    }

    if (sps30_app_has_measurement(&s_sps30_app)) {
        const sps30_app_measurement_t *m = sps30_app_get_measurement(&s_sps30_app);
        cJSON_AddNumberToObject(root, "pm2_5", m->mc_2p5);
        cJSON_AddNumberToObject(root, "pm10", m->mc_10p0);
    } else {
        cJSON_AddNullToObject(root, "pm2_5");
        cJSON_AddNullToObject(root, "pm10");
    }

    /* Gas sensor channels — see gas_sensor_app.h: value is only meaningful
     * when is_connected() is true, so null it out otherwise rather than
     * publishing a stale/zeroed reading. */
    static const struct { GasSensorType_t type; const char *key; } gas_channels[] = {
        { GAS_SENSOR_CO,  "co"  },
        { GAS_SENSOR_SO2, "so2" },
        { GAS_SENSOR_NO2, "no2" },
        { GAS_SENSOR_O3,  "o3"  },
        { GAS_SENSOR_H2S, "h2s" },
    };
    for (size_t i = 0; i < sizeof(gas_channels) / sizeof(gas_channels[0]); i++) {
        if (gas_sensor_app_is_connected(gas_channels[i].type)) {
            cJSON_AddNumberToObject(root, gas_channels[i].key,
                                     gas_sensor_app_get_value(gas_channels[i].type));
        } else {
            cJSON_AddNullToObject(root, gas_channels[i].key);
        }
    }

    if (power_monitor_app_has_voltage_reading(&s_power_monitor)) {
        cJSON_AddNumberToObject(root, "railVoltage",
                                 power_monitor_app_get_rail_voltage_v(&s_power_monitor));
    } else {
        cJSON_AddNullToObject(root, "railVoltage");
    }
    if (power_monitor_app_has_current_reading(&s_power_monitor)) {
        cJSON_AddNumberToObject(root, "railCurrent",
                                 power_monitor_app_get_current_a(&s_power_monitor));
    } else {
        cJSON_AddNullToObject(root, "railCurrent");
    }

    cJSON_AddStringToObject(root, "motionState",
                             accel_app_is_movement_detected(&s_accel_app) ? "moving" : "stationary");

    /* GPS fix (see gps_process() call in app_process()) — WS_v0's
     * dataPayload() gated this on weatherstation.gps.isReady; sx_gps_t
     * here has no such flag (see board.gps in sx_board.h), so this uses
     * the same "non-zero lat/long" liveness check sx_sleep_manager.c's
     * _gps_wait_is_done() already uses for the same purpose. */
    if (board.gps.latitude != 0.0f && board.gps.longtitude != 0.0f) {
        cJSON_AddNumberToObject(root, "latitude", board.gps.latitude);
        cJSON_AddNumberToObject(root, "longitude", board.gps.longtitude);
    } else {
        cJSON_AddNullToObject(root, "latitude");
        cJSON_AddNullToObject(root, "longitude");
    }

    memset(s_telemetry_json, 0, sizeof(s_telemetry_json));
    cJSON_PrintPreallocated(root, s_telemetry_json, TELEMETRY_JSON_BUFF_SIZE, 0);
    cJSON_Delete(root);
    return s_telemetry_json;
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
             * kick the pump on exactly once. */
            pump_on(sx_board_get_pump_gpio());
            log_info(TAG, "Pump on");
        }
        if (s_cycle_tick_ms >= network_config_get()->pump_on_ms) {
            s_cycle_tick_ms = 0;
            s_cycle_state = APP_CYCLE_SENSING;
            sps30_app_start_cycle(&s_sps30_app);
            log_info(TAG, "Sensing started");
        }
        break;

    case APP_CYCLE_SENSING:
        sps30_app_poll(&s_sps30_app, delta_ms);
        s_cycle_tick_ms += delta_ms;
        if (s_cycle_tick_ms >= network_config_get()->sensing_ms) {
            s_cycle_tick_ms = 0;
            s_cycle_state = APP_CYCLE_SENDING;
            pump_off(sx_board_get_pump_gpio());
            log_info(TAG, "Pump off, sending data");
        }
        break;

    case APP_CYCLE_SENDING: {
        const char *payload = build_telemetry_payload();
        /* Plain MQTT publish, not thingsboard_client_publish_telemetry()
         * — see app_init()'s comment on why Thingsboard isn't used yet.
         * Topic is a fixed default for now (no Thingsboard-style topic
         * convention applies without a real Thingsboard broker); revisit
         * once network_config/CDC-MSC input can also carry a topic. */
        if (sx_user_mqtt_is_connected()) {
            /* Drain one backlog file first, same "one at a time" shape as
             * WS_v0's push_last_telemetry_json() — see its doc-comment
             * above build_telemetry_payload(). Done before publishing
             * this cycle's fresh reading so the backlog gets a turn every
             * cycle instead of only when nothing new needs sending. */
            offline_queue_resend_one();

            sx_user_mqtt_publish("weatherstation/telemetry", payload);
            log_info(TAG, "Telemetry published: %s", payload);
        } else {
            offline_queue_save(payload);
            log_warn(TAG, "MQTT not connected, telemetry queued: %s", payload);
        }
        sps30_app_reset(&s_sps30_app);
        s_cycle_state = APP_CYCLE_SLEEPING;
        s_app_mode    = APP_MODE_ENTER_SLEEP;
        /* app_process() below acts on APP_MODE_ENTER_SLEEP right after
         * this switch returns — see the comment there for why the
         * actual sx_sleep_manager_enter_sleep() call lives in
         * app_process() rather than here. */
        break;
    }

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

void app_init(void){
    log_info(TAG, "APP initializing ....");

    sps30_app_init(&s_sps30_app, sx_board_get_sps30_power_gpio());
    sx_temp_humi_init(&s_temp_humi, &board.sht3x);
    accel_app_init(&s_accel_app, &board.imu);
    power_monitor_app_init(&s_power_monitor, &board.ads1115);

    sx_sleep_manager_init(&s_sleep_mgr, &board.sleep, &board.modem, &board.gps,
                           &s_sps30_app, sx_board_get_pump_gpio(), &s_accel_app);

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
    mqtt_cfg.on_message   = mqtt_rpc_on_message;
    mqtt_cfg.on_connected = mqtt_rpc_init;
    if (net_cfg->use_tls) {
        sx_user_mqtt_tls_init(&mqtt_cfg,
                               net_cfg->ca_cert_len ? (char *)net_cfg->ca_cert : NULL,
                               net_cfg->client_cert_len ? (char *)net_cfg->client_cert : NULL,
                               net_cfg->client_key_len ? (char *)net_cfg->client_key : NULL);
    } else {
        sx_user_mqtt_nontls_init(&mqtt_cfg);
    }

    /* Time sync (RTC set once from modem NITZ, falling back to GPS UTC) —
     * see app/user/time_sync/. board.modem/board.gps/board.rtc are all
     * already initialized by sx_board_init() before app_init() runs. */
    time_sync_init(&s_time_sync, &board.modem, &board.gps, &board.rtc);

    /* USB CDC CLI ("settings -i/-c", "restart", "help") — see
     * app/user/shell_app/. Placed after network_config_init() so the
     * shell always has a live, already-loaded config to read/write from
     * the moment it becomes usable. */
    shell_app_init(&board.usb);

    s_cycle_state   = APP_CYCLE_ON_PUMP;
    s_cycle_tick_ms = 0;
    s_app_mode      = APP_MODE_FULL_POWER;
}
void app_process(uint32_t delta_ms){
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

    /* Plain MQTT poll, not thingsboard_client_poll() — see app_init()'s
     * comment on why Thingsboard isn't used yet. */
    sx_user_mqtt_poll(delta_ms);

    /* USB CDC CLI — same "every tick regardless of app_mode" reasoning
     * as the other *_poll() calls above. No-op while the board is
     * actually parked in STOP mode (execution is paused then anyway). */
    shell_app_poll();

    /* Main cycle only runs in APP_MODE_FULL_POWER. The other app_mode_t
     * states drive the sleep/wake transition below:
     *
     *  - APP_MODE_ENTER_SLEEP: set once by app_cycle_process()'s SENDING
     *    case right after publish. sx_sleep_manager_enter_sleep() is a
     *    BLOCKING call — it runs every registered sleep_step (GPS/modem
     *    power-down, SPS30 SHDLC-sleep+EN_PW_DUST-low, pump off, ZE12A to
     *    QA mode, accel suspend) and then parks the STM32 itself in STOP
     *    mode via tier 1. It does not return until the RTC wakeup timer
     *    fires (sleep_sec derived from APP_CYCLE_PERIOD_MS) and the MCU
     *    resumes — so this call itself is where "the whole board sleeps"
     *    actually happens; everything after it below is post-wake.
     *  - APP_MODE_WAKEUP: drives sx_sleep_manager_wake_process() every
     *    tick until sx_sleep_manager_is_wake_done() reports the wake
     *    step sequence (GPS on, modem on + wait ready, gas sensor back to
     *    active mode, accel resume) has finished; app_cycle_process()'s
     *    APP_CYCLE_WAKING case then resets state and flips s_app_mode back
     *    to FULL_POWER, resuming the ON_PUMP->SENSING->SENDING lap. */
    if (s_app_mode == APP_MODE_FULL_POWER) {
        app_cycle_process(delta_ms);
    } else if (s_app_mode == APP_MODE_ENTER_SLEEP) {
        {
            uint32_t sleep_ms = network_config_get()->sleep_ms;
            log_info(TAG, "Entering sleep for %lu ms", (unsigned long)sleep_ms);
            sx_sleep_manager_enter_sleep(&s_sleep_mgr, sleep_ms / 1000);
        }
        /* Execution resumes here after the RTC wakeup timer fires. */
        s_app_mode    = APP_MODE_WAKEUP;
        s_cycle_state = APP_CYCLE_WAKING;
        log_info(TAG, "Woke up - running wake sequence");
    } else if (s_app_mode == APP_MODE_WAKEUP) {
        sx_sleep_manager_wake_process(&s_sleep_mgr, delta_ms);
        app_cycle_process(delta_ms);
    }
}

void app_request_sleep(void)
{
    /* Not yet acted upon — see the doc-comment above (point 6) and
     * network_config.h's note on app_request_sleep() currently being
     * unused (its former tud_umount_cb() caller was removed per the
     * user). Logged so the request is at least visible/traceable while
     * this is being built out. */
    log_info(TAG, "app_request_sleep() called - sleep-on-request not implemented yet");
}

void app_sync_gps_log_to_disk(void)
{
    /* Not yet implemented — no GPS-log-to-flash storage wired up in this
     * app.c yet (see app_config.h's GPS_LOG_FILE_PATH, unused so far). */
    log_info(TAG, "app_sync_gps_log_to_disk() called - not implemented yet");
}