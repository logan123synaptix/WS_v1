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
#include "ze12a.h"
#include "cJSON.h"
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
 *  3. Timing (pump-on duration, sensing duration, overall cycle period) is
 *     APP_PUMP_ON_MS/APP_SENSING_MS/APP_CYCLE_PERIOD_MS fixed #defines
 *     (app_config.h) rather than WS_v0's runtime-adjustable app_setting
 *     struct (loaded from flash, changeable via Thingsboard RPC). Per the
 *     user: fixed defaults for now, with USB CDC/MSC runtime override
 *     planned as later, separate work — see app_config.h's note at the
 *     APP_*_MS defines.
 *
 *  4. WS_v0's SENDING state falls back to saving telemetry JSON to SPIF/
 *     littlefs when the publish fails, for later retry. WS_v1 has no
 *     filesystem-on-flash equivalent wired up yet, so a failed publish here
 *     is just logged and dropped — no offline-queue fallback yet. Flagged
 *     as a gap, not silently "fixed" — matches WS_v0's intent, just not a
 *     feature WS_v1 has the infrastructure for yet.
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
    APP_CYCLE_IDLE = 0,
    APP_CYCLE_ON_PUMP,
    APP_CYCLE_SENSING,
    APP_CYCLE_SENDING,
} app_cycle_state_t;

static app_cycle_state_t s_cycle_state   = APP_CYCLE_IDLE;
static uint32_t          s_cycle_tick_ms = 0;
static volatile app_mode_t s_app_mode    = APP_MODE_FULL_POWER;

#define TELEMETRY_JSON_BUFF_SIZE 512
static char s_telemetry_json[TELEMETRY_JSON_BUFF_SIZE];

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

static void app_cycle_process(uint32_t delta_ms)
{
    switch (s_cycle_state) {
    case APP_CYCLE_IDLE:
        s_cycle_tick_ms += delta_ms;
        if (s_cycle_tick_ms >= (APP_CYCLE_PERIOD_MS - APP_PUMP_ON_MS - APP_SENSING_MS)) {
            s_cycle_tick_ms = 0;
            s_cycle_state = APP_CYCLE_ON_PUMP;
            pump_on(sx_board_get_pump_gpio());
            log_info(TAG, "Pump on");
        }
        break;

    case APP_CYCLE_ON_PUMP:
        s_cycle_tick_ms += delta_ms;
        if (s_cycle_tick_ms >= APP_PUMP_ON_MS) {
            s_cycle_tick_ms = 0;
            s_cycle_state = APP_CYCLE_SENSING;
            sps30_app_start_cycle(&s_sps30_app);
            log_info(TAG, "Sensing started");
        }
        break;

    case APP_CYCLE_SENSING:
        sps30_app_poll(&s_sps30_app, delta_ms);
        s_cycle_tick_ms += delta_ms;
        if (s_cycle_tick_ms >= APP_SENSING_MS) {
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
            sx_user_mqtt_publish("weatherstation/telemetry", payload);
            log_info(TAG, "Telemetry published: %s", payload);
        } else {
            /* No offline-queue fallback yet (unlike WS_v0's SPIF/littlefs
             * save-to-file-on-failure) — see the module doc-comment above,
             * point 4. */
            log_warn(TAG, "MQTT not connected, telemetry dropped: %s", payload);
        }
        sps30_app_reset(&s_sps30_app);
        s_cycle_state = APP_CYCLE_IDLE;
        break;
    }

    default:
        s_cycle_state = APP_CYCLE_IDLE;
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
    if (net_cfg->use_tls) {
        sx_user_mqtt_tls_init(&mqtt_cfg,
                               net_cfg->ca_cert_len ? (char *)net_cfg->ca_cert : NULL,
                               net_cfg->client_cert_len ? (char *)net_cfg->client_cert : NULL,
                               net_cfg->client_key_len ? (char *)net_cfg->client_key : NULL);
    } else {
        sx_user_mqtt_nontls_init(&mqtt_cfg);
    }

    s_cycle_state   = APP_CYCLE_IDLE;
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

    /* Plain MQTT poll, not thingsboard_client_poll() — see app_init()'s
     * comment on why Thingsboard isn't used yet. */
    sx_user_mqtt_poll(delta_ms);

    /* Main cycle only runs in APP_MODE_FULL_POWER — the other app_mode_t
     * states (ENTER_SLEEP/SLEEP/WAKE_PUBLISH) are not yet driven from
     * here, see the doc-comment above (point 6). */
    if (s_app_mode == APP_MODE_FULL_POWER) {
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