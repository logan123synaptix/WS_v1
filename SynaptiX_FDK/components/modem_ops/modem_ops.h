#ifndef MODEM_OPS_H
#define MODEM_OPS_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/*
 * modem_ops.h
 *
 * Common interface (VTABLE) for every modem driver (sim76xx, a7677s, ...).
 * This is the only contract that the service layer (sx_mqtt.c,
 * sx_sleep_manager.c, sx_user_mqtt.c) is allowed to know about. The service
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

/* Callback used for enter_low_power / exit_low_power, since these are also
 * asynchronous operations (waiting for the AT+CFUN=0/1 response via
 * poll()). */
typedef void (*power_mode_cb_t)(modem_ops_result_t result, void *ctx);

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

    /* --- Modem initialization after power is up --- */

    /* Runs the AT init sequence (network attach, APN, etc). Returns 0 if the
     * sequence was started successfully (this does not mean it is already
     * ready), <0 if it could not be started (e.g. modem busy). Actual
     * progress must be tracked via is_ready(). */
    int (*start)(void *ctx);

    /* true once the modem has finished init and is ready for
     * network/MQTT use. */
    bool (*is_ready)(void *ctx);

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
     * username/password: may be NULL or "" if the broker does not require
     *   authentication (the driver will skip sending credentials). */
    int (*mqtt_connect)(void *ctx, const char *client_id,
                         const char *broker, uint16_t port,
                         uint8_t use_ssl, uint16_t keepalive,
                         uint8_t clean_session, const char *username,
                         const char *password, mqtt_cb_t cb);
    int (*mqtt_disconnect)(void *ctx, mqtt_cb_t cb);
    int (*mqtt_publish)(void *ctx, const char *topic, const char *msg,
                         uint8_t qos, uint8_t retain, mqtt_cb_t cb);
    int (*mqtt_subscribe)(void *ctx, const char *topic, uint8_t qos,
                           mqtt_cb_t cb);

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