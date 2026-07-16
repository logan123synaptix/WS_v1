#include "mqtt_rpc.h"
#include "sx_user_mqtt.h"
#include "network_config.h"
#include "app_config.h"
#include "logger.h"
#include "cJSON.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "MQTT_RPC";

#define RPC_RESPONSE_BUFF_SIZE 256

void mqtt_rpc_init(void)
{
    sx_user_mqtt_subscribe(RPC_REQUEST_API);
    log_info(TAG, "Subscribed to %s", RPC_REQUEST_API);
}

/* Applies one "-flag": value pair from the params object, same flag
 * names/units (seconds for timing, matching shell_commands.c's CLI) so a
 * single mental model covers both config channels. Returns 0 on an
 * unrecognized flag (silently skipped, same as WS_v0's RPCHandle() did
 * for unknown params — unlike the CLI's "-i"/"-c" path, which aborts the
 * whole command on an unknown flag; RPC callers are programmatic and a
 * partial-apply-and-report result is more useful than an all-or-nothing
 * one for a machine caller). Returns 1 if the flag was recognized and
 * applied. */
static int apply_param(cJSON *params, const char *flag)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(params, flag);
    if (item == NULL) {
        return 0;
    }

    /* Timing flags: seconds in JSON, ms in network_config_t, same unit
     * convention as shell_commands.c's CLI. */
    if (strcmp(flag, "-pump") == 0) {
        if (!cJSON_IsNumber(item) || item->valueint <= 0) {
            log_warn(TAG, "-pump must be a positive number");
            return 1;
        }
        network_config_set_pump_on_ms((uint32_t)item->valueint * 1000U);
        log_info(TAG, "Set pump_on_ms to %d s", item->valueint);
        return 1;
    }
    if (strcmp(flag, "-sensing") == 0) {
        if (!cJSON_IsNumber(item) || item->valueint <= 0) {
            log_warn(TAG, "-sensing must be a positive number");
            return 1;
        }
        network_config_set_sensing_ms((uint32_t)item->valueint * 1000U);
        log_info(TAG, "Set sensing_ms to %d s", item->valueint);
        return 1;
    }
    if (strcmp(flag, "-sleep") == 0) {
        if (!cJSON_IsNumber(item) || item->valueint <= 0) {
            log_warn(TAG, "-sleep must be a positive number");
            return 1;
        }
        network_config_set_sleep_ms((uint32_t)item->valueint * 1000U);
        log_info(TAG, "Set sleep_ms to %d s", item->valueint);
        return 1;
    }

    /* Broker / APN string+number flags. */
    if (strcmp(flag, "-host") == 0) {
        if (cJSON_IsString(item)) {
            network_config_set_host(item->valuestring);
            log_info(TAG, "Set host to %s", item->valuestring);
        }
        return 1;
    }
    if (strcmp(flag, "-port") == 0) {
        if (cJSON_IsNumber(item)) {
            network_config_set_port((uint16_t)item->valueint);
            log_info(TAG, "Set port to %d", item->valueint);
        }
        return 1;
    }
    if (strcmp(flag, "-clientid") == 0) {
        if (cJSON_IsString(item)) {
            network_config_set_client_id(item->valuestring);
            log_info(TAG, "Set client_id to %s", item->valuestring);
        }
        return 1;
    }
    if (strcmp(flag, "-user") == 0) {
        if (cJSON_IsString(item)) {
            network_config_set_username(item->valuestring);
            log_info(TAG, "Set username");
        }
        return 1;
    }
    if (strcmp(flag, "-pass") == 0) {
        if (cJSON_IsString(item)) {
            network_config_set_password(item->valuestring);
            log_info(TAG, "Set password");
        }
        return 1;
    }
    if (strcmp(flag, "-keepalive") == 0) {
        if (cJSON_IsNumber(item)) {
            network_config_set_keepalive((uint16_t)item->valueint);
            log_info(TAG, "Set keepalive to %d s", item->valueint);
        }
        return 1;
    }
    if (strcmp(flag, "-apn") == 0) {
        if (cJSON_IsString(item)) {
            network_config_set_apn(item->valuestring);
            log_info(TAG, "Set apn to %s", item->valuestring);
        }
        return 1;
    }
    if (strcmp(flag, "-apnuser") == 0) {
        if (cJSON_IsString(item)) {
            network_config_set_apn_username(item->valuestring);
            log_info(TAG, "Set apn_username");
        }
        return 1;
    }
    if (strcmp(flag, "-apnpass") == 0) {
        if (cJSON_IsString(item)) {
            network_config_set_apn_password(item->valuestring);
            log_info(TAG, "Set apn_password");
        }
        return 1;
    }

    return 0;
}

static void publish_response(const char *result)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "result", result);

    char buff[RPC_RESPONSE_BUFF_SIZE];
    memset(buff, 0, sizeof(buff));
    cJSON_PrintPreallocated(root, buff, sizeof(buff), 0);
    cJSON_Delete(root);

    sx_user_mqtt_publish(RPC_RESPONSE_API, buff);
}

void mqtt_rpc_on_message(const char *topic, const char *message)
{
    if (strcmp(topic, RPC_REQUEST_API) != 0) {
        return;
    }

    log_info(TAG, "RPC received: %s", message);

    cJSON *root = cJSON_Parse(message);
    if (root == NULL) {
        log_error(TAG, "Failed to parse RPC JSON");
        publish_response("Invalid JSON");
        return;
    }

    cJSON *method = cJSON_GetObjectItemCaseSensitive(root, "method");
    if (!cJSON_IsString(method) || method->valuestring == NULL) {
        log_error(TAG, "Missing/invalid RPC method");
        cJSON_Delete(root);
        publish_response("Missing method");
        return;
    }

    if (strcmp(method->valuestring, "setParams") != 0) {
        log_warn(TAG, "Unknown RPC method: %s", method->valuestring);
        cJSON_Delete(root);
        publish_response("Unknown method");
        return;
    }

    cJSON *params = cJSON_GetObjectItemCaseSensitive(root, "params");
    if (!cJSON_IsObject(params)) {
        log_error(TAG, "Missing/invalid RPC params");
        cJSON_Delete(root);
        publish_response("Missing params");
        return;
    }

    /* Same flag set as shell_commands.c's "settings -c", applied
     * best-effort (unrecognized/malformed individual fields are skipped
     * and logged, not fatal to the whole request — see apply_param()'s
     * doc-comment). */
    static const char *const flags[] = {
        "-pump", "-sensing", "-sleep",
        "-host", "-port", "-clientid", "-user", "-pass", "-keepalive",
        "-apn", "-apnuser", "-apnpass",
    };
    int applied = 0;
    for (size_t i = 0; i < sizeof(flags) / sizeof(flags[0]); i++) {
        applied += apply_param(params, flags[i]);
    }
    cJSON_Delete(root);

    if (applied == 0) {
        log_warn(TAG, "No recognized params in setParams request");
        publish_response("No recognized params");
        return;
    }

    if (!network_config_save()) {
        log_error(TAG, "Failed to save settings to flash");
        publish_response("Failed to save to flash");
        return;
    }

    log_info(TAG, "Settings updated via MQTT RPC (%d field(s))", applied);
    publish_response("Settings updated");
}