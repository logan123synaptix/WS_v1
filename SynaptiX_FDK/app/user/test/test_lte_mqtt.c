#include "test_lte_mqtt.h"
#include "sx_board.h"
#include "sx_user_mqtt.h"
#include "logger.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "TEST_LTE_MQTT";

/* Test topic — deliberately NOT the real Thingsboard/station topics
 * (MQTT_STATION_DATA_TOPIC etc, app_config.h) since this test only
 * confirms the modem+MQTT client stack is alive end-to-end, not real
 * telemetry format/content. Confirmed with user, 2026-07-28. */
#define TEST_TOPIC          "synaptix/test/lte_mqtt"
#define TEST_PUBLISH_PERIOD_MS  5000U

static uint32_t s_publish_accum_ms = 0;
static uint32_t s_msg_count = 0;
static uint8_t  s_was_connected = 0; /* edge-detect for connect/disconnect log */

static void on_connected(void)
{
    log_info(TAG, "MQTT connected callback fired");
}

static void on_disconnected(void)
{
    log_warn(TAG, "MQTT disconnected callback fired");
}

static void on_message(const char *topic, const char *message)
{
    log_info(TAG, "MSG RX [%s] = %s", topic, message);
}

static void on_publish(int success)
{
    log_info(TAG, "Publish result: %s", success ? "OK" : "FAIL");
}

void test_lte_mqtt_init(void)
{
    log_info(TAG, "=== TEST LTE + MQTT (broker=%s client_id=%s) ===",
             MQTT_HOST_TEST, MQTT_CLIENTID_TEST);

    /* Hardcoded test config — bypasses network_config on purpose (see
     * test_lte_mqtt.h doc-comment). Public broker, plain TCP, no auth. */
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
        return;
    }

    log_info(TAG, "Init OK — waiting for modem power-on + network + MQTT handshake...");
}

void test_lte_mqtt_poll(uint32_t delta_ms)
{
    /* Drives modem power-on sequence, network registration, and MQTT
     * connect/publish/subscribe state machines — same call app.c's real
     * app_process() makes every tick. */
    sx_user_mqtt_poll(delta_ms);

    uint8_t connected = sx_user_mqtt_is_connected();

    if (connected && !s_was_connected) {
        log_info(TAG, "-> Now CONNECTED. IP=%s IMEI=%s RSSI=%d operator=%s",
                 sx_user_mqtt_get_ip(), sx_user_mqtt_get_imei(),
                 sx_user_mqtt_get_rssi(), sx_user_mqtt_get_operator());
    } else if (!connected && s_was_connected) {
        log_warn(TAG, "-> Now DISCONNECTED");
    }
    s_was_connected = connected;

    if (!connected) {
        return; /* nothing to publish yet */
    }

    s_publish_accum_ms += delta_ms;
    if (s_publish_accum_ms >= TEST_PUBLISH_PERIOD_MS) {
        s_publish_accum_ms = 0;
        s_msg_count++;

        char payload[64];
        snprintf(payload, sizeof(payload), "{\"seq\":%lu,\"hello\":\"WS_v1\"}",
                  (unsigned long)s_msg_count);

        log_info(TAG, "[TX] topic=%s payload=%s", TEST_TOPIC, payload);
        sx_user_mqtt_publish(TEST_TOPIC, payload);
    }
}