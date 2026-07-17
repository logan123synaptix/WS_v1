#ifndef MODEM_OPS_H
#define MODEM_OPS_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

/*
 * modem_ops.h
 *
 * Common interface (VTABLE) for every modem driver (sim76xx, a7677s, ...).
 * This is the only contract that the service layer (sx_mqtt.c,
 * sx_sleep_service.c, sx_user_mqtt.c) is allowed to know about. The service
 * layer must NOT include sim76xx.h or a7677s.h directly — every operation
 * must go through modem_ops_t.
 *
 * Design notes:
 * - No PSM (AT+CPSMS) in this interface. Reason: A7677S has no DTR pin wired
 *   to the STM32, so it cannot be woken up early while PSM is waiting out
 *   the T3412 cycle. Use enter_low_power/exit_low_power (AT+CFUN=0/1)
 *   instead.
 * - No power-ready signal based on a STATUS pin (A7677S has no STATUS pin
 *   wired to the STM32 either). Modem readiness must be confirmed via AT/OK
 *   response, handled internally by the concrete driver (e.g. through a
 *   POWER_WAIT_BOOT state in its power state machine).
 * - Every function is non-blocking. No function is allowed to call a
 *   blocking delay (sx_delay_ms). State must be advanced through poll().
 */

/* Result codes at the business/service layer (different from
 * modem_response_st_t in modem.h, which is the raw AT-command layer result —
 * this one is already interpreted by the driver). */
typedef enum {
    MODEM_OPS_OK = 0,
    MODEM_OPS_ERROR,
    MODEM_OPS_TIMEOUT,
    MODEM_OPS_BUSY          /* modem is busy, request rejected, retry later */
} modem_ops_result_t;

/* Common callback for MQTT operations (connect/disconnect/publish/subscribe).
 * ctx: context pointer passed in by the caller when invoking the function
 *      (e.g. the MQTT client), passed back unchanged when the callback
 *      fires. */
typedef void (*mqtt_cb_t)(modem_ops_result_t result, void *ctx);

/* Callback fired when an inbound MQTT message is fully received (i.e. after
 * the driver has accumulated all +CMQTTRXTOPIC/+CMQTTRXPAYLOAD fragments up
 * to +CMQTTRXEND). topic/payload are NUL-terminated but not guaranteed to
 * stay valid after the callback returns — copy out anything needed.
 * ctx: the user_ctx pointer passed to mqtt_set_callbacks(). */
typedef void (*mqtt_incoming_cb_t)(const char *topic, uint16_t topic_len,
                                    const char *payload, uint16_t payload_len,
                                    void *ctx);

/* Callback fired when the MQTT session drops asynchronously (+CMQTTCONNLOST
 * URC, or a PDN deactivation that the driver knows MQTT cannot survive).
 * ctx: the user_ctx pointer passed to mqtt_set_callbacks(). */
typedef void (*mqtt_connlost_cb_t)(void *ctx);

/* Callback used for enter_low_power / exit_low_power, since these are also
 * asynchronous operations (waiting for the AT+CFUN=0/1 response via
 * poll()). */
typedef void (*power_mode_cb_t)(modem_ops_result_t result, void *ctx);

/* Callback fired exactly once, the moment the driver's internal state
 * transitions into "ready" (rising edge — the driver must track its own
 * previous state and only fire on the 0->1 transition, never repeatedly
 * while already ready). Lets the caller react immediately (e.g. kick off
 * mqtt_connect()) instead of having to poll is_ready() every loop.
 * ctx: the user_ctx pointer passed to set_on_ready(). */
typedef void (*modem_ready_cb_t)(void *ctx);

/* Callback fired when the driver detects an unrecoverable error condition
 * outside of any in-flight operation (e.g. network deregistration, modem
 * stopped responding to AT during normal operation). Not related to a
 * specific mqtt_cb_t/power_mode_cb_t call already in flight — those still
 * report their own failure through their own callback.
 * ctx: the user_ctx pointer passed to set_on_error(). */
typedef void (*modem_error_cb_t)(void *ctx);

typedef struct {
    /* --- Power state machine (non-blocking, PWRKEY pulse + poll based) --- */

    /* Starts the power-on sequence: pulse PWRKEY, then confirm via AT/OK.
     * Does not block. Progress is advanced inside poll(). */
    void (*power_on_start)(void *ctx);

    /* Starts the power-off sequence (e.g. AT+CPOF or a long PWRKEY toggle,
     * depending on the concrete driver). Does not block. */
    void (*power_off_start)(void *ctx);

    /* true if the power state machine is mid-sequence (neither IDLE nor
     * READY). Callers use this to decide whether it is safe to send other
     * commands yet. */
    bool (*power_is_busy)(void *ctx);

    /* Last-resort hard reset via the module's physical RST pin (if wired on
     * this board revision), bypassing the graceful AT+CPOF/PWRKEY shutdown
     * sequence entirely. Non-blocking — pulses the pin for the manufacturer-
     * recommended duration, then the module re-boots on its own; progress
     * (including the post-reset AT/OK probe) is advanced inside poll(), same
     * as power_on_start(). Callers should treat this the same as a fresh
     * power_on_start() for sequencing purposes (e.g. wait for is_ready()/
     * set_on_ready() afterwards) — do not call start() immediately after
     * this returns.
     * Intended only as an escalation when the normal power_off_start()/
     * power_on_start() cycle has itself failed to recover the modem
     * repeatedly, or the modem has stopped responding to AT entirely — not
     * as a routine recovery path. A driver with no RST pin wired on a given
     * board revision may implement this as a no-op that simply falls back to
     * power_off_start()+power_on_start(); callers must not assume the RST
     * pin was actually pulsed. */
    void (*hard_reset)(void *ctx);

    /* Clears stale low-level UART/RX-buffer state (busy flag, receive
     * buffer index/contents) without touching power state, init state, or
     * any higher-level session/connection state. Needed specifically
     * because power-cycling (power_off_start()/power_on_start()/hard_reset())
     * does NOT by itself clear this state — only the driver's one-time
     * boot-time init function does, which callers outside the driver cannot
     * invoke again mid-runtime. Intended for callers such as a sleep/wake
     * manager that power-cycle the modem across a sleep period and need to
     * guarantee no stale byte count or half-received command survives into
     * the post-wake AT sequence, before calling start() again. Safe to call
     * at any time; does not send any AT command and completes synchronously. */
    void (*comm_reset)(void *ctx);

    /* --- Modem initialization after power is up --- */

    /* Runs the AT init sequence (network attach, APN, etc). Returns 0 if the
     * sequence was started successfully (this does not mean it is already
     * ready), <0 if it could not be started (e.g. modem busy). Actual
     * progress must be tracked via is_ready(). */
    int (*start)(void *ctx);

    /* true once the modem has finished init and is ready for
     * network/MQTT use. */
    bool (*is_ready)(void *ctx);

    /* Registers a one-shot-per-transition callback fired when is_ready()
     * would flip from false to true. Not an async op of its own — just
     * stores the callback + user_ctx on the driver. Call once, any time
     * after modem_handle_t is set up (before or after start()). Passing
     * cb=NULL disables the notification (driver keeps polling internally
     * but does not fire anything). */
    void (*set_on_ready)(void *ctx, modem_ready_cb_t cb, void *user_ctx);

    /* Registers a callback fired when the driver detects it has dropped out
     * of the ready state due to an error condition (not a normal, expected
     * transition like entering low power). Passing cb=NULL disables it. */
    void (*set_on_error)(void *ctx, modem_error_cb_t cb, void *user_ctx);

    /* --- Low power mode (replaces PSM) --- */

    /* AT+CFUN=0 — reduces power draw, no reboot needed on resume.
     * Used for short sleep cycles. Non-blocking, result delivered via cb. */
    int (*enter_low_power)(void *ctx, power_mode_cb_t cb);

    /* AT+CFUN=1 — restores full functionality. Non-blocking. */
    int (*exit_low_power)(void *ctx, power_mode_cb_t cb);

    /* --- MQTT --- */

    /* All connection parameters are passed on every call so the caller
     * (sx_mqtt.c) can reconnect to a different broker or use a different
     * identity without knowing the concrete driver type.
     *
     * client_id: MQTT client identifier (AT+CMQTTACCQ). Must be unique per
     *   device/session. NULL is not allowed (MQTT spec requires a non-empty
     *   client ID for clean_session=0; most brokers also reject empty IDs).
     * use_ssl: 0 = plain TCP (broker URI built as "tcp://host:port"), the
     *   ca_cert/client_cert/client_key parameters below are ignored entirely
     *   in this case, regardless of what is passed in them.
     *   1 = TLS (broker URI built as "ssl://host:port"); the driver runs the
     *   AT+CSSLCFG/AT+CCERTDOWN sequence before AT+CMQTTCONNECT.
     * ca_cert/client_cert/client_key: only consulted when use_ssl is 1.
     *   PEM text (NUL-terminated), or NULL/"" if not used:
     *     - ca_cert NULL/"" and client_cert/client_key NULL/"": TLS with the
     *       module's own trust handling / no certs uploaded (server-auth-off
     *       or module-default trust, driver-dependent).
     *     - ca_cert set, client_cert/client_key NULL/"": server-authentication
     *       TLS only (the common HTTPS-like case).
     *     - all three set: mutual TLS (client authenticates to the broker
     *       too, e.g. AWS IoT Core).
     *   There is no separate "mutual TLS" flag — whether mutual auth happens
     *   is entirely determined by whether client_cert/client_key are supplied.
     * username/password: may be NULL or "" if the broker does not require
     *   authentication (the driver will skip sending credentials). */
    int (*mqtt_connect)(void *ctx, const char *client_id,
                         const char *broker, uint16_t port,
                         uint8_t use_ssl,
                         const char *ca_cert,
                         const char *client_cert,
                         const char *client_key,
                         uint16_t keepalive,
                         uint8_t clean_session, const char *username,
                         const char *password, mqtt_cb_t cb);
    int (*mqtt_disconnect)(void *ctx, mqtt_cb_t cb);
    int (*mqtt_publish)(void *ctx, const char *topic, const char *msg,
                         uint8_t qos, uint8_t retain, mqtt_cb_t cb);
    int (*mqtt_subscribe)(void *ctx, const char *topic, uint8_t qos,
                           mqtt_cb_t cb);

    /* Registers the callbacks used to deliver inbound MQTT messages and
     * connection-lost notifications. Not an async op (no cb param of its
     * own) — just stores the two function pointers on the driver's context.
     * Call once after modem_handle_t is set up, before mqtt_connect(). */
    void (*mqtt_set_callbacks)(void *ctx, mqtt_incoming_cb_t incoming_cb,
                                mqtt_connlost_cb_t connlost_cb,
                                void *user_ctx);

    /* --- Device info (cached values, updated internally by the driver;
     * these never block or send an AT command synchronously) --- */

    /* Returns the device IMEI (AT+CGSN), NUL-terminated, or "" if not yet
     * read (e.g. still waiting on start() to finish). The string is read
     * once and cached — it does not change for the lifetime of the modem,
     * so no callback/refresh mechanism is needed. Pointer remains valid for
     * the lifetime of ctx. */
    const char *(*get_imei)(void *ctx);

    /* Returns the last-known signal quality from AT+CSQ, as the raw "rssi"
     * value (0-31, 99 = unknown), refreshed periodically by the driver's own
     * poll() — this call never blocks or sends AT itself, it only reads the
     * cached value. */
    int (*get_rssi)(void *ctx);

    /* Returns the current PDP context IP address, NUL-terminated, or "" if
     * not attached yet. Cached after a successful start(); refreshed again
     * if the driver detects the PDN was re-attached. Pointer remains valid
     * for the lifetime of ctx. */
    const char *(*get_ip)(void *ctx);

    /* Returns the registered network operator name (AT+COPS?, long
     * alphanumeric format), NUL-terminated, or "" if not yet read (e.g.
     * still waiting on start() to finish) or if the read failed. Read once
     * during start()'s attach sequence and cached — same "cached, never
     * blocks" contract as get_imei/get_ip above. Pointer remains valid for
     * the lifetime of ctx. */
    const char *(*get_operator)(void *ctx);

    /* --- Network time (NITZ), read once during the AT init sequence ---
     *
     * The driver enables automatic network time (AT+CTZU=1) and reads it
     * back (AT+CCLK?) as a one-shot, non-fatal step of start()'s attach
     * sequence — same "cached, never blocks" contract as get_imei/get_ip
     * above, and same "does not block is_ready() on failure" treatment as
     * those two (NITZ support is optional and network-dependent per
     * 3GPP TS 24.008 — a driver/network that doesn't provide it must not
     * prevent the modem from otherwise becoming ready).
     *
     * A driver with no network-time capability at all (e.g. a future modem
     * that doesn't support NITZ) may implement get_time_synced() as an
     * always-false no-op; callers must treat that the same as "network
     * hasn't provided a valid time yet" and fall back to another time
     * source (e.g. GPS UTC) rather than assuming a bug. */

    /* True once a valid network time has actually been read back (i.e.
     * AT+CCLK? returned a real network-provided time, not the module's
     * factory-invalid default — see the concrete driver for how it tells
     * the two apart). False before that point, or if the network never
     * provides NITZ. Never blocks. */
    bool (*get_time_synced)(void *ctx);

    /* Fills *out with the last network-provided time (UTC — any network
     * timezone offset is already applied by the driver before this is
     * returned, so callers never need to interpret <zz> themselves).
     * Returns true and fills *out only if get_time_synced() is also true;
     * returns false (leaving *out untouched) otherwise. tm_isdst is always
     * set to 0 (NITZ carries a fixed UTC offset, not a DST flag). */
    bool (*get_synced_time)(void *ctx, struct tm *out);

    /* --- Mandatory poll, called continuously from the main loop --- */

    /* ts: current timestamp (ms). Each driver tracks its own elapsed time
     * internally via a waitElapsed field in its own struct (never a static
     * local variable). */
    void (*poll)(void *ctx, uint32_t ts);
} modem_ops_t;

/* Common handle: pairs ops (vtable) with ctx (pointer to the concrete
 * struct, e.g. sim76xx_t* or a7677s_t*). The service layer only ever holds
 * a modem_handle_t and does not need to know the real type of ctx. */
typedef struct {
    const modem_ops_t *ops;
    void *ctx;
} modem_handle_t;

/* Inline helpers so callers don't have to repeat
 * handle->ops->xxx(handle->ctx, ...) everywhere. Optional to use. */
static inline void modem_handle_poll(modem_handle_t *h, uint32_t ts) {
    h->ops->poll(h->ctx, ts);
}

static inline bool modem_handle_is_ready(modem_handle_t *h) {
    return h->ops->is_ready(h->ctx);
}

#ifdef __cplusplus
}
#endif
#endif /* MODEM_OPS_H */