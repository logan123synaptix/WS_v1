#ifndef MQTT_RPC_H
#define MQTT_RPC_H

#ifdef __cplusplus
extern "C" {
#endif

/* Remote config-set channel over plain MQTT, alongside the existing USB
 * CDC CLI (see app/user/shell_app/). Ported from WS_v0's app.c's
 * RPCHandle() pattern (method "setParams" + a flat "params" object of
 * "-flag": value pairs), re-targeted at network_config_t's setters the
 * same way shell_commands.c's "settings -c" already is — both channels
 * end up calling the same network_config_set_*()/network_config_save().
 *
 * Unlike WS_v0 (which used Thingsboard's RPC_REQUEST_API/RPC_RESPONSE_API
 * topic convention via its thingsboard-client.c layer), this subscribes
 * directly on plain sx_user_mqtt — WS_v1 does not use thingsboard_client
 * yet (see app_init()'s comment on why). The topic *prefixes*
 * (RPC_REQUEST_API/RPC_RESPONSE_API) are taken from app_config.h, but the
 * actual topic used on the wire is built at runtime as
 * "<prefix><device_id>" (network_config_get()->device_id) — see
 * build_rpc_request_topic()/build_rpc_response_topic() in mqtt_rpc.c —
 * so multiple stations sharing one broker don't collide. A device_id
 * change via CLI/RPC does not retroactively re-subscribe; per the user,
 * a manual restart is required for it to take effect, same as every
 * other network_config field.
 *
 * There is no polling function here — this module is purely reactive,
 * driven by sx_user_mqtt_cfg_t's on_message callback (wired in app_init()).
 * mqtt_rpc_init() only performs the topic subscription. */

/* Subscribes to RPC_REQUEST_API. Matches sx_user_mqtt_on_connected_cb_t
 * (void(*)(void)) and is wired as mqtt_cfg.on_connected in app_init(),
 * NOT called directly — sx_user_mqtt_nontls_init()/_tls_init() only start
 * the connect sequence asynchronously, so subscribing must wait for the
 * client to actually report connected (sx_user_mqtt_subscribe() is a
 * silent no-op otherwise). This also re-subscribes correctly on any
 * future reconnect, since on_connected fires again each time. */
void mqtt_rpc_init(void);

/* sx_user_mqtt_cfg_t's on_message callback. Wire this in directly:
 *   mqtt_cfg.on_message = mqtt_rpc_on_message;
 * Filters for topic == RPC_REQUEST_API itself (ignores any other message
 * the same callback might see arrive on a future subscription), so it is
 * safe to be the *only* on_message handler in the app even if more
 * subscriptions are added later — each would need its own topic check
 * added here, or this design revisited into a small dispatch table. */
void mqtt_rpc_on_message(const char *topic, const char *message);

#ifdef __cplusplus
}
#endif

#endif