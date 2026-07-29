#include "test_sleep.h"
#include "sx_board.h"
#include "sx_user_mqtt.h"
#include "sx_temp_humi.h"
#include "accel_app.h"
#include "sps30_app.h"
#include "sx_sleep_manager.h"
#include "sx_ex_rtc.h"
#include "cJSON.h"
#include "app_config.h"
#include "logger.h"
#include <string.h>

static const char *TAG = "TEST_SLEEP";

/* Test topic -- deliberately NOT the real station topics
 * (MQTT_STATION_DATA_TOPIC etc, app_config.h), same reasoning as
 * test_lte_mqtt.c's TEST_TOPIC: this is a bring-up/bench test, not real
 * telemetry. Confirmed pattern with user, 2026-07-29. */
#define TEST_SLEEP_TOPIC   "synaptix/test/sleep_cycle"

/* App-layer wrappers this test drives directly (same "owned here, not on
 * Board_t" reasoning as test_sht3x.c's s_th / test_imu.c's s_accel --
 * these are polling-cadence wrappers around board.sht3x/board.imu, not
 * raw driver state). sps30_app_t is ALSO needed here even though this
 * test never calls sps30_app_start_cycle() (no measurement is taken) --
 * per the user, EVERY module must actually be run through its real
 * sleep_step/wake_step so current draw on the bench reflects the real
 * sleep sequence, and sx_sleep_manager_init() requires a real
 * sps30_app_t* to wire sps30_app_sleep_step_start/is_done into
 * sleep_steps[2] (see sx_sleep_manager.c). */
static sx_temp_humi_t   s_th;
static accel_app_t      s_accel;
static sps30_app_t      s_sps30_app;
static sx_sleep_manager_t s_sleep_mgr;

/* Payload buffer -- sized like app.c's s_telemetry_json (TELEMETRY_JSON_
 * BUFF_SIZE, app_config.h), reused here rather than pulling that define
 * in since this test's payload is a different (simpler) shape. */
#define TEST_SLEEP_JSON_BUFF_SIZE 512
static char s_json_buf[TEST_SLEEP_JSON_BUFF_SIZE];

/* Simple state machine: publish once, then block in
 * sx_sleep_manager_enter_sleep() for SLEEP_TEST_TIME_MS, then run the
 * real wake_steps (GPS/modem/ZE12A/BNO055), then publish again --
 * repeat. Mirrors app.c's APP_MODE_ENTER_SLEEP / APP_MODE_WAKEUP split,
 * just with a single always-publish state instead of the full ON_PUMP->
 * SENSING->SENDING cycle (per the user: this test only needs to
 * exercise sleep/wake + publish, not the sensing cycle). */
typedef enum {
    TEST_SLEEP_STATE_PUBLISH = 0,
    TEST_SLEEP_STATE_ENTER_SLEEP,
    TEST_SLEEP_STATE_WAKING,
} test_sleep_state_t;

static test_sleep_state_t s_state = TEST_SLEEP_STATE_PUBLISH;
static uint8_t  s_mqtt_was_connected = 0;
static uint32_t s_cycle_count = 0;

static void on_connected(void)    { log_info(TAG, "MQTT connected callback fired"); }
static void on_disconnected(void) { log_warn(TAG, "MQTT disconnected callback fired"); }
static void on_message(const char *topic, const char *message)
{
    log_info(TAG, "MSG RX [%s] = %s", topic, message);
}
static void on_publish(int success)
{
    log_info(TAG, "Publish result: %s", success ? "OK" : "FAIL");
}

/* Wrapper matching sx_user_mqtt_set_modem_owned_elsewhere_check()'s plain
 * uint8_t(*)(void) callback signature -- sx_sleep_manager_is_waking()
 * itself needs &s_sleep_mgr, which a bare function pointer can't carry.
 * See sx_mqtt.h's modem_owned_elsewhere typedef doc-comment for why this
 * wiring exists: without it, sx_mqtt.c's reconnect/recovery-ladder logic
 * calls modem->ops->start() at the same time as this file's wake sequence
 * (via sx_sleep_manager_wake_process()), confusing the modem mid-attach.
 * Confirmed on real hardware, 2026-07-29. */
static uint8_t is_modem_owned_by_sleep_manager(void)
{
    return sx_sleep_manager_is_waking(&s_sleep_mgr);
}

/* Same "null if not ready/valid" convention as app.c's
 * build_telemetry_payload()/format_timestamp() -- see those functions'
 * doc-comments for the reasoning (don't fake a zero/stale reading). */
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

    snprintf(out, out_size, "%04u-%02u-%02uT%02u:%02u:%02uZ",
             2000U + t.year, t.month, t.day, t.hour, t.min, t.sec);
    return true;
}

/* Builds this test's payload. Fields, per the user's spec (2026-07-29):
 *   - GPS: only if a real fix is present (non-zero lat/long, same
 *     liveness check as app.c/sx_sleep_manager.c use elsewhere) --
 *     null otherwise, since this bench setup has no GPS attached.
 *   - IMU movement: boolean, from accel_app_is_movement_detected().
 *   - RTC time: ISO-8601 string, null if the RTC hasn't been validly
 *     set (VLF flag) yet.
 *   - Temperature/humidity: from SHT3x via sx_temp_humi, null until the
 *     first successful reading completes.
 *   - Gas sensor channels: always null -- no gas sensor hardware
 *     attached on this bench yet (per the user), and this test
 *     deliberately does not depend on gas_sensor_app being wired up. */
static const char *build_test_payload(void)
{
    cJSON *root = cJSON_CreateObject();

    cJSON_AddNumberToObject(root, "seq", (double)s_cycle_count);

    char timestamp[32];
    if (format_timestamp(timestamp, sizeof(timestamp))) {
        cJSON_AddStringToObject(root, "timestamp", timestamp);
    } else {
        cJSON_AddNullToObject(root, "timestamp");
    }

    if (sx_temp_humi_is_ready(&s_th)) {
        cJSON_AddNumberToObject(root, "temperature", sx_temp_humi_get_temperature(&s_th));
        cJSON_AddNumberToObject(root, "humidity", sx_temp_humi_get_humidity(&s_th));
    } else {
        cJSON_AddNullToObject(root, "temperature");
        cJSON_AddNullToObject(root, "humidity");
    }

    cJSON_AddBoolToObject(root, "moving", accel_app_is_movement_detected(&s_accel));

    /* Gas sensor -- no hardware attached yet, always null (per the user;
     * see doc-comment above and in test_sleep.h). */
    static const char *gas_keys[] = { "co", "so2", "no2", "o3", "h2s" };
    for (size_t i = 0; i < sizeof(gas_keys) / sizeof(gas_keys[0]); i++) {
        cJSON_AddNullToObject(root, gas_keys[i]);
    }

    if (board.gps.latitude != 0.0f && board.gps.longtitude != 0.0f) {
        cJSON_AddNumberToObject(root, "latitude", board.gps.latitude);
        cJSON_AddNumberToObject(root, "longitude", board.gps.longtitude);
    } else {
        cJSON_AddNullToObject(root, "latitude");
        cJSON_AddNullToObject(root, "longitude");
    }

    memset(s_json_buf, 0, sizeof(s_json_buf));
    cJSON_PrintPreallocated(root, s_json_buf, TEST_SLEEP_JSON_BUFF_SIZE, 0);
    cJSON_Delete(root);
    return s_json_buf;
}

void test_sleep_init(void)
{
    log_info(TAG, "=== TEST FULL SLEEP/WAKE CYCLE (sleep_ms=%lu) ===",
             (unsigned long)SLEEP_TEST_TIME_MS);

    /* Same MQTT bring-up as test_lte_mqtt.c -- already confirmed working
     * on hardware, reused verbatim here (broker/client id/callbacks) per
     * the user's instruction to publish "like test_lte_mqtt". */
    sx_user_mqtt_cfg_t cfg = {0};
    cfg.broker          = MQTT_HOST_TEST;
    cfg.port            = 1883;
    cfg.client_id       = MQTT_CLIENTID_TEST;
    cfg.username        = NULL;
    cfg.password        = NULL;
    cfg.keepalive       = 60;
    cfg.clean_session   = 1;
    cfg.use_ssl         = 0;
    cfg.on_connected    = on_connected;
    cfg.on_disconnected = on_disconnected;
    cfg.on_message      = on_message;
    cfg.on_publish      = on_publish;

    int ret = sx_user_mqtt_nontls_init(&cfg);
    if (ret != 0) {
        log_error(TAG, "sx_user_mqtt_nontls_init FAILED (ret=%d)", ret);
    }

    /* board.sht3x/board.imu/board.rtc/board.gps/board.modem/board.sleep
     * are already *_init()'d inside sx_board_init() (Core/Src/main.c) --
     * these calls only start each module's polling cadence / wire the
     * sleep-step tables on top, same "do not re-init the driver" rule
     * every other test_*.c file in this directory follows. */
    sx_temp_humi_init(&s_th, &board.sht3x);
    accel_app_init(&s_accel, &board.imu);
    sps30_app_init(&s_sps30_app, sx_board_get_sps30_power_gpio());

    /* Wires ALL 6 wake_steps + ALL 6 sleep_steps (GPS, modem, SPS30,
     * pump, ZE12A, BNO055) -- per the user: "phai sleep tat ca cac
     * module de toi do dong". No module is skipped/stubbed out here;
     * sx_sleep_manager.c's fixed 6+6 step tables are used exactly as
     * app.c's real cycle uses them. */
    sx_sleep_manager_init(&s_sleep_mgr, &board.sleep, &board.modem, &board.gps,
                           &s_sps30_app, sx_board_get_pump_pwm(), &s_accel);

    /* Wire the modem-ownership check so sx_mqtt.c's reconnect/recovery-
     * ladder logic defers to this file's wake sequence instead of also
     * calling modem->ops->start() during the same wake -- see
     * is_modem_owned_by_sleep_manager()'s doc-comment above and
     * sx_mqtt.h's modem_owned_elsewhere typedef. Must run after both
     * sx_user_mqtt_nontls_init() (creates s_mqtt) and
     * sx_sleep_manager_init() (creates s_sleep_mgr) above. */
    sx_user_mqtt_set_modem_owned_elsewhere_check(is_modem_owned_by_sleep_manager);

    s_state = TEST_SLEEP_STATE_PUBLISH;
    s_cycle_count = 0;

    log_info(TAG, "Init OK -- waiting for modem power-on + network + MQTT handshake...");
}

void test_sleep_poll(uint32_t delta_ms)
{
    /* Drives modem power-on/network registration/MQTT state machine,
     * same call test_lte_mqtt_poll()/app.c's app_process() make every
     * tick. Also drives the sensors this test publishes from. */
    sx_user_mqtt_poll(delta_ms);
    /* Only poll temp/humi + accel when NOT in the middle of a wake
     * sequence. accel_resume is wake_steps[5] (the last of 6 steps), so
     * BNO055 is still in SUSPEND mode until wake is fully done, and SHT3x
     * sits on the same I2C1 bus. Polling either mid-wake races the
     * wake_steps and produces spurious "read failed" spam -- not a real
     * bus fault. gps_process() is left unconditional here: unlike the I2C
     * reads above it does not emit failure warnings when the GPS hasn't
     * produced data yet, so this ordering concern doesn't apply to it. */
    if (s_state != TEST_SLEEP_STATE_WAKING) {
        sx_temp_humi_poll(&s_th, delta_ms);
        accel_app_poll(&s_accel, delta_ms);
    }
    gps_process(&board.gps, delta_ms);

    uint8_t connected = sx_user_mqtt_is_connected();
    if (connected && !s_mqtt_was_connected) {
        log_info(TAG, "-> Now CONNECTED. IP=%s IMEI=%s RSSI=%d operator=%s",
                 sx_user_mqtt_get_ip(), sx_user_mqtt_get_imei(),
                 sx_user_mqtt_get_rssi(), sx_user_mqtt_get_operator());
    } else if (!connected && s_mqtt_was_connected) {
        log_warn(TAG, "-> Now DISCONNECTED");
    }
    s_mqtt_was_connected = connected;

    switch (s_state) {
    case TEST_SLEEP_STATE_PUBLISH: {
        /* Wait for MQTT before publishing -- same gating test_lte_mqtt.c
         * uses ("if (!connected) return;"). On the very first lap after
         * boot this can take a while (modem power-on + network attach);
         * on subsequent laps the wake_steps already brought the modem
         * back up before this state is re-entered (see
         * TEST_SLEEP_STATE_WAKING below), so this should resolve fast. */
        if (!connected) {
            return;
        }

        s_cycle_count++;
        const char *payload = build_test_payload();
        log_info(TAG, "[TX] topic=%s payload=%s", TEST_SLEEP_TOPIC, payload);
        sx_user_mqtt_publish(TEST_SLEEP_TOPIC, payload);

        log_info(TAG, "Entering sleep for %lu ms (ALL modules) ...",
                 (unsigned long)SLEEP_TEST_TIME_MS);
        s_state = TEST_SLEEP_STATE_ENTER_SLEEP;
        break;
    }

    case TEST_SLEEP_STATE_ENTER_SLEEP: {
        /* Blocking call -- runs every sleep_step (GPS/modem power-down,
         * SPS30 SHDLC-sleep+EN_PW_DUST-low, pump off, ZE12A QA mode,
         * BNO055 suspend), sets the RTC wakeup timer, then parks the
         * MCU in STOP mode via tier 1. Does not return until the RTC
         * wakeup timer fires (SLEEP_TEST_TIME_MS later). */
        sx_sleep_manager_enter_sleep(&s_sleep_mgr, SLEEP_TEST_TIME_MS / 1000U);

        /* Execution resumes here after waking. */
        log_info(TAG, "Woke up - running wake sequence (ALL modules) ...");
        s_state = TEST_SLEEP_STATE_WAKING;
        break;
    }

    case TEST_SLEEP_STATE_WAKING: {
        sx_sleep_manager_wake_process(&s_sleep_mgr, delta_ms);
        if (sx_sleep_manager_is_wake_done(&s_sleep_mgr)) {
            sx_sleep_manager_reset_wake(&s_sleep_mgr);
            log_info(TAG, "Wake sequence complete -- publishing again");
            s_state = TEST_SLEEP_STATE_PUBLISH;
        }
        break;
    }

    default:
        s_state = TEST_SLEEP_STATE_PUBLISH;
        break;
    }
}