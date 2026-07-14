#if USE_THINGSBOARD
#include "thingsboard_client.h"
#include "app_config.h"
#include "logger.h"
#include <stdio.h>

static const char *TAG = "TB_CLIENT";

static uint8_t s_initialized = 0;

int thingsboard_client_init(const sx_user_mqtt_cfg_t *cfg)
{
    if (!cfg) {
        log_error(TAG, "init: NULL cfg");
        return -1;
    }

    /* Copy so we can apply the Thingsboard-specific keepalive default
     * without mutating the caller's struct. */
    sx_user_mqtt_cfg_t local_cfg = *cfg;
    if (local_cfg.keepalive == 0) {
        local_cfg.keepalive = THINGSBOARD_KEEP_ALIVE_S;
    }

    int rc = sx_user_mqtt_nontls_init(&local_cfg);
    if (rc != 0) {
        log_error(TAG, "init: sx_user_mqtt_nontls_init failed (%d)", rc);
        return rc;
    }

    s_initialized = 1;
    log_info(TAG, "Initialized (broker=%s:%u keepalive=%us)",
             local_cfg.broker, local_cfg.port, local_cfg.keepalive);
    return 0;
}

void thingsboard_client_poll(uint32_t time_stamp)
{
    if (!s_initialized) {
        return;
    }
    sx_user_mqtt_poll(time_stamp);
}

uint8_t thingsboard_client_is_connected(void)
{
    return s_initialized ? sx_user_mqtt_is_connected() : 0;
}

void thingsboard_client_publish_telemetry(const char *json_payload)
{
    if (!s_initialized || !json_payload) {
        log_warn(TAG, "publish_telemetry: not initialized or NULL payload");
        return;
    }
    sx_user_mqtt_publish(TELEMETRY_API, json_payload);
}

void thingsboard_client_publish_channel(const char *topic_fmt, int channel,
                                         const char *json_payload)
{
    if (!s_initialized || !topic_fmt || !json_payload) {
        log_warn(TAG, "publish_channel: not initialized or NULL arg");
        return;
    }

    char topic[TOPIC_DEFAULT_SIZE];
    int n = snprintf(topic, sizeof(topic), topic_fmt, channel);
    if (n < 0 || (size_t)n >= sizeof(topic)) {
        log_warn(TAG, "publish_channel: topic truncated/format error (fmt=%s ch=%d)",
                 topic_fmt, channel);
        return;
    }

    sx_user_mqtt_publish(topic, json_payload);
}

void thingsboard_client_publish_attribute(const char *json_payload)
{
    if (!s_initialized || !json_payload) {
        log_warn(TAG, "publish_attribute: not initialized or NULL payload");
        return;
    }
    sx_user_mqtt_publish(ATTRIBUTE_UPDATE_API, json_payload);
}

void thingsboard_client_stop(void)
{
    if (!s_initialized) {
        return;
    }
    sx_user_mqtt_stop_service();
    s_initialized = 0;
}
#endif