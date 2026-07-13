#include "sx_mqtt.h"
#include "logger.h"
#include "string.h"
#include "stdio.h"
#include "stdlib.h"

#define SX_MQTT_RECONNECT_DELAY_BASE    1000U
#define SX_MQTT_RECONNECT_DELAY_MAX     5000U
#define SX_MQTT_MAX_RETRY_BEFORE_RESTART 3U
#define SX_MQTT_MAX_RESTARTS_BEFORE_HARD_RESET 3U  /* consecutive PWRKEY
    power-cycle recoveries (each already SX_MQTT_MAX_RETRY_BEFORE_RESTART
    connect failures in a row) that themselves failed to reach a connected
    state, before escalating to modem_ops_t.hard_reset() (physical RST pin).
    Matches A7677S_MAX_RETRY's spirit (3 consecutive failures at a given
    layer before escalating one level up) but is a service-layer constant of
    its own, since this layer must not include any driver header (a7677s.h)
    to get it — see modem_ops_t's design note on the service layer only
    ever knowing modem_ops.h. */

static const char *TAG = "SX_MQTT";

/* modem_ops_t.mqtt_cb_t only carries (result, ctx) where ctx is the
 * driver's own context pointer (e.g. a7677s_t*), not a user_ctx we control
 * (see mqtt_op_done() in a7677s.c: cb(result, dce)). So, same as the old
 * sim76xx-based version, this service layer keeps a single static instance
 * pointer to recover "which sx_mqtt_t is this callback for". This mirrors
 * the pre-existing s_instance pattern; only one sx_mqtt_t is ever active at
 * a time in this project. */
static sx_mqtt_t *s_instance = NULL;

static void do_error(sx_mqtt_t *mqtt);

/* --- modem_ops_t.mqtt_cb_t callbacks (result, ctx) --- */
static void cb_connect_done(modem_ops_result_t result, void *ctx);
static void cb_disconnect_done(modem_ops_result_t result, void *ctx);
static void cb_publish_done(modem_ops_result_t result, void *ctx);
static void cb_subscribe_done(modem_ops_result_t result, void *ctx);

/* --- mqtt_incoming_cb_t / mqtt_connlost_cb_t (registered once via
 * mqtt_set_callbacks(), fired directly by the driver's own URC parsing —
 * see a7677s.c's urc line handler around +CMQTTRXEND/+CMQTTCONNLOST). --- */
static void on_incoming(const char *topic, uint16_t topic_len,
                        const char *payload, uint16_t payload_len, void *ctx);
static void on_connlost(void *ctx);

/*
 * Connect flow:
 *   sx_mqtt_connect() -> modem->ops->mqtt_connect(...) -> cb_connect_done()
 *
 * Everything that used to be a multi-step state machine here (CMQTTSTART,
 * cert upload, CSSLCFG, CMQTTACCQ, CMQTTCFG version, CMQTTCONNECT) now
 * happens inside the driver behind this single call. Swapping to a
 * different modem module later only means writing a new driver with the
 * same modem_ops_t contract — this file does not need to change.
 */

static void cb_connect_done(modem_ops_result_t result, void *ctx)
{
    (void)ctx;
    sx_mqtt_t *mqtt = s_instance;
    if (!mqtt) return;

    if (result != MODEM_OPS_OK) {
        log_error(TAG, "mqtt_connect failed (result=%d)", result);
        do_error(mqtt);
        return;
    }

    log_info(TAG, "MQTT connected");
    mqtt->state = SX_MQTT_STATE_CONNECTED;
    if (mqtt->on_connected) mqtt->on_connected(mqtt);
}

static void cb_disconnect_done(modem_ops_result_t result, void *ctx)
{
    (void)ctx;
    sx_mqtt_t *mqtt = s_instance;
    if (!mqtt) return;

    if (result != MODEM_OPS_OK)
        log_warn(TAG, "mqtt_disconnect failed (result=%d) — forcing disconnect", result);

    log_info(TAG, "MQTT disconnected");
    mqtt->state = SX_MQTT_STATE_DISCONNECTED;
    if (mqtt->on_disconnected) mqtt->on_disconnected(mqtt);
}

static void cb_publish_done(modem_ops_result_t result, void *ctx)
{
    (void)ctx;
    sx_mqtt_t *mqtt = s_instance;
    if (!mqtt) return;

    int ok = (result == MODEM_OPS_OK);
    if (ok) log_debug(TAG, "Publish OK");
    else    log_error(TAG, "Publish failed (result=%d)", result);

    if (mqtt->on_publish) mqtt->on_publish(mqtt, ok);
}

static void cb_subscribe_done(modem_ops_result_t result, void *ctx)
{
    (void)ctx;
    if (result == MODEM_OPS_OK)
        log_debug(TAG, "Subscribe OK");
    else
        log_error(TAG, "Subscribe failed (result=%d)", result);
}

/*
 * Inbound message / connection-lost callbacks — registered once in
 * sx_mqtt_init() via modem->ops->mqtt_set_callbacks(). The driver has
 * already fully reassembled +CMQTTRXSTART/+CMQTTRXTOPIC/+CMQTTRXPAYLOAD/
 * +CMQTTRXEND (including multi-fragment topics/payloads) by the time this
 * fires — this layer does not parse any AT/URC text anymore.
 */
static void on_incoming(const char *topic, uint16_t topic_len,
                        const char *payload, uint16_t payload_len, void *ctx)
{
    (void)ctx; (void)topic_len; (void)payload_len;
    sx_mqtt_t *mqtt = s_instance;
    if (!mqtt) return;

    log_debug(TAG, "RX topic='%s' payload='%s'", topic, payload);
    if (mqtt->on_message) mqtt->on_message(mqtt, topic, payload);
}

static void on_connlost(void *ctx)
{
    (void)ctx;
    sx_mqtt_t *mqtt = s_instance;
    if (!mqtt) return;

    log_warn(TAG, "MQTT connection lost");
    mqtt->state = SX_MQTT_STATE_DISCONNECTED;
    if (mqtt->on_disconnected) mqtt->on_disconnected(mqtt);
}

/*
 * Internal helpers
 */

/* Shared escalation ladder: reconnect_count -> (PWRKEY power-cycle) ->
 * restart_cycle_count -> (hard_reset via RST pin). Used both by do_error()
 * (an actual connect/operation failure reported by the driver) and by
 * sx_mqtt_report_failure() (repeated publish failures reported by
 * the app layer, sx_user_mqtt.c) — see that function's doc-comment for why
 * publish failures must feed into this same counter instead of running an
 * independent recovery path of their own. */
static void escalate_recovery(sx_mqtt_t *mqtt)
{
    mqtt->reconnect_count++;

    if (mqtt->reconnect_count >= SX_MQTT_MAX_RETRY_BEFORE_RESTART) {
        mqtt->reconnect_count = 0;
        mqtt->restart_cycle_count++;

        if (mqtt->restart_cycle_count >= SX_MQTT_MAX_RESTARTS_BEFORE_HARD_RESET) {
            log_error(TAG, "PWRKEY power-cycle recovery failed %u times in a row — "
                      "escalating to hard_reset() (RST pin)",
                      mqtt->restart_cycle_count);
            mqtt->restart_cycle_count = 0;
            mqtt->modem->ops->hard_reset(mqtt->modem->ctx);
        } else {
            log_warn(TAG, "Too many retries — restarting modem power/init sequence "
                     "(restart cycle %u/%u before hard_reset escalation)",
                     mqtt->restart_cycle_count, SX_MQTT_MAX_RESTARTS_BEFORE_HARD_RESET);
            /* Same recovery action as before (full power cycle + re-init), now
             * reached only through modem_ops_t so this file stays
             * driver-agnostic. */
            mqtt->modem->ops->power_off_start(mqtt->modem->ctx);
            mqtt->modem->ops->power_on_start(mqtt->modem->ctx);
            mqtt->modem->ops->start(mqtt->modem->ctx);
        }
    }
}

static void do_error(sx_mqtt_t *mqtt)
{
    mqtt->state = SX_MQTT_STATE_ERROR;
    mqtt->reconnect_delay = SX_MQTT_RECONNECT_DELAY_BASE;
    if (mqtt->reconnect_delay > SX_MQTT_RECONNECT_DELAY_MAX)
        mqtt->reconnect_delay = SX_MQTT_RECONNECT_DELAY_MAX;
    mqtt->reconnect_elapsed = 0;

    escalate_recovery(mqtt);

    log_warn(TAG, "Error — reconnect after: %lu ms (retry #%d)",
             (unsigned long)mqtt->reconnect_delay, mqtt->reconnect_count);
    if (mqtt->on_disconnected) mqtt->on_disconnected(mqtt);
}

/*
 * Public API
 */

void sx_mqtt_init(sx_mqtt_t *mqtt, modem_handle_t *modem)
{
    memset(mqtt, 0, sizeof(sx_mqtt_t));
    mqtt->modem = modem;
    mqtt->state = SX_MQTT_STATE_DISCONNECTED;
    s_instance  = mqtt;

    /* Register once, up front — the driver fires these directly out of its
     * own URC parsing, independent of any AT command currently in flight. */
    modem->ops->mqtt_set_callbacks(modem->ctx, on_incoming, on_connlost, mqtt);

    log_info(TAG, "init OK");
}

/* Lets other layers report a failure into this file's shared recovery
 * ladder (see escalate_recovery()) instead of running their own independent
 * power-cycle/start() decision. Two current callers:
 *   - sx_user_mqtt.c's _on_publish(), after MQTT_PUBLISH_MAX_RETRY
 *     consecutive publish failures.
 *   - sx_user_mqtt.c's _on_modem_error() (modem_ops_t.set_on_error callback),
 *     fired by the driver itself for: (a) the init-attach sequence never
 *     reaching ready in the first place (see a7677s.c's restart_init() gap
 *     fix), or (b) the periodic AT+CSQ heartbeat failing
 *     A7677S_MAX_RETRY times in a row while runtime-idle (see a7677s.c's
 *     csq_fail_count) — both are the driver's own signal that something is
 *     wrong, previously only logged and never coordinated with any recovery
 *     action.
 * Deliberately does NOT set mqtt->state or fire on_disconnected: none of
 * these failure reports by themselves prove the MQTT connection is down
 * (state is driven by the real connect/disconnect/connlost paths elsewhere
 * in this file) — this call only means "count this toward the shared
 * recovery ladder." */
void sx_mqtt_report_failure(sx_mqtt_t *mqtt)
{
    log_warn(TAG, "Failure reported — feeding into recovery ladder");
    escalate_recovery(mqtt);
}

void sx_mqtt_set_config(sx_mqtt_t *mqtt, const sx_mqtt_config_t *config)
{
    mqtt->config = *config;
    log_info(TAG, "config: broker=%s port=%u client_id=%s",
             config->broker, config->port, config->client_id);
}

int sx_mqtt_connect(sx_mqtt_t *mqtt)
{
    if (!modem_handle_is_ready(mqtt->modem)) {
        log_warn(TAG, "connect: modem not ready");
        return -1;
    }
    if (mqtt->state != SX_MQTT_STATE_DISCONNECTED &&
        mqtt->state != SX_MQTT_STATE_ERROR)
    {
        log_warn(TAG, "connect: already connected or in progress");
        return -2;
    }

    log_info(TAG, "Starting connect flow");
    mqtt->state = SX_MQTT_STATE_CONNECTING;
    return mqtt->modem->ops->mqtt_connect(
        mqtt->modem->ctx,
        mqtt->config.client_id,
        mqtt->config.broker, mqtt->config.port,
        mqtt->config.use_ssl,
        mqtt->config.ca_cert,
        mqtt->config.client_cert,
        mqtt->config.client_key,
        mqtt->config.keepalive,
        mqtt->config.clean_session,
        mqtt->config.username, mqtt->config.password,
        cb_connect_done);
}

int sx_mqtt_disconnect(sx_mqtt_t *mqtt)
{
    if (!sx_mqtt_is_connected(mqtt)) {
        log_warn(TAG, "disconnect: not connected");
        return -1;
    }
    mqtt->state = SX_MQTT_STATE_DISCONNECTING;
    return mqtt->modem->ops->mqtt_disconnect(mqtt->modem->ctx, cb_disconnect_done);
}

int sx_mqtt_publish(sx_mqtt_t *mqtt, const char *topic, const char *message,
                     uint8_t qos, uint8_t retain)
{
    if (!sx_mqtt_is_connected(mqtt)) {
        log_warn(TAG, "publish: not connected");
        return -1;
    }
    if (!topic || !message) return -1;
    return mqtt->modem->ops->mqtt_publish(mqtt->modem->ctx, topic, message,
                                           qos, retain, cb_publish_done);
}

int sx_mqtt_subscribe(sx_mqtt_t *mqtt, const char *topic, uint8_t qos)
{
    if (!sx_mqtt_is_connected(mqtt)) {
        log_warn(TAG, "subscribe: not connected");
        return -1;
    }
    if (!topic) return -1;
    return mqtt->modem->ops->mqtt_subscribe(mqtt->modem->ctx, topic, qos,
                                             cb_subscribe_done);
}

void sx_mqtt_poll(sx_mqtt_t *mqtt, uint32_t ts)
{
    modem_handle_poll(mqtt->modem, ts);

    if ((mqtt->state == SX_MQTT_STATE_ERROR || mqtt->state == SX_MQTT_STATE_DISCONNECTED) &&
        modem_handle_is_ready(mqtt->modem) && mqtt->reconnect_count > 0)
    {
        mqtt->reconnect_elapsed += ts;
        if (mqtt->reconnect_elapsed >= mqtt->reconnect_delay) {
            mqtt->reconnect_elapsed = 0;
            /* Do NOT increment reconnect_count here — it is already
             * incremented exactly once per real failure inside
             * escalate_recovery() (called from do_error() /
             * sx_mqtt_report_failure()). Incrementing it again here double-
             * counts every failure cycle, causing
             * SX_MQTT_MAX_RETRY_BEFORE_RESTART and, worse,
             * SX_MQTT_MAX_RESTARTS_BEFORE_HARD_RESET (physical RST pin) to
             * be reached at roughly half the intended number of real
             * failures. */
            log_info(TAG, "Reconnecting... (retry #%d)", mqtt->reconnect_count);
            sx_mqtt_connect(mqtt);
        }
    }

    if (mqtt->state == SX_MQTT_STATE_CONNECTED) {
        mqtt->reconnect_count = 0;
        mqtt->reconnect_delay = SX_MQTT_RECONNECT_DELAY_BASE;
        mqtt->restart_cycle_count = 0;
    }
}