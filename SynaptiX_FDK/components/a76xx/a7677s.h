#ifndef A7677S_H
#define A7677S_H
#ifdef __cplusplus
extern "C" {
#endif

#include "modem.h"
#include "modem_ops.h"

/* Timeouts (ms) for the MQTT state machine.
 * AT+CMQTTSTART/STOP: MaxResponseTime per datasheet is 12000 ms.
 * AT+CMQTTCONNECT: no explicit max in datasheet; 30 s is conservative.
 * AT+CMQTTDISC: timeout param range is 0 or 60-180 s; we pass 120 s to the
 *   modem and give the AT layer 130 s so it never fires before the modem.
 * AT+CMQTTPUB: pub_timeout must be 60-180 s (datasheet 18.2.12); we use 60 s
 *   and give the AT layer 70 s so it never fires before the modem.
 * AT+CMQTTSUBTOPIC/CMQTTSUB prompt (">") and OK: short, reuse AT timeout. */
#define A7677S_TIMEOUT_MQTT_START    13000U
#define A7677S_TIMEOUT_MQTT_CONNECT  30000U
#define A7677S_TIMEOUT_MQTT_DISC     130000U
#define A7677S_TIMEOUT_MQTT_PUB      70000U
/* AT+CCERTDOWN and AT+CSSLCFG: MaxResponseTime 120000 ms per datasheet
 * sections 19.2.1/19.2.2. We give 121 s so the AT layer's own timeout never
 * races the modem's documented worst case. */
#define A7677S_TIMEOUT_CERT_CMD      121000U
#define A7677S_MQTT_PUB_TIMEOUT_S    60U     /* passed to AT+CMQTTPUB as <pub_timeout> */
#define A7677S_MQTT_DISC_TIMEOUT_S   120U    /* passed to AT+CMQTTDISC as <timeout> */
#define A7677S_MQTT_CLIENT_INDEX     0U      /* only one client used; index 0 */
/* AT+CMQTTCFG="version",<idx>,<value>: value is a SIMCom-internal code, NOT
 * the real MQTT protocol level printed anywhere in the datasheet by name.
 * Confirmed via SIMCom application examples (A76XX/SIM76xx family): 3 = MQTT
 * v3.1, 4 = MQTT v3.1.1. This project uses 3.1.1, which is accepted by
 * effectively all modern brokers (Mosquitto, EMQX, AWS IoT, HiveMQ, etc.) —
 * MQTT 5.0 is not supported by this AT command set at all (range is 3-4 only). */
#define A7677S_MQTT_PROTOCOL_VERSION 4U

/* Max lengths for MQTT dynamic command buffers. */
#define A7677S_MQTT_CLIENT_ID_MAX    129U    /* 1-128 bytes per datasheet + NUL */
#define A7677S_MQTT_BROKER_MAX       257U    /* "tcp://host:port" up to 256 bytes + NUL */
#define A7677S_MQTT_USERNAME_MAX     257U    /* up to 256 bytes + NUL */
#define A7677S_MQTT_PASSWORD_MAX     257U    /* up to 256 bytes + NUL */
#define A7677S_MQTT_TOPIC_MAX        1025U   /* 1-1024 bytes + NUL */
#define A7677S_MQTT_PAYLOAD_MAX      10241U  /* 1-10240 bytes + NUL */
#define A7677S_MQTT_DYN_CMD_MAX      384U    /* scratch buffer for built AT cmds */

/* TLS/certificate support (only used when a7677s_mqtt_connect() is called
 * with use_ssl=1). Per a76xx_at_cmd.md 19.2.2 (AT+CCERTDOWN), a single cert
 * file may be up to 10240 bytes; we size the buffers to that maximum since
 * the caller (sx_mqtt.c today, sx_user_mqtt.c indirectly) may hand in a
 * full PEM chain. ssl_ctx_index 0 is used exclusively (mirrors
 * A7677S_MQTT_CLIENT_INDEX being fixed at 0 - this driver only ever manages
 * a single MQTT client/SSL context, matching the board's single-modem,
 * single-broker design). */
#define A7677S_SSL_CTX_INDEX         0U
#define A7677S_CERT_PEM_MAX          10241U  /* 1-10240 bytes + NUL, per AT+CCERTDOWN */
#define A7677S_CERT_FILENAME_CA      "cacert.pem"
#define A7677S_CERT_FILENAME_CLIENT  "clientcert.pem"
#define A7677S_CERT_FILENAME_KEY     "clientkey.pem"

/* Timeouts (ms) for the power state machine and AT init sequence. */
#define A7677S_PULSE_HIGH_MS   50U
#define A7677S_PULSE_LOW_MS    500U
#define A7677S_BOOT_PROBE_MS   500U     /* interval between "AT" probes while waiting for boot */
#define A7677S_TIMEOUT_AT      2500U
#define A7677S_TIMEOUT_CPOF    5000U
#define A7677S_TIMEOUT_NETWORK 9000U    /* CGDCONT/CGAUTH/CGACT/COPS, per a76xx_at_cmd.md MaxResponseTime */
#define A7677S_TIMEOUT_CFUN    9000U    /* AT+CFUN=0/1, MaxResponseTime per a76xx_at_cmd.md section 3.2.1 */
#define A7677S_CREG_POLL_MS    2000U    /* interval between AT+CREG? polls while waiting for registration */
#define A7677S_MAX_RETRY       3U       /* consecutive AT-layer failures before restarting the attach sequence from AT */

/* Max retries while polling AT+CREG? for <stat> == 1 (home) or 5 (roaming).
 * Registration is handled by the baseband automatically; AT+CREG=1 only
 * enables the unsolicited report, it does NOT trigger/confirm registration
 * by itself (confirmed against a76xx_at_cmd.md section 4.2.1) — so unlike
 * sim76xx.c/WS_v0's A76xx.c (which move on right after "OK"), this driver
 * polls AT+CREG? afterwards and only proceeds once <stat> is actually 1 or 5. */
#define A7677S_CREG_MAX_POLL   15U

/* Max APN/username/password length, mirrors sx_user_mqtt_config_t sizing
 * (SynaptiX_FDK/app/user/sx_mqtt/sx_user_mqtt.h: apn[20], username_apn[20],
 * password_apn[20]) so a7677s_set_full_apn() can take those buffers as-is. */
#define A7677S_APN_MAX_LEN     20U

/* Non-blocking power state machine. A7677S has neither a STATUS pin nor a
 * DTR pin wired to the STM32 (confirmed hardware fact, see handoff notes),
 * so:
 *   - "ready" cannot be confirmed via GPIO -> confirmed by probing "AT"
 *     until "OK" is received (A7677S_PWR_WAIT_BOOT).
 *   - PSM cannot be used (no early wake via DTR) -> low power is handled via
 *     AT+CFUN=0/1 instead (see modem_ops_t.enter_low_power/exit_low_power).
 * This board revision also has no VBAT cutoff transistor wired for A7677S
 * (see modem_t.hasPowerPin in modem.h) — power off relies solely on
 * AT+CPOF / PWRKEY, never on powerPin.
 */
typedef enum {
    A7677S_PWR_IDLE = 0,      /* powered off, no sequence in progress */
    A7677S_PWR_PULSE_HIGH,    /* PWRKEY driven high, waiting A7677S_PULSE_HIGH_MS */
    A7677S_PWR_PULSE_LOW,     /* PWRKEY driven low, waiting A7677S_PULSE_LOW_MS */
    A7677S_PWR_WAIT_BOOT,     /* PWRKEY released high, probing "AT" until OK */
    A7677S_PWR_READY,         /* module confirmed responsive to AT */
    A7677S_PWR_OFF_WAIT,      /* AT+CPOF sent, waiting for OK/timeout */
} a7677s_power_state_t;

/* Non-blocking AT init / network attach sequence, run after power_state
 * reaches A7677S_PWR_READY. Order confirmed against two independent sources:
 *   - WS_v0/SynaptiX/board/A7677S/A76xx.c (a76xx_setup(), lines ~82-94):
 *     CGDCONT -> CGAUTH -> CGACT -> CREG -> COPS(auto) -> COPS? -> CGDCONT?
 *   - Documents/a76xx_at_cmd.md, syntax cross-checked per command (sections
 *     4.2.1 AT+CREG, 5.2.4 AT+CGACT, 5.2.5 AT+CGDCONT, 5.2.16 AT+CGAUTH).
 * Deviates from both references on one point: AT+CREG=1 only enables the
 * unsolicited registration report, it does not itself confirm registration
 * (a76xx_at_cmd.md 4.2.1) — WS_v0/sim76xx.c both move on right after "OK",
 * which is not a real confirmation. This driver adds an explicit
 * A7677S_INIT_CREG_POLL state that reads back AT+CREG? and checks <stat>. */
typedef enum {
    A7677S_INIT_IDLE = 0,
    A7677S_INIT_AT,             /* probe "AT", also used to restart the whole sequence on failure */
    A7677S_INIT_CGDCONT,        /* AT+CGDCONT=1,"IP","<apn>" */
    A7677S_INIT_CGAUTH,         /* AT+CGAUTH=1,<type>,"<passwd>","<user>" - skipped if no username/password */
    A7677S_INIT_CGACT,          /* AT+CGACT=1,1 */
    A7677S_INIT_CREG_SET,       /* AT+CREG=1 - enables the unsolicited report only */
    A7677S_INIT_CREG_POLL,      /* AT+CREG? - polled until <stat> == 1 or 5 */
    A7677S_INIT_COPS_SET,       /* AT+COPS=0 (automatic operator selection) */
    A7677S_INIT_COPS_QUERY,     /* AT+COPS? */
    A7677S_INIT_CGDCONT_QUERY,  /* AT+CGDCONT? - final read-back, then READY */
    A7677S_INIT_READY,          /* attach sequence complete, is_ready() true */
} a7677s_init_state_t;

/* Non-blocking MQTT state machine, driven by poll() after is_ready() is true.
 *
 * Connect sequence (plain TCP, use_ssl=0):
 *   MQTT_IDLE -> START -> ACCQ -> CFG_VERSION -> CONNECT -> CONNECTED
 *
 * Connect sequence (TLS, use_ssl=1) inserts a cert-upload/SSL-config block
 * between START and ACCQ. Each cert step is skipped entirely if the
 * corresponding cert string is NULL/"" (see a7677s_mqtt_connect() below):
 *   MQTT_IDLE -> START
 *     -> [CERT_CA_PROMPT -> CERT_CA_DATA]         (only if ca_cert set)
 *     -> [CERT_CLIENT_PROMPT -> CERT_CLIENT_DATA] (only if client_cert set)
 *     -> [CERT_KEY_PROMPT -> CERT_KEY_DATA]       (only if client_key set)
 *     -> SSLCFG_CACERT -> SSLCFG_CLIENTCERT -> SSLCFG_CLIENTKEY
 *     -> SSLCFG_AUTHMODE -> SSLBIND
 *   -> ACCQ -> CFG_VERSION -> CONNECT -> CONNECTED
 * CFG_VERSION always runs (TLS and plain TCP alike), right after ACCQ and
 * before CONNECT, per AT+CMQTTCFG's documented requirement of being called
 * after ACCQ and before CONNECT. Sets MQTT protocol level to 3.1.1
 * (A7677S_MQTT_PROTOCOL_VERSION = 4).
 * authmode sent in SSLCFG_AUTHMODE: 0 if no certs at all, 1 if only ca_cert
 * (server-authentication TLS), 2 if client_cert+client_key are also set
 * (mutual TLS). Matches AT+CSSLCFG="authmode",... per a76xx_at_cmd.md 19.2.1.
 *
 * Disconnect:       CONNECTED -> DISC -> REL -> STOP -> MQTT_IDLE
 * Publish:          CONNECTED -> PUB_TOPIC -> PUB_PAYLOAD -> PUB_SEND -> CONNECTED
 * Subscribe:        CONNECTED -> SUB_TOPIC -> SUB_SEND -> CONNECTED
 *
 * On any AT-layer failure in the connect sequence (including the cert/SSL
 * steps), the state machine rewinds to MQTT_IDLE and fires the caller's
 * callback with MODEM_OPS_ERROR so the service layer can retry.  Failures
 * during pub/sub fire the callback and return to CONNECTED (the TCP/TLS
 * session is still alive).
 *
 * Inbound data (subscribed topics) arrives as URCs at any time while
 * CONNECTED. The URCs are parsed inside poll() via the rx state machine:
 *   +CMQTTRXSTART  -> rx_state = RX_TOPIC
 *   +CMQTTRXTOPIC  -> copy topic bytes from UART, rx_state = RX_PAYLOAD
 *   +CMQTTRXPAYLOAD-> copy payload bytes from UART, rx_state = RX_END
 *   +CMQTTRXEND    -> fire incoming_cb, rx_state = RX_IDLE
 *   +CMQTTCONNLOST -> fire conn_lost_cb, mqtt_state = MQTT_IDLE
 * NOTE (not yet implemented as of this revision — see handoff): this rx
 * state machine and the two URC callbacks below are currently wired up in
 * the struct/registration function only; poll() does not yet parse these
 * URCs. Tracked separately, not part of this TLS change.
 *
 * NOTE: The A7677S URC for inbound data sends raw bytes immediately after the
 * URC header line (no extra prompt). The length is in the URC header so the
 * driver reads exactly that many bytes from the UART ring buffer. */
typedef enum {
    A7677S_MQTT_IDLE = 0,        /* not connected, no sequence running */
    /* --- connect sequence --- */
    A7677S_MQTT_START,           /* AT+CMQTTSTART sent, waiting URC +CMQTTSTART:0 */
    /* --- TLS-only: cert upload block, entered from START only if use_ssl=1,
     * each pair skipped if the corresponding cert string is NULL/"" --- */
    A7677S_MQTT_CERT_CA_PROMPT,      /* AT+CCERTDOWN="<name>",<len> sent, waiting ">" */
    A7677S_MQTT_CERT_CA_DATA,        /* CA cert PEM bytes sent, waiting OK */
    A7677S_MQTT_CERT_CLIENT_PROMPT,  /* AT+CCERTDOWN for client cert, waiting ">" */
    A7677S_MQTT_CERT_CLIENT_DATA,    /* client cert PEM bytes sent, waiting OK */
    A7677S_MQTT_CERT_KEY_PROMPT,     /* AT+CCERTDOWN for client key, waiting ">" */
    A7677S_MQTT_CERT_KEY_DATA,       /* client key PEM bytes sent, waiting OK */
    /* --- TLS-only: SSL context config, always run when use_ssl=1 (even if
     * no certs were uploaded, to set sslversion/authmode=0) --- */
    A7677S_MQTT_SSLCFG_CACERT,       /* AT+CSSLCFG="cacert",0,"<file|\"\">" */
    A7677S_MQTT_SSLCFG_CLIENTCERT,   /* AT+CSSLCFG="clientcert",0,"<file|\"\">" */
    A7677S_MQTT_SSLCFG_CLIENTKEY,    /* AT+CSSLCFG="clientkey",0,"<file|\"\">" */
    A7677S_MQTT_SSLCFG_AUTHMODE,     /* AT+CSSLCFG="authmode",0,<0|1|2> */
    A7677S_MQTT_SSLBIND,             /* AT+CMQTTSSLCFG=0,0 - binds ssl_ctx 0 to client 0 */
    A7677S_MQTT_ACCQ,            /* AT+CMQTTACCQ sent, waiting OK */
    A7677S_MQTT_CFG_VERSION,     /* AT+CMQTTCFG="version",0,4 sent, waiting OK (sets MQTT 3.1.1) */
    A7677S_MQTT_CONNECT,         /* AT+CMQTTCONNECT sent, waiting URC +CMQTTCONNECT:0,0 */
    A7677S_MQTT_CONNECTED,       /* session live, ready for pub/sub */
    /* --- disconnect sequence --- */
    A7677S_MQTT_DISC,            /* AT+CMQTTDISC sent, waiting URC +CMQTTDISC:0,0 */
    A7677S_MQTT_REL,             /* AT+CMQTTREL sent, waiting OK */
    A7677S_MQTT_STOP,            /* AT+CMQTTSTOP sent, waiting URC +CMQTTSTOP:0 */
    /* --- publish sequence (CONNECTED -> back to CONNECTED) --- */
    A7677S_MQTT_PUB_TOPIC,       /* AT+CMQTTTOPIC sent, waiting ">" prompt */
    A7677S_MQTT_PUB_TOPIC_DATA,  /* topic bytes sent, waiting OK */
    A7677S_MQTT_PUB_PAYLOAD,     /* AT+CMQTTPAYLOAD sent, waiting ">" prompt */
    A7677S_MQTT_PUB_PAYLOAD_DATA,/* payload bytes sent, waiting OK */
    A7677S_MQTT_PUB_SEND,        /* AT+CMQTTPUB sent, waiting URC +CMQTTPUB:0,0 */
    /* --- subscribe sequence (CONNECTED -> back to CONNECTED) --- */
    A7677S_MQTT_SUB_TOPIC,       /* AT+CMQTTSUBTOPIC sent, waiting ">" prompt */
    A7677S_MQTT_SUB_TOPIC_DATA,  /* topic bytes sent, waiting OK */
    A7677S_MQTT_SUB_SEND,        /* AT+CMQTTSUB sent, waiting URC +CMQTTSUB:0,0 */
} a7677s_mqtt_state_t;

/* Inbound-message receive state (driven by URCs, independent of mqtt_state). */
typedef enum {
    A7677S_MQTT_RX_IDLE = 0,
    A7677S_MQTT_RX_TOPIC,
    A7677S_MQTT_RX_PAYLOAD,
    A7677S_MQTT_RX_END,
} a7677s_mqtt_rx_state_t;

/* Callback fired when a subscribed message arrives.
 * topic/topic_len: the topic string (not NUL-terminated, raw bytes).
 * payload/payload_len: the message body.
 * user_ctx: opaque pointer the caller registered alongside the callback. */
typedef void (*mqtt_incoming_cb_t)(const char *topic, uint16_t topic_len,
                                    const char *payload, uint16_t payload_len,
                                    void *user_ctx);

/* Callback fired on passive disconnect (+CMQTTCONNLOST). */
typedef void (*mqtt_connlost_cb_t)(void *user_ctx);

typedef struct a7677s a7677s_t;

struct a7677s
{
    modem_t base;

    a7677s_power_state_t power_state;

    /* Elapsed ms in the current power_state, advanced by poll()'s ts delta.
     * Lives in the instance (like modem_t.waitElapsed) — never a static
     * local — so more than one a7677s_t could coexist safely in the future. */
    uint32_t power_elapsed;

    /* 1 while an "AT" probe command is currently in flight during
     * A7677S_PWR_WAIT_BOOT (i.e. modem_send_command() was called and we are
     * waiting for modem_poll() to deliver the callback). Prevents sending a
     * second probe on top of one still awaiting a response. */
    uint8_t at_probe_pending;

    /* --- Network attach (AT init sequence) --- */
    a7677s_init_state_t init_state;
    uint32_t             init_elapsed;    /* elapsed ms in current init_state, advanced by poll()'s ts delta */
    uint8_t               init_retry_count; /* consecutive AT-layer failures, see A7677S_MAX_RETRY */
    uint8_t               creg_poll_count;  /* AT+CREG? polls done so far, see A7677S_CREG_MAX_POLL */
    uint8_t               init_cmd_pending; /* 1 while an init-sequence AT command is in flight */

    /* APN/auth, set once via a7677s_set_full_apn() before ops->start(ctx) is
     * called (mirrors the existing sim76xx_set_full_apn() pattern used by
     * sx_user_mqtt.c — NOT part of modem_ops_t.start() itself, since APN is
     * static per-device config, not a per-call runtime parameter like the
     * MQTT broker address is). Empty username/password means "no CGAUTH". */
    char apn[A7677S_APN_MAX_LEN];
    char username[A7677S_APN_MAX_LEN];
    char password[A7677S_APN_MAX_LEN];

    /* --- Low power (AT+CFUN=0/1), replaces PSM (see modem_ops.h design
     * notes: no DTR pin wired, so PSM's early-wake is not usable). Per
     * Documents/a7677s.md section 5.3.3, the serial port stays usable at
     * CFUN=0 (only RF/USIM turn off) — no extra UART-settling wait is
     * needed beyond the normal AT_CMD_TIMEOUT for the CFUN command itself. */
    uint8_t low_power_active;   /* 1 after AT+CFUN=0 succeeded, until AT+CFUN=1 succeeds again.
                                  * is_ready() returns false while this is set, since RF/network
                                  * is off and MQTT cannot work regardless of init_state. */
    uint8_t cfun_cmd_pending;   /* 1 while an AT+CFUN=0/1 command is in flight */
    power_mode_cb_t cfun_cb;    /* caller's callback for the in-flight enter/exit_low_power() call */

    /* --- MQTT client state --- */
    a7677s_mqtt_state_t    mqtt_state;
    uint8_t                mqtt_cmd_pending;  /* 1 while an MQTT AT cmd is in flight */

    /* Parameters saved across the async connect/pub/sub sequences. */
    char mqtt_client_id[A7677S_MQTT_CLIENT_ID_MAX];
    char mqtt_broker[A7677S_MQTT_BROKER_MAX];    /* "tcp://host:port" or "ssl://..." */
    char mqtt_username[A7677S_MQTT_USERNAME_MAX];
    char mqtt_password[A7677S_MQTT_PASSWORD_MAX];
    uint16_t mqtt_keepalive;
    uint8_t  mqtt_clean_session;
    uint8_t  mqtt_use_ssl;

    /* --- TLS certificates, only populated/used when mqtt_use_ssl is 1.
     * Caller-owned PEM text is copied in by a7677s_mqtt_connect() (the
     * caller's buffer does not need to outlive the call) into one of the
     * three dedicated buffers below -- kept separate rather than sharing one
     * scratch buffer so the CA/client-cert/client-key content is all still
     * available for re-send if a later step in the connect sequence fails
     * and the caller retries mqtt_connect() without re-supplying the certs
     * (the caller does still have to re-supply them today -- see the note
     * in a7677s_mqtt_connect() -- this just avoids a second, unrelated bug
     * of overwriting cert N while cert N+1 is still using the same buffer).
     * Sent to the modem via AT+CCERTDOWN=<filename>,<len> per cert (single
     * UART write per cert; datasheet 19.2.2's 3072-byte "per packet" note is
     * about the modem's own internal chunking of large inputs, not a limit
     * this driver needs to split at, since sx_uart_write() is not
     * packet-limited). mqtt_cert_has_ca/client/key record which steps were
     * actually requested (cert string was non-NULL/non-empty at connect()
     * time) so poll()'s cert-block state machine knows which
     * A7677S_MQTT_CERT_* states to skip. */
    uint8_t  mqtt_cert_has_ca;
    uint8_t  mqtt_cert_has_client;
    uint8_t  mqtt_cert_has_key;
    char     mqtt_cert_ca[A7677S_CERT_PEM_MAX];
    char     mqtt_cert_client[A7677S_CERT_PEM_MAX];
    char     mqtt_cert_key[A7677S_CERT_PEM_MAX];
    uint16_t mqtt_cert_ca_len;
    uint16_t mqtt_cert_client_len;
    uint16_t mqtt_cert_key_len;
    /* authmode sent via AT+CSSLCFG="authmode",... : 0/1/2, computed once at
     * the start of the cert block from mqtt_cert_has_client && mqtt_cert_has_key
     * (see a7677s.h enum comment above A7677S_MQTT_SSLCFG_AUTHMODE). */
    uint8_t  mqtt_ssl_authmode;

    /* Saved publish parameters. */
    char     mqtt_pub_topic[A7677S_MQTT_TOPIC_MAX];
    char     mqtt_pub_payload[A7677S_MQTT_PAYLOAD_MAX];
    uint16_t mqtt_pub_topic_len;
    uint16_t mqtt_pub_payload_len;
    uint8_t  mqtt_pub_qos;
    uint8_t  mqtt_pub_retain;

    /* Saved subscribe parameters. */
    char     mqtt_sub_topic[A7677S_MQTT_TOPIC_MAX];
    uint16_t mqtt_sub_topic_len;
    uint8_t  mqtt_sub_qos;

    /* Caller callbacks for the in-flight async operation. */
    mqtt_cb_t mqtt_op_cb;       /* connect / disconnect / publish / subscribe result */

    /* Inbound message receive state machine. */
    a7677s_mqtt_rx_state_t mqtt_rx_state;
    char     mqtt_rx_topic[A7677S_MQTT_TOPIC_MAX];
    char     mqtt_rx_payload[A7677S_MQTT_PAYLOAD_MAX];
    uint16_t mqtt_rx_topic_len;
    uint16_t mqtt_rx_payload_len;

    /* Registered inbound-message and connection-lost callbacks.
     * Set once by the service layer; not per-operation. */
    mqtt_incoming_cb_t mqtt_incoming_cb;
    mqtt_connlost_cb_t mqtt_connlost_cb;
    void              *mqtt_user_ctx;    /* passed back to both callbacks above */
};

void a7677s_init(a7677s_t *dce);

/* Sets APN/username/password before calling a7677s_ops.start(ctx) (i.e.
 * before the AT init/network-attach sequence begins). username/password may
 * be NULL or "" if the APN does not require authentication (CGAUTH is then
 * skipped entirely). Mirrors sim76xx_set_full_apn(). */
void a7677s_set_full_apn(a7677s_t *dce, const char *apn, const char *username, const char *password);

/* Register callbacks for inbound MQTT messages and passive disconnects.
 * Must be called once during board init (before mqtt_connect).
 * incoming_cb fires whenever a subscribed message arrives (+CMQTTRXPAYLOAD).
 * connlost_cb fires on passive disconnect (+CMQTTCONNLOST).
 * user_ctx is passed back unchanged to both callbacks. */
void a7677s_mqtt_register_callbacks(a7677s_t *dce,
                                     mqtt_incoming_cb_t incoming_cb,
                                     mqtt_connlost_cb_t connlost_cb,
                                     void *user_ctx);

/* modem_ops_t implementation (see a7677s.c). The service layer should only
 * ever touch this driver through a modem_handle_t { .ops = &a7677s_ops,
 * .ctx = &board.a7677s }, never call a7677s_* functions directly. */
extern const modem_ops_t a7677s_ops;

#ifdef __cplusplus
}
#endif
#endif // A7677S_H