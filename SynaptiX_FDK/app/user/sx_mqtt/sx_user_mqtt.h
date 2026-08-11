#ifndef SX_USER_MQTT_H
#define SX_USER_MQTT_H

#ifdef __cplusplus
extern "C"{
#endif

// #include "sx_mqtt.h"
#include <stdint.h>

/* NOTE: sx_user_mqtt_uart_feed() below is declared but was never
 * implemented in sx_user_mqtt.c, in the version inherited before this
 * refactor — not something introduced by moving from sim76xx to a7677s.
 * Left as-is, not touched, since its intended design is unknown; flagged
 * here for visibility rather than silently dropped or guessed at. */

typedef void (*sx_user_mqtt_on_connected_cb_t)   (void);
typedef void (*sx_user_mqtt_on_disconnected_cb_t)(void);
typedef void (*sx_user_mqtt_on_message_cb_t)     (const char *topic, const char *message);
typedef void (*sx_user_mqtt_on_publish_cb_t)     (int success);

typedef struct {
    /* GSM */
    char apn[20];
    char username_apn[20];
    char password_apn[20];
    /* Broker */
    const char *broker;
    uint16_t    port;
    const char *client_id;
    const char *username;       /* NULL = skip */
    const char *password;       /* NULL = skip */
    uint16_t    keepalive;      /* seconds, 0 = default 60s */
    uint8_t     clean_session;

    /* TLS */
    uint8_t     use_ssl;        /* 0 = TCP, 1 = SSL */
    uint8_t     ssl_auth_mode;  /* 0=no verify, 1=server cert, 2=mutual */

    /* Callbacks */
    sx_user_mqtt_on_connected_cb_t    on_connected;
    sx_user_mqtt_on_disconnected_cb_t on_disconnected;
    sx_user_mqtt_on_message_cb_t      on_message;
    sx_user_mqtt_on_publish_cb_t      on_publish;
} sx_user_mqtt_cfg_t;

/*  Helper  */
const char *sx_user_mqtt_get_ip      (void);
const char *sx_user_mqtt_get_imei    (void);
int sx_user_mqtt_get_rssi    (void);
const char *sx_user_mqtt_get_operator(void);
uint8_t sx_user_mqtt_is_connected(void);

/*  API */
int sx_user_mqtt_nontls_init(const sx_user_mqtt_cfg_t *cfg);
void sx_user_mqtt_publish(const char *topic, const char *message);
void sx_user_mqtt_subscribe(const char *topic);
void sx_user_mqtt_stop(void);
void sx_user_mqtt_poll(uint32_t time_stamp);
uint8_t sx_user_mqtt_is_initialized(void);
void sx_user_mqtt_uart_feed(uint8_t byte);
int sx_user_mqtt_tls_init(const sx_user_mqtt_cfg_t *cfg, char *ca_cert, char *client_cert, char *client_key);
void sx_user_mqtt_force_disconnect(void);
void sx_user_mqtt_stop_service(void);
uint8_t sx_user_mqtt_is_publishing(void);
uint8_t sx_user_mqtt_queue_empty(void);
void sx_user_mqtt_queue_flush(void);

/* BUG FIX (2026-08-11 hang, root-caused during ft/heartbeat debugging):
 * re-attempts dispatch of whatever is already sitting at the head of the
 * publish queue, WITHOUT enqueuing a new item. This exists because
 * dispatch_next() (sx_user_mqtt.c, static) is otherwise only ever called
 * from _on_publish() -- i.e. only after a publish actually reached the
 * modem and completed. If a publish is rejected immediately by the driver
 * (dce->mqtt_state momentarily not A7677S_MQTT_CONNECTED right after
 * connect, while mqtt_rpc_init()'s on_connected subscribe is still in
 * flight -- see a7677s_mqtt_publish()'s "not connected" check), the
 * existing fix in sx_user_mqtt_publish()/dispatch_next() re-queues the
 * rejected item and resets s_publishing to 0, but nothing then ever calls
 * dispatch_next() again on its own: _on_publish() never fires (nothing was
 * actually sent), so the re-queued item sits forever. Confirmed on real
 * hardware: sx_sleep_manager.c's HB_ONLY retry loop used to gate its own
 * retry on sx_user_mqtt_queue_empty() being true, which is now permanently
 * false after the first rejection (the rejected item IS the queue content),
 * so HB_ONLY's retry condition could never fire again either -- the mini-
 * wake state machine spun forever waiting for a condition that could never
 * become true, modem left powered on, board never returned to sleep. This
 * function lets a caller like that retry loop nudge the queue forward
 * (calls the same dispatch_next() the internal success path already uses)
 * without risking a duplicate heartbeat the way calling
 * sx_user_mqtt_publish() again would. No-op if the queue is empty or a
 * publish is already in flight. */
void sx_user_mqtt_dispatch_pending(void);

/* Forwards to sx_mqtt_set_modem_owned_elsewhere_check() (sx_mqtt.h) --
 * see that typedef's doc-comment for why this exists (sx_sleep_manager.c
 * and sx_mqtt.c independently calling modem->ops->start() during the same
 * wake, confirmed on real hardware 2026-07-29). Plain function-pointer
 * type here (not sx_mqtt_modem_owned_elsewhere_cb_t) so this header does
 * not need to include sx_mqtt.h, matching this file's existing
 * "no sx_mqtt.h in this header" choice. */
void sx_user_mqtt_set_modem_owned_elsewhere_check(uint8_t (*cb)(void));

#ifdef __cplusplus
}
#endif

#endif