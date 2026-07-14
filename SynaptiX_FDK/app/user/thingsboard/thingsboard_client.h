#ifndef THINGSBOARD_CLIENTS_H
#define THINGSBOARD_CLIENTS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "sx_user_mqtt.h"

/* Thin layer on top of sx_user_mqtt that speaks the Thingsboard-specific
 * dialect (topic layout, JSON telemetry/attribute conventions) — it does
 * not know about any concrete sensor (SPS30/SHT3x/ZE12A/...). Building the
 * JSON payload and deciding which channel index / topic family to use
 * stays the caller's job (app layer), same separation as sx_user_mqtt.c
 * not knowing about the concrete modem driver behind board.modem.
 *
 * IO_API / 4_20mA_API / THS_API (app_config.h) are still unassigned
 * templates inherited from another project — no fixed meaning yet, so no
 * per-sensor wrapper (e.g. a hypothetical thingsboard_client_publish_ths())
 * is added here. thingsboard_client_publish_channel() below is generic
 * over any of the three "topic_fmt %d" families; assigning a channel index
 * to a specific sensor is an app-layer decision made once that mapping is
 * confirmed, not baked into this file's API shape.
 */

/* Initializes the underlying MQTT layer for Thingsboard use. Internally
 * calls sx_user_mqtt_nontls_init() (non-TLS only, for now) — the app layer
 * only supplies broker/client_id/credentials via cfg, not the raw
 * sx_user_mqtt_cfg_t plumbing, mirroring how _common_init() in
 * sx_user_mqtt.c hides board.modem details from its own callers.
 * cfg->keepalive == 0 defaults to THINGSBOARD_KEEP_ALIVE_S rather than
 * sx_user_mqtt's own 60s default, since that constant exists specifically
 * for Thingsboard use (app_config.h). Returns 0 on success, matching
 * sx_user_mqtt_nontls_init()'s convention. */
int thingsboard_client_init(const sx_user_mqtt_cfg_t *cfg);

/* Drives the underlying sx_user_mqtt polling/state machine. Call once per
 * app tick, same cadence as sx_user_mqtt_poll() would otherwise need. */
void thingsboard_client_poll(uint32_t time_stamp);

uint8_t thingsboard_client_is_connected(void);

/* Publishes a pre-built JSON payload to the shared telemetry topic
 * (TELEMETRY_API). Caller is responsible for building valid Thingsboard
 * telemetry JSON, e.g. {"temperature":21.4,"humidity":55.2}. */
void thingsboard_client_publish_telemetry(const char *json_payload);

/* Publishes to one of the "%d"-indexed telemetry topic families (IO,
 * 4-20mA, THS — see TELEMETRY_IO_API/TELEMETRY_4_20mA_API/TELEMETRY_THS_API
 * in app_config.h). topic_fmt must be one of those macros (a printf-style
 * format string expecting exactly one %d); channel is substituted into it.
 * This is intentionally generic rather than one function per sensor type,
 * since those three families do not yet have a confirmed sensor mapping. */
void thingsboard_client_publish_channel(const char *topic_fmt, int channel,
                                         const char *json_payload);

/* Publishes a pre-built JSON payload of client attributes
 * (ATTRIBUTE_UPDATE_API). */
void thingsboard_client_publish_attribute(const char *json_payload);

/* Requests to unsubscribe/cleanly stop the Thingsboard MQTT session.
 * Thin passthrough to sx_user_mqtt_stop_service() (blocking wait for
 * disconnect — see that function's doc-comment in sx_user_mqtt.c for why
 * this remains a busy-wait unlike the rest of the poll-based
 * architecture). */
void thingsboard_client_stop(void);

#ifdef __cplusplus
}
#endif

#endif