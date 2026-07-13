#include "sx_board.h"
#include "sx_user_mqtt.h"
#include "sx_gpio.h"
#include "sx_uart.h"
#include "sx_mqtt.h"
#include "usart.h"
#include "logger.h"
#include "string.h"
#include "cqueue.h"

/* NOTE: this file only ever touches the modem through board.modem
 * (a modem_handle_t — see modem_ops.h), never a concrete driver type. This
 * is what makes swapping to a different modem module later a one-file
 * change (rewrite the driver + its modem_ops_t, nothing here). board.modem
 * is expected to be added by the sx_board.c/.h refactor (next piece after
 * this file) as modem_handle_t modem = { .ops = &a7677s_ops,
 * .ctx = &board.a7677s }; this file does not know or care which concrete
 * driver backs it. */

static const char *TAG = "USER_MQTT";

#define MQTT_QUEUE_SIZE 16
#define MQTT_PUBLISH_MAX_RETRY 3

volatile sx_mqtt_t s_mqtt;

typedef struct {
    char topic[128];
    char message[256];
} mqtt_queue_item_t;

static mqtt_queue_item_t s_queue_buf[MQTT_QUEUE_SIZE];
static CQueue_t s_queue;

static mqtt_queue_item_t s_current_item;
static uint8_t s_publish_retry = 0;

static sx_user_mqtt_on_connected_cb_t    s_on_connected    = NULL;
static sx_user_mqtt_on_disconnected_cb_t s_on_disconnected = NULL;
static sx_user_mqtt_on_message_cb_t      s_on_message      = NULL;
static sx_user_mqtt_on_publish_cb_t      s_on_publish      = NULL;
static char s_client_id_buf[32];

// Poll timing
static uint32_t s_last_tick = 0;
static uint8_t s_initialized = 0;
static uint8_t s_publishing = 0;

static void dispatch_next(void){
    if (!cqueue_is_empty(&s_queue) && sx_mqtt_is_connected(&s_mqtt)) {
        if (cqueue_receive(&s_queue, &s_current_item)) {
            s_publishing = 1;
            sx_mqtt_publish(&s_mqtt, s_current_item.topic, s_current_item.message, 1, 0);
        }
    }
}

static void _on_connected(sx_mqtt_t *mqtt){
    (void)mqtt;
    log_info(TAG, "MQTT connected");
    if (s_on_connected) s_on_connected();
}

static void _on_disconnected(sx_mqtt_t *mqtt){
    (void)mqtt;
    log_warn(TAG, "MQTT disconnected");
    if (s_on_disconnected) s_on_disconnected();
}

static void _on_message(sx_mqtt_t *mqtt, const char *topic, const char *message){
    (void)mqtt;
    log_info(TAG, "MSG [%s] = %s", topic, message);
    if (s_on_message) s_on_message(topic, message);
}

static void _on_publish(sx_mqtt_t *mqtt, int success)
{
    (void)mqtt;
    s_publishing = 0;

    log_debug(TAG, "Publish %s", success ? "OK" : "FAIL");

    if (!success) {
        /* NOTE: s_publish_retry is intentionally NOT reset here — it must
         * accumulate across consecutive failures for MQTT_PUBLISH_MAX_RETRY
         * to ever be reached. It is only cleared on a successful publish
         * (see the success branch below) or by the max-retry recovery path
         * itself. Previously this was reset to 0 unconditionally at the top
         * of this function before the increment below, which meant the
         * counter always went 0->1 on every failure and the max-retry
         * threshold was effectively unreachable through normal operation. */
        s_publish_retry++;
        log_warn(TAG, "Publish fail %d/%d", s_publish_retry, MQTT_PUBLISH_MAX_RETRY);
        if (s_publish_retry >= MQTT_PUBLISH_MAX_RETRY) {
            log_error(TAG, "Max retry — reporting to sx_mqtt.c's recovery ladder");
            s_publish_retry = 0;
            cqueue_init_static(&s_queue, s_queue_buf, MQTT_QUEUE_SIZE,
                               sizeof(mqtt_queue_item_t));
            if (s_on_publish) s_on_publish(0);
            /* Let sx_mqtt.c decide the actual recovery action (it may
             * already be mid-PWRKEY-cycle from an unrelated connect
             * failure) — this file no longer calls board.modem.ops->start()
             * directly, see sx_mqtt_report_failure()'s doc-comment. */
            sx_mqtt_report_failure(&s_mqtt);
            return;
        }
        s_publishing = 1;
        sx_mqtt_publish(&s_mqtt, s_current_item.topic,
                        s_current_item.message, 1, 0);
        return;
    }

    s_publish_retry = 0;
    dispatch_next();
    if (s_on_publish) s_on_publish(1);
}

/*
 * modem_ready_cb_t / modem_error_cb_t (modem_ops.h) — registered once via
 * board.modem.ops->set_on_ready/set_on_error in _common_init(). These fire
 * exactly once per is_ready() transition (see a7677s_poll()'s edge
 * detection), so there is no risk of firing sx_mqtt_connect() repeatedly
 * while already connecting/connected.
 *
 * ctx here is always &board.modem (the user_ctx passed to set_on_ready/
 * set_on_error below) — not the driver's own ctx — precisely so this file
 * never needs to know the concrete driver type to read it back.
 */
static void _on_modem_ready(void *ctx)
{
    modem_handle_t *modem = (modem_handle_t *)ctx;

    const char *imei = modem->ops->get_imei(modem->ctx);
    size_t imei_len = strlen(imei);
    const char *imei_suffix = imei_len >= 8 ? imei + imei_len - 8 : imei;
    snprintf(s_client_id_buf, sizeof(s_client_id_buf), "stm32h5_%s", imei_suffix);

    s_mqtt.config.client_id = s_client_id_buf;

    log_info(TAG, "Modem ready — IP=%s RSSI=%d client_id=%s",
             modem->ops->get_ip(modem->ctx), modem->ops->get_rssi(modem->ctx),
             s_client_id_buf);
    sx_mqtt_connect(&s_mqtt);
}

static void _on_modem_error(void *ctx)
{
    (void)ctx;
    log_error(TAG, "Modem error — feeding into recovery ladder");
    sx_mqtt_report_failure(&s_mqtt);
    if (s_on_disconnected) s_on_disconnected();
}

static int _validate_cfg(const sx_user_mqtt_cfg_t *cfg)
{
    // if (!cfg->apn)       { log_error(TAG, "Missing APN");      return -1; }
    if (!cfg->broker)    { log_error(TAG, "Missing broker");    return -1; }
    if (!cfg->port)      { log_error(TAG, "Missing port");      return -1; }
    if (!cfg->client_id) { log_error(TAG, "Missing client_id"); return -1; }
    return 0;
}

static int _common_init(const sx_user_mqtt_cfg_t *cfg, sx_mqtt_config_t *mqtt_cfg){
    if (_validate_cfg(cfg) != 0) return -1;
    /* save user callbacks */
    cqueue_init_static(&s_queue, s_queue_buf, MQTT_QUEUE_SIZE, sizeof(mqtt_queue_item_t));
    s_on_connected    = cfg->on_connected;
    s_on_disconnected = cfg->on_disconnected;
    s_on_message      = cfg->on_message;
    s_on_publish      = cfg->on_publish;

    /* APN is static per-device config, set once before start() — mirrors
     * the previous sim76xx_set_full_apn() call. This file deliberately does
     * NOT call the equivalent a7677s_set_full_apn() itself, since that is
     * not part of modem_ops_t (APN is driver-specific setup done once at
     * board init, not a per-call runtime parameter) and calling it here
     * would require knowing the concrete a7677s_t type. That call belongs
     * in sx_board.c, alongside where board.a7677s itself is declared — see
     * the handoff note left for the sx_board.c/.h refactor. */
    mqtt_cfg->broker        = cfg->broker;
    mqtt_cfg->port          = cfg->port;
    mqtt_cfg->client_id     = cfg->client_id;
    mqtt_cfg->username      = cfg->username;
    mqtt_cfg->password      = cfg->password;
    mqtt_cfg->keepalive     = cfg->keepalive ? cfg->keepalive : 60;
    mqtt_cfg->clean_session = cfg->clean_session;

    board.modem.ops->set_on_ready(board.modem.ctx, _on_modem_ready, &board.modem);
    board.modem.ops->set_on_error(board.modem.ctx, _on_modem_error, &board.modem);

    sx_mqtt_init(&s_mqtt, &board.modem);
    sx_mqtt_set_config(&s_mqtt, mqtt_cfg);
    sx_mqtt_set_on_connected   (&s_mqtt, _on_connected);
    sx_mqtt_set_on_disconnected(&s_mqtt, _on_disconnected);
    sx_mqtt_set_on_message     (&s_mqtt, _on_message);
    sx_mqtt_set_on_publish     (&s_mqtt, _on_publish);

    s_last_tick = sx_gettick();
    board.modem.ops->start(board.modem.ctx);
    log_info(TAG, "Init done — broker=%s:%u ssl=%d", cfg->broker, cfg->port, cfg->use_ssl);

    s_initialized = 1;
    return 0;
}

uint8_t sx_user_mqtt_is_connected(void) {
    return sx_mqtt_is_connected(&s_mqtt);
}

const char *sx_user_mqtt_get_ip(void) {
    return board.modem.ops->get_ip(board.modem.ctx);
}

const char *sx_user_mqtt_get_imei(void) {
    return board.modem.ops->get_imei(board.modem.ctx);
}

int sx_user_mqtt_get_rssi(void) {
    return board.modem.ops->get_rssi(board.modem.ctx);
}

int sx_user_mqtt_nontls_init(const sx_user_mqtt_cfg_t *cfg){
    static sx_mqtt_config_t mqtt_cfg;
    memset(&mqtt_cfg, 0, sizeof(mqtt_cfg));
    mqtt_cfg.use_ssl = 0;
    return _common_init(cfg, &mqtt_cfg);
}

int sx_user_mqtt_tls_init(const sx_user_mqtt_cfg_t *cfg, char *ca_cert, char *client_cert, char *client_key){
    static sx_mqtt_config_t mqtt_cfg;
    memset(&mqtt_cfg, 0, sizeof(mqtt_cfg));
    mqtt_cfg.use_ssl = 1;

    if (client_cert && client_key)
        mqtt_cfg.ssl_auth_mode = 2;
    else if (ca_cert)
        mqtt_cfg.ssl_auth_mode = 1;
    else
        mqtt_cfg.ssl_auth_mode = 0;

    mqtt_cfg.ca_cert     = ca_cert;
    mqtt_cfg.client_cert = client_cert;
    mqtt_cfg.client_key  = client_key;

    return _common_init(cfg, &mqtt_cfg);
}

void sx_user_mqtt_force_disconnect(void) {
    sx_mqtt_force_disconnect(&s_mqtt);
}

void sx_user_mqtt_publish(const char *topic, const char *message){
    if (!sx_mqtt_is_connected(&s_mqtt)) { log_warn(TAG, "publish: not connected"); return; }

    mqtt_queue_item_t item;
    strncpy(item.topic,   topic,   sizeof(item.topic)   - 1);
    strncpy(item.message, message, sizeof(item.message) - 1);
    item.topic  [sizeof(item.topic)   - 1] = '\0';
    item.message[sizeof(item.message) - 1] = '\0';

    if (!cqueue_send(&s_queue, &item)) {
        log_warn(TAG, "publish queue full, drop: %s", topic);
        return;
    }

    if (!s_publishing) {
        if (cqueue_receive(&s_queue, &s_current_item)) {
            s_publishing = 1;
            sx_mqtt_publish(&s_mqtt, s_current_item.topic, s_current_item.message, 1, 0);
        }
    }
}

void sx_user_mqtt_subscribe(const char *topic){
    if (!sx_mqtt_is_connected(&s_mqtt)) { log_warn(TAG, "subscribe: not connected"); return; }
    if (!topic) { log_warn(TAG, "subscribe: NULL topic"); return; }
    sx_mqtt_subscribe(&s_mqtt, topic, 1);
}

void sx_user_mqtt_stop(void){
    if (sx_mqtt_is_connected(&s_mqtt))
        sx_mqtt_disconnect(&s_mqtt);
}

void sx_user_mqtt_poll(uint32_t time_stamp){
    sx_mqtt_poll(&s_mqtt, time_stamp);
}

uint8_t sx_user_mqtt_is_initialized(void) { return s_initialized; }

/*
 * Stop the MQTT service and wait for it to actually disconnect.
 *
 * NOTE: this remains a blocking busy-wait loop, unlike the rest of this
 * architecture which is non-blocking/poll-based — kept as-is, not something
 * this refactor changes on its own initiative. sx_mqtt_disconnect() already
 * goes through modem_ops_t.mqtt_disconnect() -> the driver's own
 * CMQTTDISC/CMQTTREL/CMQTTSTOP sequence (see a7677s_mqtt_state_t's DISC/
 * REL/STOP states) and reports completion asynchronously via sx_mqtt_t's
 * state flipping to SX_MQTT_STATE_DISCONNECTED. There is no separate
 * "mqtt_stop" operation in modem_ops_t (confirmed with the user:
 * sx_mqtt_disconnect() alone replaces the old two-call
 * sim76xx_mqtt_disconnect()+sim76xx_mqtt_stop() sequence). This function
 * polls sx_mqtt_t.state directly instead of relying on a raw AT command
 * callback — cb_mqtt_stop_done/cb_mqtt_disc_done and the modem_t-typed
 * callback signature they used are gone, since modem_ops_t's mqtt_cb_t is
 * the only callback shape available now.
 */
void sx_user_mqtt_stop_service(void) {
    if (sx_mqtt_is_connected(&s_mqtt)) {
        sx_mqtt_disconnect(&s_mqtt);
    }

    uint32_t wait = 0;
    while (s_mqtt.state != SX_MQTT_STATE_DISCONNECTED && wait < 10000) {
        sx_mqtt_poll(&s_mqtt, 10);
        sx_delay_ms(10);
        wait += 10;
    }

    sx_mqtt_force_disconnect(&s_mqtt);

    log_info(TAG, "MQTT service stopped, wait=%lu", (unsigned long)wait);
}

uint8_t sx_user_mqtt_is_publishing(void) {
    return s_publishing;
}

uint8_t sx_user_mqtt_queue_empty(void) {
    return cqueue_is_empty(&s_queue);
}

void sx_user_mqtt_queue_flush(void) {
    cqueue_init_static(&s_queue, s_queue_buf, MQTT_QUEUE_SIZE, sizeof(mqtt_queue_item_t));
    s_publishing   = 0;
    s_publish_retry = 0;
    log_info(TAG, "Queue flushed");
}