#ifndef __SX_MQTT_H
#define __SX_MQTT_H

#ifdef __cplusplus
extern "C"{
#endif

#include <stdint.h>
#include "modem_ops.h"

#define SX_MQTT_TOPIC_MAX_LEN       128U
#define SX_MQTT_MESSAGE_MAX_LEN     512U
#define SX_MQTT_TIMEOUT_CONF        2000U
#define SX_MQTT_TIMEOUT_ACCQ        2000U
#define SX_MQTT_TIMEOUT_CONN        8000U
#define SX_MQTT_TIMEOUT_DISC        5000U
#define SX_MQTT_TIMEOUT_PUB         3000U
#define SX_MQTT_TIMEOUT_SUB         5000U

/* NOTE: driver-level states (CMQTTSTART, cert upload, CSSLCFG, ACCQ,
 * CMQTTCFG version, CMQTTCONNECT...) all now live inside the concrete modem
 * driver's own state machine (e.g. a7677s_mqtt_state_t in a7677s.c), reached
 * through a single modem_ops_t.mqtt_connect() call. This service layer only
 * needs to track its own coarse-grained state, not the driver's internals —
 * that is the whole point of going through modem_ops_t rather than calling
 * driver-specific functions directly. Swapping to a different modem module
 * later only means writing a new driver; this file does not change. */
typedef enum{
    SX_MQTT_STATE_DISCONNECTED = 0,
    SX_MQTT_STATE_CONNECTING,    /* mqtt_connect() issued, waiting for cb */
    SX_MQTT_STATE_CONNECTED,
    SX_MQTT_STATE_DISCONNECTING, /* mqtt_disconnect() issued, waiting for cb */
    SX_MQTT_STATE_ERROR,
} sx_mqtt_state_t;

typedef struct sx_mqtt sx_mqtt_t;

typedef void (*sx_mqtt_on_connected_cb_t) (sx_mqtt_t *mqtt);

typedef void (*sx_mqtt_on_disconnected_cb_t) (sx_mqtt_t *mqtt);

typedef void (*sx_mqtt_on_message_cb_t) (sx_mqtt_t *mqtt, const char *topic, const char *message);

typedef void (*sx_mqtt_on_publish_cb_t) (sx_mqtt_t *mqtt, int success);

typedef struct{
    const char *broker;
    uint16_t port;
    const char *client_id;
    const char *username;
    const char *password;
    uint16_t keepalive;
    uint8_t clean_session;
    uint8_t qos;
    uint8_t use_ssl;        //0 : tcp, 1: ssl
    uint8_t ssl_auth_mode; // 0 : no verify, 1 : sever cert, 2 = mutual
    char *ca_cert;        
    char *client_cert;    
    char *client_key;
}sx_mqtt_config_t;

typedef enum{
    CONF_CLIENTID = 0,
    CONF_URL,
    CONF_KEEPTIME,
    CONF_CLEANSS,
    CONF_USERNAME,
    CONF_PASSWORD,
    CONF_DONE,
} sx_mqtt_conf_step_t;

struct sx_mqtt{
    /* Handle into the concrete modem driver (e.g. &board.a7677s paired with
     * &a7677s_ops). This service layer never touches the driver except
     * through modem->ops->xxx(modem->ctx, ...) — see modem_ops.h. */
    modem_handle_t *modem;
    sx_mqtt_config_t config;
    sx_mqtt_state_t state;

    uint32_t reconnect_elapsed;
    uint32_t reconnect_delay;
    uint8_t  reconnect_count;

    /* Counts how many times the PWRKEY power-cycle recovery in do_error()
     * has itself been triggered without a successful connect happening in
     * between (i.e. reconnect_count reaching SX_MQTT_MAX_RETRY_BEFORE_RESTART
     * repeatedly). Only cleared on an actual CONNECTED transition (see
     * sx_mqtt_poll()) — unlike reconnect_count, which resets every time a
     * restart is triggered. Once this reaches
     * SX_MQTT_MAX_RESTARTS_BEFORE_HARD_RESET, do_error() escalates to
     * modem_ops_t.hard_reset() (physical RST pin) instead of another PWRKEY
     * cycle. */
    uint8_t  restart_cycle_count;

    /*  Callback    */
    sx_mqtt_on_connected_cb_t on_connected;
    sx_mqtt_on_disconnected_cb_t on_disconnected;
    sx_mqtt_on_message_cb_t on_message;
    sx_mqtt_on_publish_cb_t on_publish;
};

/*  Status helper   */
static inline sx_mqtt_state_t sx_mqtt_get_state(sx_mqtt_t *mqtt){
    return mqtt->state;
}

static inline uint8_t sx_mqtt_is_connected(sx_mqtt_t *mqtt){
    return (mqtt->state == SX_MQTT_STATE_CONNECTED);
}

/*  Callback    */
static inline void sx_mqtt_set_on_connected(sx_mqtt_t *mqtt, sx_mqtt_on_connected_cb_t cb){
    mqtt->on_connected = cb;
}

static inline void sx_mqtt_set_on_disconnected(sx_mqtt_t *mqtt, sx_mqtt_on_disconnected_cb_t cb){
    mqtt->on_disconnected = cb;
}

static inline void sx_mqtt_set_on_publish(sx_mqtt_t *mqtt, sx_mqtt_on_publish_cb_t cb){
    mqtt->on_publish = cb;
}

static inline void sx_mqtt_set_on_message(sx_mqtt_t *mqtt, sx_mqtt_on_message_cb_t cb){
    mqtt->on_message = cb;
}

static inline void sx_mqtt_force_disconnect(sx_mqtt_t *mqtt) {
    mqtt->state           = SX_MQTT_STATE_DISCONNECTED;
    mqtt->reconnect_count = 0;
    mqtt->reconnect_elapsed = 0;
}

/*  API */
void sx_mqtt_init(sx_mqtt_t *mqtt, modem_handle_t *modem);
void sx_mqtt_set_config(sx_mqtt_t *mqtt, const sx_mqtt_config_t *config);
int sx_mqtt_connect(sx_mqtt_t *mqtt);
int sx_mqtt_disconnect(sx_mqtt_t *mqtt);
int sx_mqtt_publish(sx_mqtt_t *mqtt, const char *topic, const char *message, uint8_t qos, uint8_t retain);
int sx_mqtt_subscribe(sx_mqtt_t *mqtt, const char *topic, uint8_t qos);
void sx_mqtt_poll(sx_mqtt_t *mqtt, uint32_t ts);
void sx_mqtt_report_failure(sx_mqtt_t *mqtt);

#ifdef __cplusplus
}
#endif

#endif