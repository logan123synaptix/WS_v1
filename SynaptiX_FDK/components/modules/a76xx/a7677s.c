#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include "a7677s.h"
#include "sx_gpio.h"
#include "logger.h"
#include "string.h"

#define pModem(dce) ((modem_t *)&dce->base)
#define pDCE(modem) ((a7677s_t *)modem)

static const char *TAG = "A7677S";

/* --- AT command table (mirrors the pattern already used in sim76xx.c) --- */
#define CMD_AT              0
#define CMD_CPOF            1
#define CMD_CREG_SET        2
#define CMD_CREG_POLL       3
#define CMD_COPS_SET        4
#define CMD_COPS_QUERY      5
#define CMD_CGDCONT_QUERY   6
#define CMD_CFUN_0          7
#define CMD_CFUN_1          8
#define CMD_CGSN            9    /* AT+CGSN - read IMEI, once during start() */
#define CMD_CGPADDR         10   /* AT+CGPADDR=1 - read PDP IP, once during start() */
#define CMD_CSQ             11   /* AT+CSQ - read signal quality, polled periodically once ready */
#define CMD_DYNAMIC         12   /* CGDCONT/CGAUTH/CGACT - built at runtime, need apn/user/pass */

static modem_command_t command[] = {
    [CMD_AT]            = {.cmd = "AT\r\n",              .res_success = "\r\nOK\r\n", .res_fail = "\r\nERROR\r\n"},
    [CMD_CPOF]          = {.cmd = "AT+CPOF\r\n",         .res_success = "\r\nOK\r\n", .res_fail = "\r\nERROR\r\n"},
    [CMD_CREG_SET]      = {.cmd = "AT+CREG=1\r\n",       .res_success = "\r\nOK\r\n", .res_fail = "\r\nERROR\r\n"},
    [CMD_CREG_POLL]     = {.cmd = "AT+CREG?\r\n",        .res_success = "\r\nOK\r\n", .res_fail = "\r\nERROR\r\n"},
    [CMD_COPS_SET]      = {.cmd = "AT+COPS=0\r\n",       .res_success = "\r\nOK\r\n", .res_fail = "\r\nERROR\r\n"},
    [CMD_COPS_QUERY]    = {.cmd = "AT+COPS?\r\n",        .res_success = "\r\nOK\r\n", .res_fail = "\r\nERROR\r\n"},
    [CMD_CGDCONT_QUERY] = {.cmd = "AT+CGDCONT?\r\n",     .res_success = "\r\nOK\r\n", .res_fail = "\r\nERROR\r\n"},
    [CMD_CFUN_0]        = {.cmd = "AT+CFUN=0\r\n",       .res_success = "\r\nOK\r\n", .res_fail = "\r\nERROR\r\n"},
    [CMD_CFUN_1]        = {.cmd = "AT+CFUN=1\r\n",       .res_success = "\r\nOK\r\n", .res_fail = "\r\nERROR\r\n"},
    [CMD_CGSN]          = {.cmd = "AT+CGSN\r\n",         .res_success = "\r\nOK\r\n", .res_fail = "\r\nERROR\r\n"},
    [CMD_CGPADDR]       = {.cmd = "AT+CGPADDR=1\r\n",    .res_success = "\r\nOK\r\n", .res_fail = "\r\nERROR\r\n"},
    [CMD_CSQ]           = {.cmd = "AT+CSQ\r\n",          .res_success = "\r\nOK\r\n", .res_fail = "\r\nERROR\r\n"},
    [CMD_DYNAMIC]       = {0},
};

/* Scratch buffer for the CGDCONT/CGAUTH/CGACT commands built at runtime
 * (need apn/username/password filled in) — mirrors s_dyn_cmd_buf in
 * sim76xx.c. Not re-entrant, matches the existing single-instance
 * assumption already made by the command[] table above. */
static char s_dyn_cmd_buf[96];

/* Separate scratch buffer for MQTT dynamic commands (broker URL, topic, payload
 * length lines, etc.).  Kept separate from s_dyn_cmd_buf so an in-flight init
 * command and an MQTT command never share the same buffer — even though the
 * state machines are mutually exclusive at runtime, having distinct buffers
 * makes the invariant explicit and avoids subtle bugs during future refactors.
 * Size covers the longest possible AT+CMQTTCONNECT line:
 *   "AT+CMQTTCONNECT=0,\"tcp://<256 chars>\",64800,1,\"<256>\",\"<256>\"\r\n"
 *   = ~850 chars worst case; A7677S_MQTT_DYN_CMD_MAX is 384 which covers
 *   all practical broker URLs/credentials (broker <= 100, user/pass <= 64). */
static char s_mqtt_dyn_cmd_buf[A7677S_MQTT_DYN_CMD_MAX];

static void cb_at_probe(modem_t *modem, const char *response, modem_response_st_t res, void *arg);
static void cb_cpof(modem_t *modem, const char *response, modem_response_st_t res, void *arg);

static void restart_init(a7677s_t *dce);
static void send_init_cmd(a7677s_t *dce, int idx, modem_command_response_callback_t cb, uint32_t timeout_ms);
static void send_init_dynamic(a7677s_t *dce, const char *cmd_str, modem_command_response_callback_t cb, uint32_t timeout_ms);

/* MQTT sequence callbacks (forward declarations). */
static void cb_mqtt_start         (modem_t *modem, const char *response, modem_response_st_t res, void *arg);
static void cb_mqtt_accq          (modem_t *modem, const char *response, modem_response_st_t res, void *arg);
static void cb_mqtt_cfg_version   (modem_t *modem, const char *response, modem_response_st_t res, void *arg);
static void cb_mqtt_connect       (modem_t *modem, const char *response, modem_response_st_t res, void *arg);
static void cb_mqtt_disc          (modem_t *modem, const char *response, modem_response_st_t res, void *arg);
static void cb_mqtt_rel           (modem_t *modem, const char *response, modem_response_st_t res, void *arg);
static void cb_mqtt_stop          (modem_t *modem, const char *response, modem_response_st_t res, void *arg);
static void cb_mqtt_pub_topic     (modem_t *modem, const char *response, modem_response_st_t res, void *arg);
static void cb_mqtt_pub_topic_data(modem_t *modem, const char *response, modem_response_st_t res, void *arg);
static void cb_mqtt_pub_payload   (modem_t *modem, const char *response, modem_response_st_t res, void *arg);
static void cb_mqtt_pub_payload_data(modem_t *modem, const char *response, modem_response_st_t res, void *arg);
static void cb_mqtt_pub_send      (modem_t *modem, const char *response, modem_response_st_t res, void *arg);
static void cb_mqtt_sub_topic     (modem_t *modem, const char *response, modem_response_st_t res, void *arg);
static void cb_mqtt_sub_topic_data(modem_t *modem, const char *response, modem_response_st_t res, void *arg);
static void cb_mqtt_sub_send      (modem_t *modem, const char *response, modem_response_st_t res, void *arg);

/* TLS cert-upload / SSL-config sequence callbacks. Order mirrors
 * a7677s_mqtt_state_t: START -> [CERT_CA -> CERT_CLIENT -> CERT_KEY]
 * -> SSLCFG_CACERT -> SSLCFG_CLIENTCERT -> SSLCFG_CLIENTKEY
 * -> SSLCFG_AUTHMODE -> SSLBIND -> ACCQ. Only entered when mqtt_use_ssl=1
 * (see cb_mqtt_start()). Each *_prompt callback fires after the modem sends
 * ">" for AT+CCERTDOWN; each *_data callback fires after the raw PEM bytes
 * are accepted (OK). */
static void cb_mqtt_cert_ca_prompt      (modem_t *modem, const char *response, modem_response_st_t res, void *arg);
static void cb_mqtt_cert_ca_data        (modem_t *modem, const char *response, modem_response_st_t res, void *arg);
static void cb_mqtt_cert_client_prompt  (modem_t *modem, const char *response, modem_response_st_t res, void *arg);
static void cb_mqtt_cert_client_data    (modem_t *modem, const char *response, modem_response_st_t res, void *arg);
static void cb_mqtt_cert_key_prompt     (modem_t *modem, const char *response, modem_response_st_t res, void *arg);
static void cb_mqtt_cert_key_data       (modem_t *modem, const char *response, modem_response_st_t res, void *arg);
static void cb_mqtt_sslcfg_cacert       (modem_t *modem, const char *response, modem_response_st_t res, void *arg);
static void cb_mqtt_sslcfg_clientcert   (modem_t *modem, const char *response, modem_response_st_t res, void *arg);
static void cb_mqtt_sslcfg_clientkey    (modem_t *modem, const char *response, modem_response_st_t res, void *arg);
static void cb_mqtt_sslcfg_authmode     (modem_t *modem, const char *response, modem_response_st_t res, void *arg);
static void cb_mqtt_sslbind             (modem_t *modem, const char *response, modem_response_st_t res, void *arg);

/* begin_ssl_cert_block(): entry point from cb_mqtt_start() when use_ssl=1.
 * Uploads whichever of CA/client-cert/client-key were supplied (skipping
 * any not set), then falls through to begin_sslcfg_block(). */
static void begin_ssl_cert_block(a7677s_t *dce);
/* begin_sslcfg_block(): runs the AT+CSSLCFG/AT+CMQTTSSLCFG sequence that
 * always executes when use_ssl=1, regardless of whether any certs were
 * uploaded (this is what actually sets authmode=0 in the no-cert case).
 * Called either directly from begin_ssl_cert_block() (no certs at all) or
 * after the last requested cert finishes uploading. */
static void begin_sslcfg_block(a7677s_t *dce);
static void advance_to_accq(a7677s_t *dce);

/* Helper: send a dynamic MQTT command (built into s_mqtt_dyn_cmd_buf). */
static void send_mqtt_dynamic(a7677s_t *dce, const char *cmd_str,
                               const char *res_success, const char *res_fail,
                               modem_command_response_callback_t cb,
                               uint32_t timeout_ms);

/* URC (Unsolicited Result Code) scanner — reassembles +CMQTTRXSTART/TOPIC/
 * PAYLOAD/END, +CMQTTCONNLOST, and +CGEV PDN-deactivation lines. Runs only
 * while !modem_is_busy(), see a7677s_poll(). Fully self-contained inside
 * this driver: the service layer never sees any of this, it only ever gets
 * mqtt_incoming_cb/mqtt_connlost_cb fired through the generic
 * modem_ops_t.mqtt_set_callbacks() vtable entry. */
static void urc_poll(a7677s_t *dce);
static void urc_reset_rx_state(a7677s_t *dce);
static void urc_process_header_line(a7677s_t *dce, char *line);

/* modem_ops_t.mqtt_set_callbacks wrapper — thin adapter so the service layer
 * calls this only through the vtable, never a7677s_mqtt_register_callbacks()
 * directly. */
static void a7677s_mqtt_set_callbacks(void *ctx, mqtt_incoming_cb_t incoming_cb,
                                       mqtt_connlost_cb_t connlost_cb,
                                       void *user_ctx);

/* Helper: fire the in-flight MQTT op callback and clear it. */
static void mqtt_op_done(a7677s_t *dce, modem_ops_result_t result);

static void cb_init_at        (modem_t *modem, const char *response, modem_response_st_t res, void *arg);
static void cb_cgdcont        (modem_t *modem, const char *response, modem_response_st_t res, void *arg);
static void cb_cgauth         (modem_t *modem, const char *response, modem_response_st_t res, void *arg);
static void cb_cgact          (modem_t *modem, const char *response, modem_response_st_t res, void *arg);
static void cb_creg_set       (modem_t *modem, const char *response, modem_response_st_t res, void *arg);
static void cb_creg_poll      (modem_t *modem, const char *response, modem_response_st_t res, void *arg);
static void cb_cops_set       (modem_t *modem, const char *response, modem_response_st_t res, void *arg);
static void cb_cops_query     (modem_t *modem, const char *response, modem_response_st_t res, void *arg);
static void cb_cgdcont_query  (modem_t *modem, const char *response, modem_response_st_t res, void *arg);
static void cb_get_imei       (modem_t *modem, const char *response, modem_response_st_t res, void *arg);
static void cb_get_ip         (modem_t *modem, const char *response, modem_response_st_t res, void *arg);
static void cb_csq            (modem_t *modem, const char *response, modem_response_st_t res, void *arg);
static void cb_cfun_0         (modem_t *modem, const char *response, modem_response_st_t res, void *arg);
static void cb_cfun_1         (modem_t *modem, const char *response, modem_response_st_t res, void *arg);

/* ------------------------------------------------------------------ */
/*  Init                                                                */
/* ------------------------------------------------------------------ */

void a7677s_init(a7677s_t *dce)
{
    modem_init(pModem(dce));
    dce->power_state    = A7677S_PWR_IDLE;
    dce->power_elapsed  = 0;
    dce->at_probe_pending = 0;

    dce->init_state       = A7677S_INIT_IDLE;
    dce->init_elapsed     = 0;
    dce->init_retry_count = 0;
    dce->creg_poll_count  = 0;
    dce->init_cmd_pending = 0;
    dce->apn[0]      = '\0';
    dce->username[0] = '\0';
    dce->password[0] = '\0';

    dce->low_power_active = 0;
    dce->cfun_cmd_pending  = 0;
    dce->cfun_cb           = NULL;

    dce->ready_cb     = NULL;
    dce->ready_cb_ctx = NULL;
    dce->error_cb     = NULL;
    dce->error_cb_ctx = NULL;
    dce->was_ready    = 0;

    dce->imei[0]           = '\0';
    dce->ip[0]             = '\0';
    dce->rssi              = A7677S_RSSI_UNKNOWN;
    dce->rssi_poll_elapsed = 0;
    dce->rssi_cmd_pending  = 0;

    dce->mqtt_state       = A7677S_MQTT_IDLE;
    dce->mqtt_cmd_pending = 0;
    dce->mqtt_client_id[0] = '\0';
    dce->mqtt_broker[0]    = '\0';
    dce->mqtt_username[0]  = '\0';
    dce->mqtt_password[0]  = '\0';
    dce->mqtt_keepalive    = 60;
    dce->mqtt_clean_session = 1;
    dce->mqtt_use_ssl      = 0;
    dce->mqtt_pub_topic[0]   = '\0';
    dce->mqtt_pub_payload[0] = '\0';
    dce->mqtt_pub_topic_len   = 0;
    dce->mqtt_pub_payload_len = 0;
    dce->mqtt_pub_qos    = 0;
    dce->mqtt_pub_retain = 0;
    dce->mqtt_sub_topic[0]  = '\0';
    dce->mqtt_sub_topic_len = 0;
    dce->mqtt_sub_qos = 0;
    dce->mqtt_op_cb = NULL;
    dce->mqtt_rx_state       = A7677S_MQTT_RX_IDLE;
    dce->mqtt_rx_topic[0]    = '\0';
    dce->mqtt_rx_payload[0]  = '\0';
    dce->mqtt_rx_topic_len   = 0;
    dce->mqtt_rx_payload_len = 0;
    dce->mqtt_rx_topic_total_len   = 0;
    dce->mqtt_rx_payload_total_len = 0;
    dce->mqtt_incoming_cb  = NULL;
    dce->mqtt_connlost_cb  = NULL;
    dce->mqtt_user_ctx     = NULL;

    dce->urc_buf[0] = '\0';
    dce->urc_buf_len = 0;
    dce->urc_chunk_remaining = 0;

    log_debug(TAG, "Initializing");
}

void a7677s_set_full_apn(a7677s_t *dce, const char *apn, const char *username, const char *password)
{
    strncpy(dce->apn, apn ? apn : "", sizeof(dce->apn) - 1);
    dce->apn[sizeof(dce->apn) - 1] = '\0';

    strncpy(dce->username, username ? username : "", sizeof(dce->username) - 1);
    dce->username[sizeof(dce->username) - 1] = '\0';

    strncpy(dce->password, password ? password : "", sizeof(dce->password) - 1);
    dce->password[sizeof(dce->password) - 1] = '\0';

    log_info(TAG, "APN config: %s | User: %s | Pass: %s",
             dce->apn,
             dce->username[0] ? dce->username : "(none)",
             dce->password[0] ? dce->password : "(none)");
}

/* ------------------------------------------------------------------ */
/*  Power state machine                                                */
/* ------------------------------------------------------------------ */

static void a7677s_power_on_start(void *ctx)
{
    a7677s_t *dce = (a7677s_t *)ctx;

    log_info(TAG, "Power On start");
    dce->power_state      = A7677S_PWR_PULSE_HIGH;
    dce->power_elapsed    = 0;
    dce->at_probe_pending = 0;
    /* PWRKEY pulse pattern is identical to sim76xx (same physical circuit),
     * but here it is driven by state machine timing inside poll() instead
     * of sx_delay_ms(), so the main loop is never blocked. */
    sx_gpio_write(&dce->base.pwrPin, SX_GPIO_HIGH);
}

static void a7677s_power_off_start(void *ctx)
{
    a7677s_t *dce = (a7677s_t *)ctx;

    log_info(TAG, "Power Off start (AT+CPOF)");
    /* No powerPin/VBAT cutoff on this board revision (hasPowerPin stays 0,
     * see modem_init()) — graceful shutdown relies solely on AT+CPOF.
     * modem_send_command() itself is non-blocking; the OK/timeout arrives
     * later via modem_poll(), handled by cb_cpof() below. */
    dce->power_state   = A7677S_PWR_OFF_WAIT;
    dce->power_elapsed = 0;
    command[CMD_CPOF].callback = cb_cpof;
    command[CMD_CPOF].arg      = dce;
    modem_send_command(pModem(dce), &command[CMD_CPOF], A7677S_TIMEOUT_CPOF);
}

static bool a7677s_power_is_busy(void *ctx)
{
    a7677s_t *dce = (a7677s_t *)ctx;
    return (dce->power_state != A7677S_PWR_IDLE) &&
           (dce->power_state != A7677S_PWR_READY);
}

static void cb_at_probe(modem_t *modem, const char *response, modem_response_st_t res, void *arg)
{
    a7677s_t *dce = pDCE(modem);
    dce->at_probe_pending = 0;

    if (res == MODEM_RESPONSE_SUCCESS) {
        log_info(TAG, "Module responsive, boot confirmed");
        dce->power_state   = A7677S_PWR_READY;
        dce->power_elapsed = 0;
    }
    /* On FAIL/TIMEOUT: stay in A7677S_PWR_WAIT_BOOT, poll() will send the
     * next probe after another A7677S_BOOT_PROBE_MS — this is the expected,
     * normal path while the module is still booting. */
}

static void cb_cpof(modem_t *modem, const char *response, modem_response_st_t res, void *arg)
{
    a7677s_t *dce = pDCE(modem);
    /* Whether AT+CPOF succeeded, failed, or timed out, the power sequence
     * is considered finished from the state machine's point of view — a
     * failed/timeout CPOF still leaves the caller free to retry
     * power_off_start() again later; power_is_busy() must not stay stuck
     * forever. */
    if (res != MODEM_RESPONSE_SUCCESS) {
        log_error(TAG, "AT+CPOF did not complete cleanly (res=%d)", res);
    } else {
        log_info(TAG, "Power Off OK");
    }
    dce->power_state   = A7677S_PWR_IDLE;
    dce->power_elapsed = 0;
}

static void a7677s_poll(void *ctx, uint32_t ts)
{
    a7677s_t *dce = (a7677s_t *)ctx;

    /* Always pump the underlying command/response state machine first, so
     * any in-flight AT command (probe or CPOF) gets its callback fired
     * before we act on power_state below. */
    modem_poll(pModem(dce), ts);

    /* URC scanner: only safe to read the UART for unsolicited lines while
     * no AT command is currently awaiting its response (modem_poll() above
     * owns the UART/modem->buff channel exclusively whenever isBusy is 1).
     * Since modem_poll() just ran and may have cleared isBusy in the same
     * tick (command completed), checking is_busy() here, after it, means a
     * URC arriving in the same tick a command just completed is still
     * picked up — no extra tick of latency. */
    if (!modem_is_busy(pModem(dce))) {
        urc_poll(dce);
    }

    /* set_on_ready/set_on_error edge detection — must run every tick,
     * independent of which state machine below is active. Only fires each
     * callback once per transition (never repeatedly while steady). A 1->0
     * transition caused by entering low power on purpose is NOT treated as
     * an error (low_power_active is part of is_ready()'s own definition, so
     * a deliberate enter_low_power() call would otherwise look identical to
     * an unexpected drop — excluded explicitly below). */
    {
        bool now_ready = a7677s_is_ready(dce);
        if (now_ready && !dce->was_ready) {
            if (dce->ready_cb) dce->ready_cb(dce->ready_cb_ctx);
        } else if (!now_ready && dce->was_ready && !dce->low_power_active) {
            if (dce->error_cb) dce->error_cb(dce->error_cb_ctx);
        }
        dce->was_ready = now_ready;
    }

    /* Periodic AT+CSQ refresh for get_rssi() — only while ready, and only
     * when no other AT command (init sequence, MQTT, CFUN) is already in
     * flight, since this shares the same single command/response channel
     * as everything else in modem_send_command()/modem_poll(). Skipping a
     * tick when busy just delays the refresh slightly; it is re-attempted
     * every tick thereafter since rssi_poll_elapsed keeps accumulating. */
    if (a7677s_is_ready(dce) && !dce->rssi_cmd_pending &&
        !modem_is_busy(pModem(dce)) && !dce->init_cmd_pending &&
        !dce->mqtt_cmd_pending && !dce->cfun_cmd_pending)
    {
        dce->rssi_poll_elapsed += ts;
        if (dce->rssi_poll_elapsed >= A7677S_RSSI_POLL_MS) {
            dce->rssi_poll_elapsed = 0;
            dce->rssi_cmd_pending  = 1;
            command[CMD_CSQ].callback = cb_csq;
            command[CMD_CSQ].arg      = dce;
            modem_send_command(pModem(dce), &command[CMD_CSQ], A7677S_TIMEOUT_CSQ);
        }
    }

    switch (dce->power_state) {

    case A7677S_PWR_PULSE_HIGH:
        dce->power_elapsed += ts;
        if (dce->power_elapsed >= A7677S_PULSE_HIGH_MS) {
            sx_gpio_write(&dce->base.pwrPin, SX_GPIO_LOW);
            dce->power_state   = A7677S_PWR_PULSE_LOW;
            dce->power_elapsed = 0;
        }
        break;

    case A7677S_PWR_PULSE_LOW:
        dce->power_elapsed += ts;
        if (dce->power_elapsed >= A7677S_PULSE_LOW_MS) {
            sx_gpio_write(&dce->base.pwrPin, SX_GPIO_HIGH);
            dce->power_state      = A7677S_PWR_WAIT_BOOT;
            dce->power_elapsed    = 0;
            dce->at_probe_pending = 0;
        }
        break;

    case A7677S_PWR_WAIT_BOOT:
        dce->power_elapsed += ts;
        if (!dce->at_probe_pending &&
            dce->power_elapsed >= A7677S_BOOT_PROBE_MS &&
            !modem_is_busy(pModem(dce))) {
            dce->power_elapsed    = 0;
            dce->at_probe_pending = 1;
            command[CMD_AT].callback = cb_at_probe;
            command[CMD_AT].arg      = dce;
            modem_send_command(pModem(dce), &command[CMD_AT], A7677S_TIMEOUT_AT);
        }
        break;

    case A7677S_PWR_IDLE:
    case A7677S_PWR_READY:
    case A7677S_PWR_OFF_WAIT:
    default:
        /* Nothing to advance here; A7677S_PWR_OFF_WAIT is resolved entirely
         * by cb_cpof() above, driven by modem_poll(). */
        break;
    }

    /* Advance the network attach sequence. Only A7677S_INIT_CREG_POLL needs
     * timer-driven action here (send the next AT+CREG? after
     * A7677S_CREG_POLL_MS) — every other init_state transitions purely via
     * its AT-command callback, already fired above by modem_poll(). */
    if (dce->init_state == A7677S_INIT_CREG_POLL && !dce->init_cmd_pending &&
        !modem_is_busy(pModem(dce))) {
        dce->init_elapsed += ts;
        if (dce->init_elapsed >= A7677S_CREG_POLL_MS) {
            dce->init_elapsed = 0;
            send_init_cmd(dce, CMD_CREG_POLL, cb_creg_poll, A7677S_TIMEOUT_AT);
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Network attach (AT init sequence)                                  */
/* ------------------------------------------------------------------ */
/* Order and command syntax: see the comment above a7677s_init_state_t in
 * a7677s.h for the two reference sources and the one deliberate deviation
 * (explicit AT+CREG? poll instead of trusting AT+CREG=1's "OK"). */

static void send_init_cmd(a7677s_t *dce, int idx, modem_command_response_callback_t cb, uint32_t timeout_ms)
{
    command[idx].callback = cb;
    command[idx].arg      = dce;
    dce->init_cmd_pending = 1;
    log_debug(TAG, "INIT CMD: %s", command[idx].cmd);
    modem_send_command(pModem(dce), &command[idx], timeout_ms);
}

static void send_init_dynamic(a7677s_t *dce, const char *cmd_str, modem_command_response_callback_t cb, uint32_t timeout_ms)
{
    command[CMD_DYNAMIC].cmd         = cmd_str;
    command[CMD_DYNAMIC].res_success = "\r\nOK\r\n";
    command[CMD_DYNAMIC].res_fail    = "\r\nERROR\r\n";
    command[CMD_DYNAMIC].callback    = cb;
    command[CMD_DYNAMIC].arg         = dce;
    dce->init_cmd_pending = 1;
    log_debug(TAG, "INIT CMD: %s", cmd_str);
    modem_send_command(pModem(dce), &command[CMD_DYNAMIC], timeout_ms);
}

/* Restarts the whole attach sequence from AT, incrementing the retry
 * counter. Mirrors sim76xx.c's restart_at(). Does NOT touch power_state —
 * if the module stopped responding entirely, that is caught separately by
 * power_is_busy()/is_ready() going false, not handled here. */
static void restart_init(a7677s_t *dce)
{
    dce->init_retry_count++;
    log_error(TAG, "Init step failed (retry %u/%u)", dce->init_retry_count, A7677S_MAX_RETRY);

    if (dce->init_retry_count >= A7677S_MAX_RETRY) {
        /* Give up restarting silently forever; go back to IDLE so the
         * caller (board/service layer) can decide to power-cycle via
         * power_off_start()/power_on_start() instead of looping here. */
        dce->init_retry_count = 0;
        dce->init_state       = A7677S_INIT_IDLE;
        log_error(TAG, "Init sequence failed after max retries, giving up (caller should power-cycle)");
        return;
    }

    dce->init_state   = A7677S_INIT_AT;
    dce->init_elapsed = 0;
    send_init_cmd(dce, CMD_AT, cb_init_at, A7677S_TIMEOUT_AT);
}

static void cb_init_at(modem_t *modem, const char *response, modem_response_st_t res, void *arg)
{
    a7677s_t *dce = pDCE(arg);
    dce->init_cmd_pending = 0;
    if (res != MODEM_RESPONSE_SUCCESS) { restart_init(dce); return; }

    dce->init_retry_count = 0;
    dce->init_state       = A7677S_INIT_CGDCONT;
    snprintf(s_dyn_cmd_buf, sizeof(s_dyn_cmd_buf), "AT+CGDCONT=1,\"IP\",\"%s\"\r\n", dce->apn);
    send_init_dynamic(dce, s_dyn_cmd_buf, cb_cgdcont, A7677S_TIMEOUT_NETWORK);
}

static void cb_cgdcont(modem_t *modem, const char *response, modem_response_st_t res, void *arg)
{
    a7677s_t *dce = pDCE(arg);
    dce->init_cmd_pending = 0;
    if (res != MODEM_RESPONSE_SUCCESS) { restart_init(dce); return; }

    if (dce->username[0] != '\0' && dce->password[0] != '\0') {
        /* PAP: AT+CGAUTH=<cid>,<auth_type>,<passwd>,<user> — password comes
         * BEFORE username. Confirmed against a76xx_at_cmd.md section 5.2.16
         * (WriteCommand: AT+CGAUTH=<cid>[,<auth_type>[,<passwd>[,<user>]]]).
         * WS_v0/A76xx.c has this swapped (passes username, then password) —
         * that is a bug in WS_v0, not followed here. */
        dce->init_state = A7677S_INIT_CGAUTH;
        snprintf(s_dyn_cmd_buf, sizeof(s_dyn_cmd_buf), "AT+CGAUTH=1,1,\"%s\",\"%s\"\r\n",
                 dce->password, dce->username);
        send_init_dynamic(dce, s_dyn_cmd_buf, cb_cgauth, A7677S_TIMEOUT_NETWORK);
        return;
    }
    if (dce->password[0] != '\0') {
        /* CHAP: only <passwd> given. */
        dce->init_state = A7677S_INIT_CGAUTH;
        snprintf(s_dyn_cmd_buf, sizeof(s_dyn_cmd_buf), "AT+CGAUTH=1,2,\"%s\"\r\n", dce->password);
        send_init_dynamic(dce, s_dyn_cmd_buf, cb_cgauth, A7677S_TIMEOUT_NETWORK);
        return;
    }
    /* No auth configured — skip CGAUTH entirely, go straight to CGACT. */
    dce->init_state = A7677S_INIT_CGACT;
    snprintf(s_dyn_cmd_buf, sizeof(s_dyn_cmd_buf), "AT+CGACT=1,1\r\n");
    send_init_dynamic(dce, s_dyn_cmd_buf, cb_cgact, A7677S_TIMEOUT_NETWORK);
}

static void cb_cgauth(modem_t *modem, const char *response, modem_response_st_t res, void *arg)
{
    a7677s_t *dce = pDCE(arg);
    dce->init_cmd_pending = 0;
    if (res != MODEM_RESPONSE_SUCCESS) { restart_init(dce); return; }

    log_info(TAG, "CGAUTH OK");
    dce->init_state = A7677S_INIT_CGACT;
    snprintf(s_dyn_cmd_buf, sizeof(s_dyn_cmd_buf), "AT+CGACT=1,1\r\n");
    send_init_dynamic(dce, s_dyn_cmd_buf, cb_cgact, A7677S_TIMEOUT_NETWORK);
}

static void cb_cgact(modem_t *modem, const char *response, modem_response_st_t res, void *arg)
{
    a7677s_t *dce = pDCE(arg);
    dce->init_cmd_pending = 0;
    if (res != MODEM_RESPONSE_SUCCESS) { restart_init(dce); return; }

    dce->init_state = A7677S_INIT_CREG_SET;
    send_init_cmd(dce, CMD_CREG_SET, cb_creg_set, A7677S_TIMEOUT_AT);
}

static void cb_creg_set(modem_t *modem, const char *response, modem_response_st_t res, void *arg)
{
    a7677s_t *dce = pDCE(arg);
    dce->init_cmd_pending = 0;
    if (res != MODEM_RESPONSE_SUCCESS) { restart_init(dce); return; }

    /* AT+CREG=1 only enabled the unsolicited report — it did not confirm
     * registration. Move to CREG_POLL, which is driven from poll() below
     * (waits A7677S_CREG_POLL_MS between each AT+CREG? read). */
    dce->init_state      = A7677S_INIT_CREG_POLL;
    dce->init_elapsed     = 0;
    dce->creg_poll_count  = 0;
}

static void cb_creg_poll(modem_t *modem, const char *response, modem_response_st_t res, void *arg)
{
    a7677s_t *dce = pDCE(arg);
    dce->init_cmd_pending = 0;

    if (res == MODEM_RESPONSE_SUCCESS && response) {
        /* Response format: +CREG: <n>,<stat>[,<lac>,<ci>] */
        const char *p = strstr(response, "+CREG:");
        if (p) {
            p = strchr(p, ',');
            if (p) {
                int stat = (int)strtol(p + 1, NULL, 10);
                log_debug(TAG, "CREG stat=%d (poll %u/%u)", stat, dce->creg_poll_count, A7677S_CREG_MAX_POLL);
                if (stat == 1 || stat == 5) {
                    log_info(TAG, "Network registered (stat=%d)", stat);
                    dce->init_state = A7677S_INIT_COPS_SET;
                    send_init_cmd(dce, CMD_COPS_SET, cb_cops_set, A7677S_TIMEOUT_NETWORK);
                    return;
                }
            }
        }
    }

    /* Not registered yet (or response didn't parse) — keep polling until
     * A7677S_CREG_MAX_POLL is reached, then treat it as a failed attach. */
    dce->creg_poll_count++;
    if (dce->creg_poll_count >= A7677S_CREG_MAX_POLL) {
        log_error(TAG, "Network registration timed out after %u polls", dce->creg_poll_count);
        restart_init(dce);
        return;
    }
    /* Stay in A7677S_INIT_CREG_POLL; poll() will send the next AT+CREG?
     * after another A7677S_CREG_POLL_MS. */
    dce->init_elapsed = 0;
}

static void cb_cops_set(modem_t *modem, const char *response, modem_response_st_t res, void *arg)
{
    a7677s_t *dce = pDCE(arg);
    dce->init_cmd_pending = 0;
    if (res != MODEM_RESPONSE_SUCCESS) { restart_init(dce); return; }

    dce->init_state = A7677S_INIT_COPS_QUERY;
    send_init_cmd(dce, CMD_COPS_QUERY, cb_cops_query, A7677S_TIMEOUT_NETWORK);
}

static void cb_cops_query(modem_t *modem, const char *response, modem_response_st_t res, void *arg)
{
    a7677s_t *dce = pDCE(arg);
    dce->init_cmd_pending = 0;
    if (res != MODEM_RESPONSE_SUCCESS) { restart_init(dce); return; }

    if (response) {
        log_info(TAG, "COPS: %s", response);
    }
    dce->init_state = A7677S_INIT_CGDCONT_QUERY;
    send_init_cmd(dce, CMD_CGDCONT_QUERY, cb_cgdcont_query, A7677S_TIMEOUT_AT);
}

static void cb_cgdcont_query(modem_t *modem, const char *response, modem_response_st_t res, void *arg)
{
    a7677s_t *dce = pDCE(arg);
    dce->init_cmd_pending = 0;
    /* Purely informational read-back (mirrors sim76xx.c's
     * cb_cgdcont_query()) — even on FAIL/TIMEOUT here we still consider the
     * attach itself complete, since CGACT/CREG already succeeded; a failed
     * read-back should not block is_ready(). Proceeds to the one-shot
     * device-info reads (IMEI/IP) rather than READY directly. */
    if (res == MODEM_RESPONSE_SUCCESS && response) {
        log_info(TAG, "CGDCONT: %s", response);
    } else {
        log_warn(TAG, "CGDCONT read-back failed (non-fatal)");
    }
    dce->init_retry_count = 0;
    dce->init_state       = A7677S_INIT_GET_IMEI;
    send_init_cmd(dce, CMD_CGSN, cb_get_imei, A7677S_TIMEOUT_AT);
}

static void cb_get_imei(modem_t *modem, const char *response, modem_response_st_t res, void *arg)
{
    a7677s_t *dce = pDCE(arg);
    dce->init_cmd_pending = 0;
    /* AT+CGSN's success response is a single line containing just the IMEI
     * digits (a76xx_at_cmd.md 2.2.19), e.g. "351602000330570" — no "+CGSN:"
     * prefix, unlike most other queries. Non-fatal on failure: leaves
     * dce->imei as "" (already the case from a7677s_init()'s memset-less
     * init, or from a previous failed attempt), does not block is_ready(). */
    if (res == MODEM_RESPONSE_SUCCESS && response) {
        char imei_buf[A7677S_IMEI_LEN] = {0};
        int i = 0;
        const char *p = response;
        /* Skip any leading \r\n left over from the raw response buffer;
         * copy only ASCII digits, stop at the first non-digit or buffer
         * limit. Mirrors the defensive-parsing style used elsewhere in this
         * file (e.g. cb_creg_poll's strtol on a trusted-format substring),
         * but here the whole line IS the value, not a "key: value" pair. */
        while (*p && (*p == '\r' || *p == '\n')) p++;
        while (*p && isdigit((unsigned char)*p) && i < (int)(A7677S_IMEI_LEN - 1)) {
            imei_buf[i++] = *p++;
        }
        imei_buf[i] = '\0';
        if (i > 0) {
            memcpy(dce->imei, imei_buf, sizeof(imei_buf));
            log_info(TAG, "IMEI: %s", dce->imei);
        } else {
            log_warn(TAG, "CGSN response did not parse as digits: %s", response);
        }
    } else {
        log_warn(TAG, "AT+CGSN failed (non-fatal)");
    }

    dce->init_state = A7677S_INIT_GET_IP;
    send_init_cmd(dce, CMD_CGPADDR, cb_get_ip, A7677S_TIMEOUT_AT);
}

static void cb_get_ip(modem_t *modem, const char *response, modem_response_st_t res, void *arg)
{
    a7677s_t *dce = pDCE(arg);
    dce->init_cmd_pending = 0;
    /* Response format: +CGPADDR:<cid>,<PDP_addr> (a76xx_at_cmd.md 5.2.13).
     * cid is always 1 here (matches the single AT+CGDCONT=1,... context this
     * driver ever sets up). Non-fatal on failure: leaves dce->ip as "". */
    if (res == MODEM_RESPONSE_SUCCESS && response) {
        const char *p = strstr(response, "+CGPADDR:");
        if (p) {
            p = strchr(p, ',');
            if (p) {
                p++; /* skip the comma, now at the start of <PDP_addr> */
                char ip_buf[A7677S_IP_LEN] = {0};
                int i = 0;
                while (*p && *p != '\r' && *p != '\n' && *p != ',' &&
                       i < (int)(A7677S_IP_LEN - 1)) {
                    ip_buf[i++] = *p++;
                }
                ip_buf[i] = '\0';
                if (i > 0) {
                    memcpy(dce->ip, ip_buf, sizeof(ip_buf));
                    log_info(TAG, "IP: %s", dce->ip);
                }
            }
        }
        if (dce->ip[0] == '\0') {
            log_warn(TAG, "CGPADDR response did not parse: %s", response);
        }
    } else {
        log_warn(TAG, "AT+CGPADDR failed (non-fatal)");
    }

    dce->init_state = A7677S_INIT_READY;
    log_info(TAG, "Network attach complete, ready for MQTT");
}

/* Fired by the periodic AT+CSQ refresh in a7677s_poll() (not part of the
 * init_state sequence — this keeps running for as long as the modem stays
 * ready, see the a7677s_is_ready() gate at the call site). Response format:
 * +CSQ:<rssi>,<ber> (a76xx_at_cmd.md 3.2.2). On failure, caches
 * A7677S_RSSI_UNKNOWN rather than leaving the previous value, since a
 * failed AT+CSQ IS informative (link likely degraded) — unlike IMEI/IP,
 * which are one-shot facts that failing to re-read does not invalidate. */
static void cb_csq(modem_t *modem, const char *response, modem_response_st_t res, void *arg)
{
    a7677s_t *dce = pDCE(arg);
    dce->rssi_cmd_pending = 0;

    if (res == MODEM_RESPONSE_SUCCESS && response) {
        const char *p = strstr(response, "+CSQ:");
        if (p) {
            int rssi = (int)strtol(p + 5, NULL, 10);
            dce->rssi = rssi;
            log_debug(TAG, "RSSI: %d", rssi);
            return;
        }
        log_warn(TAG, "CSQ response did not parse: %s", response);
    } else {
        log_warn(TAG, "AT+CSQ failed");
    }
    dce->rssi = A7677S_RSSI_UNKNOWN;
}

/* ------------------------------------------------------------------ */
/*  Low power (AT+CFUN=0/1)                                            */
/* ------------------------------------------------------------------ */
/* Per Documents/a7677s.md section 5.3.3: at CFUN=0 the serial port stays
 * usable (only RF/USIM turn off), so unlike the CREG_POLL deviation above,
 * no extra confirmation step is needed here — AT+CFUN's own OK/ERROR is a
 * direct, reliable result per a76xx_at_cmd.md section 3.2.1. */

static void cb_cfun_0(modem_t *modem, const char *response, modem_response_st_t res, void *arg)
{
    a7677s_t *dce = pDCE(arg);
    dce->cfun_cmd_pending = 0;

    modem_ops_result_t result;
    if (res == MODEM_RESPONSE_SUCCESS) {
        log_info(TAG, "Entered low power (AT+CFUN=0)");
        dce->low_power_active = 1;
        result = MODEM_OPS_OK;
    } else if (res == MODEM_RESPONSE_TIMEOUT) {
        log_error(TAG, "AT+CFUN=0 timed out");
        result = MODEM_OPS_TIMEOUT;
    } else {
        log_error(TAG, "AT+CFUN=0 failed");
        result = MODEM_OPS_ERROR;
    }

    power_mode_cb_t cb = dce->cfun_cb;
    dce->cfun_cb = NULL;
    if (cb) cb(result, dce);
}

static void cb_cfun_1(modem_t *modem, const char *response, modem_response_st_t res, void *arg)
{
    a7677s_t *dce = pDCE(arg);
    dce->cfun_cmd_pending = 0;

    modem_ops_result_t result;
    if (res == MODEM_RESPONSE_SUCCESS) {
        log_info(TAG, "Exited low power (AT+CFUN=1)");
        dce->low_power_active = 0;
        result = MODEM_OPS_OK;
    } else if (res == MODEM_RESPONSE_TIMEOUT) {
        log_error(TAG, "AT+CFUN=1 timed out");
        result = MODEM_OPS_TIMEOUT;
        /* Deliberately leave low_power_active=1 on failure: we do not know
         * the module's true state, and is_ready() staying false is the
         * safe default until a retry succeeds — better than reporting
         * "ready" while unsure the module actually left CFUN=0. */
    } else {
        log_error(TAG, "AT+CFUN=1 failed");
        result = MODEM_OPS_ERROR;
    }

    power_mode_cb_t cb = dce->cfun_cb;
    dce->cfun_cb = NULL;
    if (cb) cb(result, dce);
}

/* TODO (next piece, not yet implemented):
 *   - mqtt_connect/disconnect/publish/subscribe: AT+CMQTT* sequence, not yet
 *     implemented (see confirmed AT command set in Documents/a76xx_at_cmd.md
 *     section 18.2, and the errcode-parsing bug fix noted in the handoff).
 * These functions are stubbed to fail safely (return -1 / false) so the
 * vtable compiles and is safe to wire up incrementally.
 */
static int a7677s_start(void *ctx)
{
    a7677s_t *dce = (a7677s_t *)ctx;

    if (dce->power_state != A7677S_PWR_READY) {
        log_warn(TAG, "start(): power not ready yet");
        return -1;
    }
    if (modem_is_busy(pModem(dce)) || dce->init_cmd_pending) {
        log_warn(TAG, "start(): modem busy");
        return -1;
    }
    if (dce->init_state != A7677S_INIT_IDLE && dce->init_state != A7677S_INIT_READY) {
        log_warn(TAG, "start(): attach sequence already in progress");
        return -1;
    }

    log_info(TAG, "Starting network attach sequence");
    dce->init_retry_count = 0;
    dce->creg_poll_count  = 0;
    dce->init_state       = A7677S_INIT_AT;
    send_init_cmd(dce, CMD_AT, cb_init_at, A7677S_TIMEOUT_AT);
    return 0;
}

static bool a7677s_is_ready(void *ctx)
{
    a7677s_t *dce = (a7677s_t *)ctx;
    return dce->power_state == A7677S_PWR_READY &&
           dce->init_state  == A7677S_INIT_READY &&
           !dce->low_power_active;
}

/* modem_ops_t.set_on_ready / set_on_error — just store the callback + ctx;
 * actual firing (rising/falling edge detection) happens once per tick at
 * the top of a7677s_poll(), see the was_ready check there. Not an async op
 * of its own, so it never touches modem_cmd_pending/mqtt_cmd_pending etc. */
static void a7677s_set_on_ready(void *ctx, modem_ready_cb_t cb, void *user_ctx)
{
    a7677s_t *dce = (a7677s_t *)ctx;
    dce->ready_cb     = cb;
    dce->ready_cb_ctx = user_ctx;
}

static void a7677s_set_on_error(void *ctx, modem_error_cb_t cb, void *user_ctx)
{
    a7677s_t *dce = (a7677s_t *)ctx;
    dce->error_cb     = cb;
    dce->error_cb_ctx = user_ctx;
}

/* modem_ops_t.get_imei/get_rssi/get_ip — all three just return a cached
 * value, never send AT or block (see modem_ops.h's doc-comment on each).
 * get_imei/get_ip return "" until the corresponding A7677S_INIT_GET_IMEI/
 * A7677S_INIT_GET_IP step has run (or if it failed); get_rssi returns
 * A7677S_RSSI_UNKNOWN (99) until the first successful periodic AT+CSQ. */
static const char *a7677s_get_imei(void *ctx)
{
    a7677s_t *dce = (a7677s_t *)ctx;
    return dce->imei;
}

static int a7677s_get_rssi(void *ctx)
{
    a7677s_t *dce = (a7677s_t *)ctx;
    return dce->rssi;
}

static const char *a7677s_get_ip(void *ctx)
{
    a7677s_t *dce = (a7677s_t *)ctx;
    return dce->ip;
}

static int a7677s_enter_low_power(void *ctx, power_mode_cb_t cb)
{
    a7677s_t *dce = (a7677s_t *)ctx;

    if (dce->power_state != A7677S_PWR_READY) {
        log_warn(TAG, "enter_low_power(): power not ready");
        return -1;
    }
    if (modem_is_busy(pModem(dce)) || dce->init_cmd_pending || dce->cfun_cmd_pending) {
        log_warn(TAG, "enter_low_power(): modem busy");
        return -1;
    }
    if (dce->low_power_active) {
        log_warn(TAG, "enter_low_power(): already in low power");
        return -1;
    }

    dce->cfun_cb = cb;
    command[CMD_CFUN_0].callback = cb_cfun_0;
    command[CMD_CFUN_0].arg      = dce;
    dce->cfun_cmd_pending = 1;
    modem_send_command(pModem(dce), &command[CMD_CFUN_0], A7677S_TIMEOUT_CFUN);
    return 0;
}

static int a7677s_exit_low_power(void *ctx, power_mode_cb_t cb)
{
    a7677s_t *dce = (a7677s_t *)ctx;

    if (dce->power_state != A7677S_PWR_READY) {
        log_warn(TAG, "exit_low_power(): power not ready");
        return -1;
    }
    if (modem_is_busy(pModem(dce)) || dce->init_cmd_pending || dce->cfun_cmd_pending) {
        log_warn(TAG, "exit_low_power(): modem busy");
        return -1;
    }
    if (!dce->low_power_active) {
        log_warn(TAG, "exit_low_power(): not in low power");
        return -1;
    }

    dce->cfun_cb = cb;
    command[CMD_CFUN_1].callback = cb_cfun_1;
    command[CMD_CFUN_1].arg      = dce;
    dce->cfun_cmd_pending = 1;
    modem_send_command(pModem(dce), &command[CMD_CFUN_1], A7677S_TIMEOUT_CFUN);
    return 0;
}
/* ------------------------------------------------------------------ */
/*  MQTT — helpers                                                      */
/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/*  MQTT — URC (Unsolicited Result Code) scanner                        */
/* ------------------------------------------------------------------ */
/* Reassembles, from raw UART bytes read independently of the AT command/
 * response channel (see urc_poll() call site in a7677s_poll()):
 *   +CMQTTRXSTART: <idx>,<topic_total_len>,<payload_total_len>
 *   +CMQTTRXTOPIC: <idx>,<chunk_len>\r\n<chunk raw bytes>      (may repeat)
 *   +CMQTTRXPAYLOAD: <idx>,<chunk_len>\r\n<chunk raw bytes>    (may repeat)
 *   +CMQTTRXEND: <idx>
 * per a76xx_at_cmd.md 18.4 — topic/payload chunks repeat as needed until
 * their accumulated length reaches the total announced by RXSTART. Also
 * catches +CMQTTCONNLOST:<idx>,<cause> (passive MQTT disconnect) and
 * +CGEV: ME/NW PDN DEACT (network-level loss — treated as an implicit MQTT
 * disconnect too, since MQTT cannot survive without the underlying PDN). */

static void urc_reset_rx_state(a7677s_t *dce)
{
    dce->mqtt_rx_state             = A7677S_MQTT_RX_IDLE;
    dce->mqtt_rx_topic_len         = 0;
    dce->mqtt_rx_payload_len       = 0;
    dce->mqtt_rx_topic_total_len   = 0;
    dce->mqtt_rx_payload_total_len = 0;
    dce->urc_chunk_remaining       = 0;
}

/* line: a single NUL-terminated header line, "\r\n" already stripped.
 * Only called when urc_chunk_remaining == 0 (i.e. between raw-byte
 * chunks) — see urc_poll(). */
static void urc_process_header_line(a7677s_t *dce, char *line)
{
    if (strstr(line, "+CMQTTRXSTART:")) {
        unsigned idx = 0, topic_len = 0, payload_len = 0;
        char *p = strchr(line, ':');
        if (p && sscanf(p + 1, "%u,%u,%u", &idx, &topic_len, &payload_len) == 3) {
            urc_reset_rx_state(dce);
            dce->mqtt_rx_state             = A7677S_MQTT_RX_TOPIC;
            dce->mqtt_rx_topic_total_len   = (uint16_t)topic_len;
            dce->mqtt_rx_payload_total_len = (uint16_t)payload_len;
            dce->mqtt_rx_topic[0]   = '\0';
            dce->mqtt_rx_payload[0] = '\0';
            log_debug(TAG, "RXSTART idx=%u topic_len=%u payload_len=%u",
                      idx, topic_len, payload_len);
        } else {
            log_error(TAG, "Malformed +CMQTTRXSTART: [%s]", line);
            urc_reset_rx_state(dce);
        }
        return;
    }

    if (strstr(line, "+CMQTTRXTOPIC:")) {
        unsigned idx = 0, chunk_len = 0;
        char *p = strchr(line, ':');
        if (dce->mqtt_rx_state == A7677S_MQTT_RX_TOPIC && p &&
            sscanf(p + 1, "%u,%u", &idx, &chunk_len) == 2 && chunk_len > 0) {
            if ((size_t)dce->mqtt_rx_topic_len + chunk_len >= sizeof(dce->mqtt_rx_topic)) {
                log_error(TAG, "RX topic overflows buffer (have %u + chunk %u >= %u) — dropping message",
                          (unsigned)dce->mqtt_rx_topic_len, chunk_len, (unsigned)sizeof(dce->mqtt_rx_topic));
                urc_reset_rx_state(dce);
                return;
            }
            dce->urc_chunk_remaining = (uint16_t)chunk_len;
        } else {
            log_error(TAG, "Unexpected +CMQTTRXTOPIC: [%s] (rx_state=%d)", line, dce->mqtt_rx_state);
            urc_reset_rx_state(dce);
        }
        return;
    }

    if (strstr(line, "+CMQTTRXPAYLOAD:")) {
        unsigned idx = 0, chunk_len = 0;
        char *p = strchr(line, ':');
        if ((dce->mqtt_rx_state == A7677S_MQTT_RX_TOPIC ||
             dce->mqtt_rx_state == A7677S_MQTT_RX_PAYLOAD) && p &&
            sscanf(p + 1, "%u,%u", &idx, &chunk_len) == 2 && chunk_len > 0) {
            if ((size_t)dce->mqtt_rx_payload_len + chunk_len >= sizeof(dce->mqtt_rx_payload)) {
                log_error(TAG, "RX payload overflows buffer (have %u + chunk %u >= %u) — dropping message",
                          (unsigned)dce->mqtt_rx_payload_len, chunk_len, (unsigned)sizeof(dce->mqtt_rx_payload));
                urc_reset_rx_state(dce);
                return;
            }
            dce->mqtt_rx_state       = A7677S_MQTT_RX_PAYLOAD;
            dce->urc_chunk_remaining = (uint16_t)chunk_len;
        } else {
            log_error(TAG, "Unexpected +CMQTTRXPAYLOAD: [%s] (rx_state=%d)", line, dce->mqtt_rx_state);
            urc_reset_rx_state(dce);
        }
        return;
    }

    if (strstr(line, "+CMQTTRXEND:")) {
        if (dce->mqtt_rx_topic_len   < dce->mqtt_rx_topic_total_len ||
            dce->mqtt_rx_payload_len < dce->mqtt_rx_payload_total_len) {
            log_warn(TAG, "RXEND before topic/payload fully accumulated (topic %u/%u, payload %u/%u) — delivering partial message",
                      (unsigned)dce->mqtt_rx_topic_len, (unsigned)dce->mqtt_rx_topic_total_len,
                      (unsigned)dce->mqtt_rx_payload_len, (unsigned)dce->mqtt_rx_payload_total_len);
        }
        dce->mqtt_rx_topic[dce->mqtt_rx_topic_len]     = '\0';
        dce->mqtt_rx_payload[dce->mqtt_rx_payload_len] = '\0';
        if (dce->mqtt_incoming_cb) {
            dce->mqtt_incoming_cb(dce->mqtt_rx_topic, dce->mqtt_rx_topic_len,
                                   dce->mqtt_rx_payload, dce->mqtt_rx_payload_len,
                                   dce->mqtt_user_ctx);
        }
        urc_reset_rx_state(dce);
        return;
    }

    if (strstr(line, "+CMQTTCONNLOST:")) {
        log_warn(TAG, "MQTT connection lost (URC): %s", line);
        dce->mqtt_state = A7677S_MQTT_IDLE;
        urc_reset_rx_state(dce);
        if (dce->mqtt_connlost_cb) dce->mqtt_connlost_cb(dce->mqtt_user_ctx);
        return;
    }

    if (strstr(line, "+CGEV: ME PDN DEACT") || strstr(line, "+CGEV: NW PDN DEACT")) {
        log_warn(TAG, "PDN deactivated (URC) — restarting network attach sequence");
        /* A7677S_INIT_AT doubles as the "restart the whole sequence" entry
         * point (see a7677s_init_state_t comment) — reuse it rather than
         * inventing a second restart path. */
        dce->init_state       = A7677S_INIT_AT;
        dce->init_elapsed     = 0;
        dce->init_cmd_pending = 0;
        dce->mqtt_state       = A7677S_MQTT_IDLE;
        urc_reset_rx_state(dce);
        /* MQTT cannot survive the PDN going down even if CMQTTCONNLOST never
         * arrives separately — tell the service layer now so its reconnect
         * logic starts retrying (harmlessly failing via is_ready()==false
         * until re-attach completes, then succeeding on its own). */
        if (dce->mqtt_connlost_cb) dce->mqtt_connlost_cb(dce->mqtt_user_ctx);
        return;
    }

    /* Unrecognized line (e.g. +CREG:, +CPIN:, other chatter arriving
     * outside a command exchange) — ignore. */
}

static void urc_poll(a7677s_t *dce)
{
    int available = sx_uart_available(&dce->base.uart);
    if (available <= 0) return;

    int space = (int)sizeof(dce->urc_buf) - 1 - (int)dce->urc_buf_len;
    if (space <= 0) {
        /* A header line longer than A7677S_URC_BUF_SIZE with no "\r\n" in
         * sight is not a real modem response — defensive reset only. */
        log_error(TAG, "URC buffer full without a line terminator — resetting (buf=[%.*s])",
                  (int)dce->urc_buf_len, dce->urc_buf);
        dce->urc_buf_len = 0;
        urc_reset_rx_state(dce);
        space = (int)sizeof(dce->urc_buf) - 1;
    }
    if (available > space) available = space;

    int read = sx_uart_read(&dce->base.uart, (uint8_t *)dce->urc_buf + dce->urc_buf_len,
                             available, 10);
    if (read <= 0) return;
    dce->urc_buf_len += (uint16_t)read;
    dce->urc_buf[dce->urc_buf_len] = '\0';

    /* Drain every complete unit (header line or raw chunk) currently fully
     * present in urc_buf. Loops because one UART read can easily contain
     * more than one complete line/chunk (e.g. RXTOPIC header + full small
     * topic + RXPAYLOAD header all in one burst). */
    for (;;) {
        if (dce->urc_chunk_remaining > 0) {
            uint16_t take = dce->urc_buf_len < dce->urc_chunk_remaining
                             ? dce->urc_buf_len : dce->urc_chunk_remaining;
            if (take == 0) break;

            if (dce->mqtt_rx_state == A7677S_MQTT_RX_TOPIC) {
                memcpy(dce->mqtt_rx_topic + dce->mqtt_rx_topic_len, dce->urc_buf, take);
                dce->mqtt_rx_topic_len += take;
            } else if (dce->mqtt_rx_state == A7677S_MQTT_RX_PAYLOAD) {
                memcpy(dce->mqtt_rx_payload + dce->mqtt_rx_payload_len, dce->urc_buf, take);
                dce->mqtt_rx_payload_len += take;
            }
            dce->urc_chunk_remaining -= take;

            memmove(dce->urc_buf, dce->urc_buf + take, dce->urc_buf_len - take);
            dce->urc_buf_len -= take;
            dce->urc_buf[dce->urc_buf_len] = '\0';

            if (dce->urc_chunk_remaining > 0) break; /* need more bytes next poll */

            /* The modem sends a trailing "\r\n" after each data chunk,
             * before the next header line. Consume it now if it is already
             * here; if not, the blank-line skip below handles it next
             * iteration once it arrives. */
            if (dce->urc_buf_len >= 2 && dce->urc_buf[0] == '\r' && dce->urc_buf[1] == '\n') {
                memmove(dce->urc_buf, dce->urc_buf + 2, dce->urc_buf_len - 2);
                dce->urc_buf_len -= 2;
                dce->urc_buf[dce->urc_buf_len] = '\0';
            }
            continue;
        }

        char *eol = strstr(dce->urc_buf, "\r\n");
        if (!eol) break; /* incomplete line — wait for more bytes */

        size_t line_len = (size_t)(eol - dce->urc_buf);
        if (line_len == 0) {
            /* blank line between URCs — consume and continue */
            memmove(dce->urc_buf, dce->urc_buf + 2, dce->urc_buf_len - 2);
            dce->urc_buf_len -= 2;
            dce->urc_buf[dce->urc_buf_len] = '\0';
            continue;
        }

        char line[A7677S_URC_BUF_SIZE];
        memcpy(line, dce->urc_buf, line_len);
        line[line_len] = '\0';

        size_t consumed = line_len + 2; /* + "\r\n" */
        memmove(dce->urc_buf, dce->urc_buf + consumed, dce->urc_buf_len - consumed);
        dce->urc_buf_len -= (uint16_t)consumed;
        dce->urc_buf[dce->urc_buf_len] = '\0';

        urc_process_header_line(dce, line);
        /* Loop again — urc_process_header_line() may have set
         * urc_chunk_remaining > 0 (RXTOPIC/RXPAYLOAD header), which the top
         * of this loop now handles against whatever remains in urc_buf. */
    }
}

void a7677s_mqtt_register_callbacks(a7677s_t *dce,
                                     mqtt_incoming_cb_t incoming_cb,
                                     mqtt_connlost_cb_t connlost_cb,
                                     void *user_ctx)
{
    dce->mqtt_incoming_cb = incoming_cb;
    dce->mqtt_connlost_cb = connlost_cb;
    dce->mqtt_user_ctx    = user_ctx;
}

/* modem_ops_t vtable adapter — identical signature to
 * a7677s_mqtt_register_callbacks() above, just reached through the generic
 * interface so sx_mqtt.c never has to include a7677s.h. */
static void a7677s_mqtt_set_callbacks(void *ctx, mqtt_incoming_cb_t incoming_cb,
                                       mqtt_connlost_cb_t connlost_cb,
                                       void *user_ctx)
{
    a7677s_mqtt_register_callbacks((a7677s_t *)ctx, incoming_cb, connlost_cb, user_ctx);
}

/* Fire the in-flight op callback and clear it so it cannot fire twice. */
static void mqtt_op_done(a7677s_t *dce, modem_ops_result_t result)
{
    mqtt_cb_t cb = dce->mqtt_op_cb;
    dce->mqtt_op_cb = NULL;
    if (cb) cb(result, dce);
}

/* Send a dynamically-built MQTT AT command stored in s_mqtt_dyn_cmd_buf. */
static void send_mqtt_dynamic(a7677s_t *dce, const char *cmd_str,
                               const char *res_success, const char *res_fail,
                               modem_command_response_callback_t cb,
                               uint32_t timeout_ms)
{
    command[CMD_DYNAMIC].cmd         = cmd_str;
    command[CMD_DYNAMIC].res_success = res_success;
    command[CMD_DYNAMIC].res_fail    = res_fail;
    command[CMD_DYNAMIC].callback    = cb;
    command[CMD_DYNAMIC].arg         = dce;
    dce->mqtt_cmd_pending = 1;
    log_debug(TAG, "MQTT CMD: %s", cmd_str);
    modem_send_command(pModem(dce), &command[CMD_DYNAMIC], timeout_ms);
}

/* ------------------------------------------------------------------ */
/*  MQTT — connect sequence                                             */
/* ------------------------------------------------------------------ */
/* START -> ACCQ -> CONNECT -> CONNECTED
 *
 * Key design: several MQTT commands return "OK" first, then a separate URC
 * line carries the real errcode (e.g. "+CMQTTSTART:0").  We set res_success
 * to the full URC with errcode=0 so the callback only fires on genuine
 * success.  modem_command checks res_success before res_fail, so ":0\r\n"
 * is caught as success and any other "+CMQTTSTART:N" is caught by res_fail
 * (which is a substring match on "ERROR\r\n" — the modem always sends
 * "ERROR\r\n" on hard failures; for URC errcode failures we rely on timeout
 * since the modem sends OK + bad URC without an ERROR line, so the AT layer
 * will eventually time out and fire the callback with TIMEOUT). */

static void cb_mqtt_start(modem_t *modem, const char *response,
                           modem_response_st_t res, void *arg)
{
    a7677s_t *dce = pDCE(arg);
    dce->mqtt_cmd_pending = 0;

    if (res != MODEM_RESPONSE_SUCCESS) {
        log_error(TAG, "AT+CMQTTSTART failed (res=%d)", res);
        dce->mqtt_state = A7677S_MQTT_IDLE;
        mqtt_op_done(dce, (res == MODEM_RESPONSE_TIMEOUT) ? MODEM_OPS_TIMEOUT : MODEM_OPS_ERROR);
        return;
    }

    /* TLS: run the cert-upload/SSL-config block before ACCQ. Plain TCP
     * skips straight to ACCQ, unchanged from before this TLS support was
     * added. */
    if (dce->mqtt_use_ssl) {
        begin_ssl_cert_block(dce);
    } else {
        advance_to_accq(dce);
    }
}

/* Shared tail of the connect sequence: AT+CMQTTACCQ then AT+CMQTTCONNECT.
 * Reached either directly (use_ssl=0) or after the TLS cert/SSLCFG block
 * completes (use_ssl=1, right after cb_mqtt_sslbind() succeeds). */
static void advance_to_accq(a7677s_t *dce)
{
    dce->mqtt_state = A7677S_MQTT_ACCQ;
    snprintf(s_mqtt_dyn_cmd_buf, sizeof(s_mqtt_dyn_cmd_buf),
             "AT+CMQTTACCQ=%u,\"%s\",%u\r\n",
             A7677S_MQTT_CLIENT_INDEX, dce->mqtt_client_id, (unsigned)dce->mqtt_use_ssl);
    send_mqtt_dynamic(dce, s_mqtt_dyn_cmd_buf, "\r\nOK\r\n", "\r\nERROR\r\n",
                      cb_mqtt_accq, A7677S_TIMEOUT_AT);
}

/* ------------------------------------------------------------------ */
/*  MQTT — TLS cert-upload block (use_ssl=1 only)                       */
/* ------------------------------------------------------------------ */
/* Sequence, each pair skipped entirely if the corresponding cert was not
 * supplied to a7677s_mqtt_connect():
 *   [CERT_CA_PROMPT -> CERT_CA_DATA]
 *   [CERT_CLIENT_PROMPT -> CERT_CLIENT_DATA]
 *   [CERT_KEY_PROMPT -> CERT_KEY_DATA]
 *   -> begin_sslcfg_block()
 * AT+CCERTDOWN="<filename>",<len> waits for ">" then the raw PEM bytes are
 * sent as a second AT-layer write, waiting for OK. Per a76xx_at_cmd.md
 * 19.2.2, MaxResponseTime is 120000 ms, hence A7677S_TIMEOUT_CERT_CMD. */

static void begin_ssl_cert_block(a7677s_t *dce)
{
    if (dce->mqtt_cert_has_ca) {
        dce->mqtt_state = A7677S_MQTT_CERT_CA_PROMPT;
        snprintf(s_mqtt_dyn_cmd_buf, sizeof(s_mqtt_dyn_cmd_buf),
                 "AT+CCERTDOWN=\"%s\",%u\r\n",
                 A7677S_CERT_FILENAME_CA, (unsigned)dce->mqtt_cert_ca_len);
        send_mqtt_dynamic(dce, s_mqtt_dyn_cmd_buf, ">", "\r\nERROR\r\n",
                          cb_mqtt_cert_ca_prompt, A7677S_TIMEOUT_CERT_CMD);
        return;
    }
    if (dce->mqtt_cert_has_client) {
        dce->mqtt_state = A7677S_MQTT_CERT_CLIENT_PROMPT;
        snprintf(s_mqtt_dyn_cmd_buf, sizeof(s_mqtt_dyn_cmd_buf),
                 "AT+CCERTDOWN=\"%s\",%u\r\n",
                 A7677S_CERT_FILENAME_CLIENT, (unsigned)dce->mqtt_cert_client_len);
        send_mqtt_dynamic(dce, s_mqtt_dyn_cmd_buf, ">", "\r\nERROR\r\n",
                          cb_mqtt_cert_client_prompt, A7677S_TIMEOUT_CERT_CMD);
        return;
    }
    if (dce->mqtt_cert_has_key) {
        dce->mqtt_state = A7677S_MQTT_CERT_KEY_PROMPT;
        snprintf(s_mqtt_dyn_cmd_buf, sizeof(s_mqtt_dyn_cmd_buf),
                 "AT+CCERTDOWN=\"%s\",%u\r\n",
                 A7677S_CERT_FILENAME_KEY, (unsigned)dce->mqtt_cert_key_len);
        send_mqtt_dynamic(dce, s_mqtt_dyn_cmd_buf, ">", "\r\nERROR\r\n",
                          cb_mqtt_cert_key_prompt, A7677S_TIMEOUT_CERT_CMD);
        return;
    }
    /* No certs at all — still run SSLCFG (authmode=0, filenames "") so the
     * modem knows this SSL context has no client-side trust material. */
    begin_sslcfg_block(dce);
}

static void cb_mqtt_cert_ca_prompt(modem_t *modem, const char *response,
                                    modem_response_st_t res, void *arg)
{
    a7677s_t *dce = pDCE(arg);
    dce->mqtt_cmd_pending = 0;

    if (res != MODEM_RESPONSE_SUCCESS) {
        log_error(TAG, "AT+CCERTDOWN (ca) prompt failed (res=%d)", res);
        dce->mqtt_state = A7677S_MQTT_IDLE;
        mqtt_op_done(dce, MODEM_OPS_ERROR);
        return;
    }

    /* Pass the pointer directly — same reasoning as cb_mqtt_pub_topic()/
     * cb_mqtt_pub_payload() above: s_mqtt_dyn_cmd_buf is only 384 bytes,
     * far smaller than A7677S_CERT_PEM_MAX (10241 bytes), and copying
     * through it would silently truncate the cert while AT+CCERTDOWN has
     * already told the modem the untruncated length. */
    dce->mqtt_state = A7677S_MQTT_CERT_CA_DATA;
    send_mqtt_dynamic(dce, dce->mqtt_cert_ca, "\r\nOK\r\n", "\r\nERROR\r\n",
                      cb_mqtt_cert_ca_data, A7677S_TIMEOUT_CERT_CMD);
}

static void cb_mqtt_cert_ca_data(modem_t *modem, const char *response,
                                  modem_response_st_t res, void *arg)
{
    a7677s_t *dce = pDCE(arg);
    dce->mqtt_cmd_pending = 0;

    if (res != MODEM_RESPONSE_SUCCESS) {
        log_error(TAG, "CA cert upload failed (res=%d)", res);
        dce->mqtt_state = A7677S_MQTT_IDLE;
        mqtt_op_done(dce, MODEM_OPS_ERROR);
        return;
    }

    if (dce->mqtt_cert_has_client) {
        dce->mqtt_state = A7677S_MQTT_CERT_CLIENT_PROMPT;
        snprintf(s_mqtt_dyn_cmd_buf, sizeof(s_mqtt_dyn_cmd_buf),
                 "AT+CCERTDOWN=\"%s\",%u\r\n",
                 A7677S_CERT_FILENAME_CLIENT, (unsigned)dce->mqtt_cert_client_len);
        send_mqtt_dynamic(dce, s_mqtt_dyn_cmd_buf, ">", "\r\nERROR\r\n",
                          cb_mqtt_cert_client_prompt, A7677S_TIMEOUT_CERT_CMD);
        return;
    }
    if (dce->mqtt_cert_has_key) {
        dce->mqtt_state = A7677S_MQTT_CERT_KEY_PROMPT;
        snprintf(s_mqtt_dyn_cmd_buf, sizeof(s_mqtt_dyn_cmd_buf),
                 "AT+CCERTDOWN=\"%s\",%u\r\n",
                 A7677S_CERT_FILENAME_KEY, (unsigned)dce->mqtt_cert_key_len);
        send_mqtt_dynamic(dce, s_mqtt_dyn_cmd_buf, ">", "\r\nERROR\r\n",
                          cb_mqtt_cert_key_prompt, A7677S_TIMEOUT_CERT_CMD);
        return;
    }
    begin_sslcfg_block(dce);
}

static void cb_mqtt_cert_client_prompt(modem_t *modem, const char *response,
                                        modem_response_st_t res, void *arg)
{
    a7677s_t *dce = pDCE(arg);
    dce->mqtt_cmd_pending = 0;

    if (res != MODEM_RESPONSE_SUCCESS) {
        log_error(TAG, "AT+CCERTDOWN (client) prompt failed (res=%d)", res);
        dce->mqtt_state = A7677S_MQTT_IDLE;
        mqtt_op_done(dce, MODEM_OPS_ERROR);
        return;
    }

    dce->mqtt_state = A7677S_MQTT_CERT_CLIENT_DATA;
    send_mqtt_dynamic(dce, dce->mqtt_cert_client, "\r\nOK\r\n", "\r\nERROR\r\n",
                      cb_mqtt_cert_client_data, A7677S_TIMEOUT_CERT_CMD);
}

static void cb_mqtt_cert_client_data(modem_t *modem, const char *response,
                                      modem_response_st_t res, void *arg)
{
    a7677s_t *dce = pDCE(arg);
    dce->mqtt_cmd_pending = 0;

    if (res != MODEM_RESPONSE_SUCCESS) {
        log_error(TAG, "client cert upload failed (res=%d)", res);
        dce->mqtt_state = A7677S_MQTT_IDLE;
        mqtt_op_done(dce, MODEM_OPS_ERROR);
        return;
    }

    if (dce->mqtt_cert_has_key) {
        dce->mqtt_state = A7677S_MQTT_CERT_KEY_PROMPT;
        snprintf(s_mqtt_dyn_cmd_buf, sizeof(s_mqtt_dyn_cmd_buf),
                 "AT+CCERTDOWN=\"%s\",%u\r\n",
                 A7677S_CERT_FILENAME_KEY, (unsigned)dce->mqtt_cert_key_len);
        send_mqtt_dynamic(dce, s_mqtt_dyn_cmd_buf, ">", "\r\nERROR\r\n",
                          cb_mqtt_cert_key_prompt, A7677S_TIMEOUT_CERT_CMD);
        return;
    }
    begin_sslcfg_block(dce);
}

static void cb_mqtt_cert_key_prompt(modem_t *modem, const char *response,
                                     modem_response_st_t res, void *arg)
{
    a7677s_t *dce = pDCE(arg);
    dce->mqtt_cmd_pending = 0;

    if (res != MODEM_RESPONSE_SUCCESS) {
        log_error(TAG, "AT+CCERTDOWN (key) prompt failed (res=%d)", res);
        dce->mqtt_state = A7677S_MQTT_IDLE;
        mqtt_op_done(dce, MODEM_OPS_ERROR);
        return;
    }

    dce->mqtt_state = A7677S_MQTT_CERT_KEY_DATA;
    send_mqtt_dynamic(dce, dce->mqtt_cert_key, "\r\nOK\r\n", "\r\nERROR\r\n",
                      cb_mqtt_cert_key_data, A7677S_TIMEOUT_CERT_CMD);
}

static void cb_mqtt_cert_key_data(modem_t *modem, const char *response,
                                   modem_response_st_t res, void *arg)
{
    a7677s_t *dce = pDCE(arg);
    dce->mqtt_cmd_pending = 0;

    if (res != MODEM_RESPONSE_SUCCESS) {
        log_error(TAG, "client key upload failed (res=%d)", res);
        dce->mqtt_state = A7677S_MQTT_IDLE;
        mqtt_op_done(dce, MODEM_OPS_ERROR);
        return;
    }

    /* Key is always last in the block regardless of which certs were
     * present, so the next step is always SSLCFG. */
    begin_sslcfg_block(dce);
}

/* ------------------------------------------------------------------ */
/*  MQTT — TLS SSL-config block (use_ssl=1 only)                        */
/* ------------------------------------------------------------------ */
/* Always runs when use_ssl=1, even with zero certs uploaded (that case
 * still needs AT+CSSLCFG="authmode",0,0 and empty filenames so the modem
 * does not try to use stale cert files from a previous session).
 * Sequence: SSLCFG_CACERT -> SSLCFG_CLIENTCERT -> SSLCFG_CLIENTKEY ->
 * SSLCFG_AUTHMODE -> SSLBIND -> advance_to_accq(). */

static void begin_sslcfg_block(a7677s_t *dce)
{
    dce->mqtt_state = A7677S_MQTT_SSLCFG_CACERT;
    snprintf(s_mqtt_dyn_cmd_buf, sizeof(s_mqtt_dyn_cmd_buf),
             "AT+CSSLCFG=\"cacert\",%u,\"%s\"\r\n",
             A7677S_SSL_CTX_INDEX,
             dce->mqtt_cert_has_ca ? A7677S_CERT_FILENAME_CA : "");
    send_mqtt_dynamic(dce, s_mqtt_dyn_cmd_buf, "\r\nOK\r\n", "\r\nERROR\r\n",
                      cb_mqtt_sslcfg_cacert, A7677S_TIMEOUT_CERT_CMD);
}

static void cb_mqtt_sslcfg_cacert(modem_t *modem, const char *response,
                                   modem_response_st_t res, void *arg)
{
    a7677s_t *dce = pDCE(arg);
    dce->mqtt_cmd_pending = 0;

    if (res != MODEM_RESPONSE_SUCCESS) {
        log_error(TAG, "AT+CSSLCFG cacert failed (res=%d)", res);
        dce->mqtt_state = A7677S_MQTT_IDLE;
        mqtt_op_done(dce, MODEM_OPS_ERROR);
        return;
    }

    dce->mqtt_state = A7677S_MQTT_SSLCFG_CLIENTCERT;
    snprintf(s_mqtt_dyn_cmd_buf, sizeof(s_mqtt_dyn_cmd_buf),
             "AT+CSSLCFG=\"clientcert\",%u,\"%s\"\r\n",
             A7677S_SSL_CTX_INDEX,
             dce->mqtt_cert_has_client ? A7677S_CERT_FILENAME_CLIENT : "");
    send_mqtt_dynamic(dce, s_mqtt_dyn_cmd_buf, "\r\nOK\r\n", "\r\nERROR\r\n",
                      cb_mqtt_sslcfg_clientcert, A7677S_TIMEOUT_CERT_CMD);
}

static void cb_mqtt_sslcfg_clientcert(modem_t *modem, const char *response,
                                       modem_response_st_t res, void *arg)
{
    a7677s_t *dce = pDCE(arg);
    dce->mqtt_cmd_pending = 0;

    if (res != MODEM_RESPONSE_SUCCESS) {
        log_error(TAG, "AT+CSSLCFG clientcert failed (res=%d)", res);
        dce->mqtt_state = A7677S_MQTT_IDLE;
        mqtt_op_done(dce, MODEM_OPS_ERROR);
        return;
    }

    dce->mqtt_state = A7677S_MQTT_SSLCFG_CLIENTKEY;
    snprintf(s_mqtt_dyn_cmd_buf, sizeof(s_mqtt_dyn_cmd_buf),
             "AT+CSSLCFG=\"clientkey\",%u,\"%s\"\r\n",
             A7677S_SSL_CTX_INDEX,
             dce->mqtt_cert_has_key ? A7677S_CERT_FILENAME_KEY : "");
    send_mqtt_dynamic(dce, s_mqtt_dyn_cmd_buf, "\r\nOK\r\n", "\r\nERROR\r\n",
                      cb_mqtt_sslcfg_clientkey, A7677S_TIMEOUT_CERT_CMD);
}

static void cb_mqtt_sslcfg_clientkey(modem_t *modem, const char *response,
                                      modem_response_st_t res, void *arg)
{
    a7677s_t *dce = pDCE(arg);
    dce->mqtt_cmd_pending = 0;

    if (res != MODEM_RESPONSE_SUCCESS) {
        log_error(TAG, "AT+CSSLCFG clientkey failed (res=%d)", res);
        dce->mqtt_state = A7677S_MQTT_IDLE;
        mqtt_op_done(dce, MODEM_OPS_ERROR);
        return;
    }

    dce->mqtt_state = A7677S_MQTT_SSLCFG_AUTHMODE;
    snprintf(s_mqtt_dyn_cmd_buf, sizeof(s_mqtt_dyn_cmd_buf),
             "AT+CSSLCFG=\"authmode\",%u,%u\r\n",
             A7677S_SSL_CTX_INDEX, (unsigned)dce->mqtt_ssl_authmode);
    send_mqtt_dynamic(dce, s_mqtt_dyn_cmd_buf, "\r\nOK\r\n", "\r\nERROR\r\n",
                      cb_mqtt_sslcfg_authmode, A7677S_TIMEOUT_CERT_CMD);
}

static void cb_mqtt_sslcfg_authmode(modem_t *modem, const char *response,
                                     modem_response_st_t res, void *arg)
{
    a7677s_t *dce = pDCE(arg);
    dce->mqtt_cmd_pending = 0;

    if (res != MODEM_RESPONSE_SUCCESS) {
        log_error(TAG, "AT+CSSLCFG authmode failed (res=%d)", res);
        dce->mqtt_state = A7677S_MQTT_IDLE;
        mqtt_op_done(dce, MODEM_OPS_ERROR);
        return;
    }

    /* AT+CMQTTSSLCFG=<client_index>,<ssl_ctx_index> binds the SSL context
     * just configured above to the MQTT client we are about to ACCQ. */
    dce->mqtt_state = A7677S_MQTT_SSLBIND;
    snprintf(s_mqtt_dyn_cmd_buf, sizeof(s_mqtt_dyn_cmd_buf),
             "AT+CMQTTSSLCFG=%u,%u\r\n",
             A7677S_MQTT_CLIENT_INDEX, A7677S_SSL_CTX_INDEX);
    send_mqtt_dynamic(dce, s_mqtt_dyn_cmd_buf, "\r\nOK\r\n", "\r\nERROR\r\n",
                      cb_mqtt_sslbind, A7677S_TIMEOUT_AT);
}

static void cb_mqtt_sslbind(modem_t *modem, const char *response,
                             modem_response_st_t res, void *arg)
{
    a7677s_t *dce = pDCE(arg);
    dce->mqtt_cmd_pending = 0;

    if (res != MODEM_RESPONSE_SUCCESS) {
        log_error(TAG, "AT+CMQTTSSLCFG bind failed (res=%d)", res);
        dce->mqtt_state = A7677S_MQTT_IDLE;
        mqtt_op_done(dce, MODEM_OPS_ERROR);
        return;
    }

    /* SSL context is bound — continue exactly as the plain-TCP path does. */
    advance_to_accq(dce);
}

static void cb_mqtt_accq(modem_t *modem, const char *response,
                          modem_response_st_t res, void *arg)
{
    a7677s_t *dce = pDCE(arg);
    dce->mqtt_cmd_pending = 0;

    if (res != MODEM_RESPONSE_SUCCESS) {
        log_error(TAG, "AT+CMQTTACCQ failed (res=%d)", res);
        dce->mqtt_state = A7677S_MQTT_IDLE;
        mqtt_op_done(dce, MODEM_OPS_ERROR);
        return;
    }

    /* AT+CMQTTCFG="version",<idx>,<value> must be sent after ACCQ and
     * before CONNECT per datasheet. Fixed at MQTT 3.1.1
     * (A7677S_MQTT_PROTOCOL_VERSION); this project does not expose a way to
     * select 3.1 vs 3.1.1 per-connection since 3.1.1 is accepted by every
     * broker this project targets. */
    dce->mqtt_state = A7677S_MQTT_CFG_VERSION;
    snprintf(s_mqtt_dyn_cmd_buf, sizeof(s_mqtt_dyn_cmd_buf),
             "AT+CMQTTCFG=\"version\",%u,%u\r\n",
             A7677S_MQTT_CLIENT_INDEX, A7677S_MQTT_PROTOCOL_VERSION);
    send_mqtt_dynamic(dce, s_mqtt_dyn_cmd_buf, "\r\nOK\r\n", "\r\nERROR\r\n",
                      cb_mqtt_cfg_version, A7677S_TIMEOUT_AT);
}

static void cb_mqtt_cfg_version(modem_t *modem, const char *response,
                                 modem_response_st_t res, void *arg)
{
    a7677s_t *dce = pDCE(arg);
    dce->mqtt_cmd_pending = 0;

    if (res != MODEM_RESPONSE_SUCCESS) {
        log_error(TAG, "AT+CMQTTCFG=\"version\" failed (res=%d)", res);
        dce->mqtt_state = A7677S_MQTT_IDLE;
        mqtt_op_done(dce, MODEM_OPS_ERROR);
        return;
    }

    dce->mqtt_state = A7677S_MQTT_CONNECT;
    if (dce->mqtt_username[0] && dce->mqtt_password[0]) {
        snprintf(s_mqtt_dyn_cmd_buf, sizeof(s_mqtt_dyn_cmd_buf),
                 "AT+CMQTTCONNECT=%u,\"%s\",%u,%u,\"%s\",\"%s\"\r\n",
                 A7677S_MQTT_CLIENT_INDEX, dce->mqtt_broker,
                 (unsigned)dce->mqtt_keepalive, (unsigned)dce->mqtt_clean_session,
                 dce->mqtt_username, dce->mqtt_password);
    } else {
        snprintf(s_mqtt_dyn_cmd_buf, sizeof(s_mqtt_dyn_cmd_buf),
                 "AT+CMQTTCONNECT=%u,\"%s\",%u,%u\r\n",
                 A7677S_MQTT_CLIENT_INDEX, dce->mqtt_broker,
                 (unsigned)dce->mqtt_keepalive, (unsigned)dce->mqtt_clean_session);
    }
    send_mqtt_dynamic(dce, s_mqtt_dyn_cmd_buf,
                      "\r\n+CMQTTCONNECT:0,0\r\n", "\r\nERROR\r\n",
                      cb_mqtt_connect, A7677S_TIMEOUT_MQTT_CONNECT);
}

static void cb_mqtt_connect(modem_t *modem, const char *response,
                             modem_response_st_t res, void *arg)
{
    a7677s_t *dce = pDCE(arg);
    dce->mqtt_cmd_pending = 0;

    if (res != MODEM_RESPONSE_SUCCESS) {
        if (response) {
            const char *p = strstr(response, "+CMQTTCONNECT:");
            if (p) {
                int ci, err;
                if (sscanf(p, "+CMQTTCONNECT:%d,%d", &ci, &err) == 2)
                    log_error(TAG, "AT+CMQTTCONNECT errcode=%d", err);
            }
        }
        dce->mqtt_state = A7677S_MQTT_IDLE;
        mqtt_op_done(dce, (res == MODEM_RESPONSE_TIMEOUT) ? MODEM_OPS_TIMEOUT : MODEM_OPS_ERROR);
        return;
    }

    log_info(TAG, "MQTT connected to %s", dce->mqtt_broker);
    dce->mqtt_state = A7677S_MQTT_CONNECTED;
    mqtt_op_done(dce, MODEM_OPS_OK);
}

/* ------------------------------------------------------------------ */
/*  MQTT — disconnect sequence                                          */
/* ------------------------------------------------------------------ */
/* DISC -> REL -> STOP -> IDLE
 * DISC/REL failures are non-fatal for the sequence: we always proceed to
 * the next step to free resources and reach a clean IDLE state. */

static void cb_mqtt_disc(modem_t *modem, const char *response,
                          modem_response_st_t res, void *arg)
{
    a7677s_t *dce = pDCE(arg);
    dce->mqtt_cmd_pending = 0;

    if (res != MODEM_RESPONSE_SUCCESS)
        log_warn(TAG, "AT+CMQTTDISC did not complete cleanly (res=%d), continuing", res);

    dce->mqtt_state = A7677S_MQTT_REL;
    snprintf(s_mqtt_dyn_cmd_buf, sizeof(s_mqtt_dyn_cmd_buf),
             "AT+CMQTTREL=%u\r\n", A7677S_MQTT_CLIENT_INDEX);
    send_mqtt_dynamic(dce, s_mqtt_dyn_cmd_buf, "\r\nOK\r\n", "\r\nERROR\r\n",
                      cb_mqtt_rel, A7677S_TIMEOUT_AT);
}

static void cb_mqtt_rel(modem_t *modem, const char *response,
                         modem_response_st_t res, void *arg)
{
    a7677s_t *dce = pDCE(arg);
    dce->mqtt_cmd_pending = 0;

    if (res != MODEM_RESPONSE_SUCCESS)
        log_warn(TAG, "AT+CMQTTREL did not complete cleanly (res=%d), continuing", res);

    dce->mqtt_state = A7677S_MQTT_STOP;
    send_mqtt_dynamic(dce, "AT+CMQTTSTOP\r\n",
                      "\r\n+CMQTTSTOP:0\r\n", "\r\nERROR\r\n",
                      cb_mqtt_stop, A7677S_TIMEOUT_MQTT_START);
}

static void cb_mqtt_stop(modem_t *modem, const char *response,
                          modem_response_st_t res, void *arg)
{
    a7677s_t *dce = pDCE(arg);
    dce->mqtt_cmd_pending = 0;

    if (res != MODEM_RESPONSE_SUCCESS)
        log_warn(TAG, "AT+CMQTTSTOP did not complete cleanly (res=%d)", res);

    log_info(TAG, "MQTT session closed");
    dce->mqtt_state = A7677S_MQTT_IDLE;
    mqtt_op_done(dce, (res == MODEM_RESPONSE_SUCCESS) ? MODEM_OPS_OK : MODEM_OPS_ERROR);
}

/* ------------------------------------------------------------------ */
/*  MQTT — publish sequence                                             */
/* ------------------------------------------------------------------ */
/* PUB_TOPIC (wait ">") -> PUB_TOPIC_DATA (wait OK) ->
 * PUB_PAYLOAD (wait ">") -> PUB_PAYLOAD_DATA (wait OK) ->
 * PUB_SEND (wait +CMQTTPUB:0,0) -> CONNECTED */

static void cb_mqtt_pub_topic(modem_t *modem, const char *response,
                               modem_response_st_t res, void *arg)
{
    a7677s_t *dce = pDCE(arg);
    dce->mqtt_cmd_pending = 0;

    if (res != MODEM_RESPONSE_SUCCESS) {
        log_error(TAG, "AT+CMQTTTOPIC prompt failed (res=%d)", res);
        dce->mqtt_state = A7677S_MQTT_CONNECTED;
        mqtt_op_done(dce, MODEM_OPS_ERROR);
        return;
    }

    /* Send raw topic bytes — modem has already accepted req_length so it
     * reads exactly that many bytes without needing \r\n.
     * BUGFIX: pass dce->mqtt_pub_topic directly instead of copying through
     * s_mqtt_dyn_cmd_buf (384 bytes). send_mqtt_dynamic()/command[].cmd only
     * stores the pointer (no copy), and modem_send_command()/sx_uart_write()
     * has no 384-byte limit of its own (it transmits exactly strlen(cmd)
     * bytes via HAL_UART_Transmit()). Topic max is A7677S_MQTT_TOPIC_MAX
     * (1025 bytes) which already exceeds the old scratch buffer, so the old
     * strncpy() silently truncated any topic longer than 383 bytes while the
     * AT+CMQTTTOPIC=...,<len> command line had already told the modem the
     * untruncated length — causing the modem to wait for bytes that never
     * arrived (timeout) or to misread subsequent AT traffic as topic data. */
    dce->mqtt_state = A7677S_MQTT_PUB_TOPIC_DATA;
    send_mqtt_dynamic(dce, dce->mqtt_pub_topic, "\r\nOK\r\n", "\r\nERROR\r\n",
                      cb_mqtt_pub_topic_data, A7677S_TIMEOUT_AT);
}

static void cb_mqtt_pub_topic_data(modem_t *modem, const char *response,
                                    modem_response_st_t res, void *arg)
{
    a7677s_t *dce = pDCE(arg);
    dce->mqtt_cmd_pending = 0;

    if (res != MODEM_RESPONSE_SUCCESS) {
        log_error(TAG, "MQTT topic data failed (res=%d)", res);
        dce->mqtt_state = A7677S_MQTT_CONNECTED;
        mqtt_op_done(dce, MODEM_OPS_ERROR);
        return;
    }

    dce->mqtt_state = A7677S_MQTT_PUB_PAYLOAD;
    snprintf(s_mqtt_dyn_cmd_buf, sizeof(s_mqtt_dyn_cmd_buf),
             "AT+CMQTTPAYLOAD=%u,%u\r\n",
             A7677S_MQTT_CLIENT_INDEX, (unsigned)dce->mqtt_pub_payload_len);
    send_mqtt_dynamic(dce, s_mqtt_dyn_cmd_buf, ">", "\r\nERROR\r\n",
                      cb_mqtt_pub_payload, A7677S_TIMEOUT_AT);
}

static void cb_mqtt_pub_payload(modem_t *modem, const char *response,
                                 modem_response_st_t res, void *arg)
{
    a7677s_t *dce = pDCE(arg);
    dce->mqtt_cmd_pending = 0;

    if (res != MODEM_RESPONSE_SUCCESS) {
        log_error(TAG, "AT+CMQTTPAYLOAD prompt failed (res=%d)", res);
        dce->mqtt_state = A7677S_MQTT_CONNECTED;
        mqtt_op_done(dce, MODEM_OPS_ERROR);
        return;
    }

    /* BUGFIX: same class of bug as cb_mqtt_pub_topic() above. Payload can be
     * up to A7677S_MQTT_PAYLOAD_MAX (10240 bytes), far beyond the 384-byte
     * s_mqtt_dyn_cmd_buf scratch buffer — the old strncpy() truncated any
     * payload longer than 383 bytes while AT+CMQTTPAYLOAD=...,<len> had
     * already announced the untruncated length to the modem. Pass the
     * pointer directly instead. */
    dce->mqtt_state = A7677S_MQTT_PUB_PAYLOAD_DATA;
    send_mqtt_dynamic(dce, dce->mqtt_pub_payload, "\r\nOK\r\n", "\r\nERROR\r\n",
                      cb_mqtt_pub_payload_data, A7677S_TIMEOUT_AT);
}

static void cb_mqtt_pub_payload_data(modem_t *modem, const char *response,
                                      modem_response_st_t res, void *arg)
{
    a7677s_t *dce = pDCE(arg);
    dce->mqtt_cmd_pending = 0;

    if (res != MODEM_RESPONSE_SUCCESS) {
        log_error(TAG, "MQTT payload data failed (res=%d)", res);
        dce->mqtt_state = A7677S_MQTT_CONNECTED;
        mqtt_op_done(dce, MODEM_OPS_ERROR);
        return;
    }

    /* AT+CMQTTPUB=<idx>,<qos>,<pub_timeout>[,<retained>[,<dup>]]
     * pub_timeout: 60-180 s (datasheet 18.2.12); we use A7677S_MQTT_PUB_TIMEOUT_S. */
    dce->mqtt_state = A7677S_MQTT_PUB_SEND;
    snprintf(s_mqtt_dyn_cmd_buf, sizeof(s_mqtt_dyn_cmd_buf),
             "AT+CMQTTPUB=%u,%u,%u,%u\r\n",
             A7677S_MQTT_CLIENT_INDEX, (unsigned)dce->mqtt_pub_qos,
             A7677S_MQTT_PUB_TIMEOUT_S, (unsigned)dce->mqtt_pub_retain);
    send_mqtt_dynamic(dce, s_mqtt_dyn_cmd_buf,
                      "\r\n+CMQTTPUB:0,0\r\n", "\r\nERROR\r\n",
                      cb_mqtt_pub_send, A7677S_TIMEOUT_MQTT_PUB);
}

static void cb_mqtt_pub_send(modem_t *modem, const char *response,
                              modem_response_st_t res, void *arg)
{
    a7677s_t *dce = pDCE(arg);
    dce->mqtt_cmd_pending = 0;

    if (res != MODEM_RESPONSE_SUCCESS) {
        if (response) {
            const char *p = strstr(response, "+CMQTTPUB:");
            if (p) {
                int ci, err;
                if (sscanf(p, "+CMQTTPUB:%d,%d", &ci, &err) == 2)
                    log_error(TAG, "AT+CMQTTPUB errcode=%d", err);
            }
        }
        dce->mqtt_state = A7677S_MQTT_CONNECTED;
        mqtt_op_done(dce, (res == MODEM_RESPONSE_TIMEOUT) ? MODEM_OPS_TIMEOUT : MODEM_OPS_ERROR);
        return;
    }

    log_info(TAG, "MQTT publish OK");
    dce->mqtt_state = A7677S_MQTT_CONNECTED;
    mqtt_op_done(dce, MODEM_OPS_OK);
}

/* ------------------------------------------------------------------ */
/*  MQTT — subscribe sequence                                           */
/* ------------------------------------------------------------------ */
/* SUB_TOPIC (wait ">") -> SUB_TOPIC_DATA (wait OK) ->
 * SUB_SEND (wait +CMQTTSUB:0,0) -> CONNECTED */

static void cb_mqtt_sub_topic(modem_t *modem, const char *response,
                               modem_response_st_t res, void *arg)
{
    a7677s_t *dce = pDCE(arg);
    dce->mqtt_cmd_pending = 0;

    if (res != MODEM_RESPONSE_SUCCESS) {
        log_error(TAG, "AT+CMQTTSUBTOPIC prompt failed (res=%d)", res);
        dce->mqtt_state = A7677S_MQTT_CONNECTED;
        mqtt_op_done(dce, MODEM_OPS_ERROR);
        return;
    }

    /* BUGFIX: same class of bug as cb_mqtt_pub_topic() above. Pass
     * dce->mqtt_sub_topic directly instead of copying through
     * s_mqtt_dyn_cmd_buf (384 bytes). send_mqtt_dynamic()/command[].cmd only
     * stores the pointer (no copy), and sx_uart_write() has no 384-byte
     * limit of its own. Topic max is A7677S_MQTT_TOPIC_MAX (1025 bytes),
     * which already exceeds the old scratch buffer, so the old strncpy()
     * silently truncated any subscribe topic longer than 383 bytes while
     * the AT+CMQTTSUBTOPIC=...,<len> command line had already told the
     * modem the untruncated length — causing the modem to wait for bytes
     * that never arrived (timeout) or to misread subsequent AT traffic as
     * topic data. */
    dce->mqtt_state = A7677S_MQTT_SUB_TOPIC_DATA;
    send_mqtt_dynamic(dce, dce->mqtt_sub_topic, "\r\nOK\r\n", "\r\nERROR\r\n",
                      cb_mqtt_sub_topic_data, A7677S_TIMEOUT_AT);
}

static void cb_mqtt_sub_topic_data(modem_t *modem, const char *response,
                                    modem_response_st_t res, void *arg)
{
    a7677s_t *dce = pDCE(arg);
    dce->mqtt_cmd_pending = 0;

    if (res != MODEM_RESPONSE_SUCCESS) {
        log_error(TAG, "MQTT sub topic data failed (res=%d)", res);
        dce->mqtt_state = A7677S_MQTT_CONNECTED;
        mqtt_op_done(dce, MODEM_OPS_ERROR);
        return;
    }

    /* AT+CMQTTSUB=<idx> — subscribe all topics accumulated via CMQTTSUBTOPIC.
     * Datasheet 18.2.14, write form 1: AT+CMQTTSUB=<client_index>[,<dup>]
     * Response: OK then URC +CMQTTSUB:<idx>,0 on success. */
    dce->mqtt_state = A7677S_MQTT_SUB_SEND;
    snprintf(s_mqtt_dyn_cmd_buf, sizeof(s_mqtt_dyn_cmd_buf),
             "AT+CMQTTSUB=%u\r\n", A7677S_MQTT_CLIENT_INDEX);
    send_mqtt_dynamic(dce, s_mqtt_dyn_cmd_buf,
                      "\r\n+CMQTTSUB:0,0\r\n", "\r\nERROR\r\n",
                      cb_mqtt_sub_send, A7677S_TIMEOUT_MQTT_CONNECT);
}

static void cb_mqtt_sub_send(modem_t *modem, const char *response,
                              modem_response_st_t res, void *arg)
{
    a7677s_t *dce = pDCE(arg);
    dce->mqtt_cmd_pending = 0;

    if (res != MODEM_RESPONSE_SUCCESS) {
        if (response) {
            const char *p = strstr(response, "+CMQTTSUB:");
            if (p) {
                int ci, err;
                if (sscanf(p, "+CMQTTSUB:%d,%d", &ci, &err) == 2)
                    log_error(TAG, "AT+CMQTTSUB errcode=%d", err);
            }
        }
        dce->mqtt_state = A7677S_MQTT_CONNECTED;
        mqtt_op_done(dce, (res == MODEM_RESPONSE_TIMEOUT) ? MODEM_OPS_TIMEOUT : MODEM_OPS_ERROR);
        return;
    }

    log_info(TAG, "MQTT subscribe OK (topic=%s)", dce->mqtt_sub_topic);
    dce->mqtt_state = A7677S_MQTT_CONNECTED;
    mqtt_op_done(dce, MODEM_OPS_OK);
}

/* ------------------------------------------------------------------ */
/*  MQTT — vtable functions                                             */
/* ------------------------------------------------------------------ */

static int a7677s_mqtt_connect(void *ctx, const char *client_id,
                                const char *broker, uint16_t port,
                                uint8_t use_ssl,
                                const char *ca_cert,
                                const char *client_cert,
                                const char *client_key,
                                uint16_t keepalive,
                                uint8_t clean_session, const char *username,
                                const char *password, mqtt_cb_t cb)
{
    a7677s_t *dce = (a7677s_t *)ctx;

    if (!a7677s_is_ready(ctx)) {
        log_warn(TAG, "mqtt_connect: modem not ready");
        return -1;
    }
    if (dce->mqtt_state != A7677S_MQTT_IDLE) {
        log_warn(TAG, "mqtt_connect: already connected or sequence in progress (state=%d)", dce->mqtt_state);
        return -1;
    }
    if (modem_is_busy(pModem(dce)) || dce->mqtt_cmd_pending) {
        log_warn(TAG, "mqtt_connect: modem busy");
        return -1;
    }
    if (!client_id || client_id[0] == '\0') {
        log_error(TAG, "mqtt_connect: client_id must not be NULL or empty");
        return -1;
    }

    strncpy(dce->mqtt_client_id, client_id, sizeof(dce->mqtt_client_id) - 1);
    dce->mqtt_client_id[sizeof(dce->mqtt_client_id) - 1] = '\0';

    /* Build URI — AT+CMQTTCONNECT requires "tcp://host:port" or "ssl://host:port". */
    snprintf(dce->mqtt_broker, sizeof(dce->mqtt_broker), "%s://%s:%u",
             use_ssl ? "ssl" : "tcp", broker, (unsigned)port);

    dce->mqtt_use_ssl       = use_ssl;
    dce->mqtt_keepalive     = keepalive ? keepalive : 60;
    dce->mqtt_clean_session = clean_session;

    strncpy(dce->mqtt_username, username ? username : "", sizeof(dce->mqtt_username) - 1);
    dce->mqtt_username[sizeof(dce->mqtt_username) - 1] = '\0';
    strncpy(dce->mqtt_password, password ? password : "", sizeof(dce->mqtt_password) - 1);
    dce->mqtt_password[sizeof(dce->mqtt_password) - 1] = '\0';

    /* Certs are only consulted when use_ssl=1 (per modem_ops.h doc-comment on
     * mqtt_connect). When use_ssl=0 we deliberately clear the has_* flags so
     * a stale cert from a previous TLS connect() call cannot leak into a
     * later plain-TCP session (the buffers themselves are left alone since
     * clearing 3x10KB here would be wasted work — has_ca/has_client/has_key
     * gate every use of them). */
    if (use_ssl) {
        dce->mqtt_cert_has_ca     = (ca_cert     && ca_cert[0]);
        dce->mqtt_cert_has_client = (client_cert && client_cert[0]);
        dce->mqtt_cert_has_key    = (client_key  && client_key[0]);

        if (dce->mqtt_cert_has_ca) {
            size_t len = strlen(ca_cert);
            if (len >= A7677S_CERT_PEM_MAX) {
                log_error(TAG, "mqtt_connect: ca_cert too large (%u bytes, max %u)",
                          (unsigned)len, (unsigned)(A7677S_CERT_PEM_MAX - 1));
                return -1;
            }
            memcpy(dce->mqtt_cert_ca, ca_cert, len + 1);
            dce->mqtt_cert_ca_len = (uint16_t)len;
        }
        if (dce->mqtt_cert_has_client) {
            size_t len = strlen(client_cert);
            if (len >= A7677S_CERT_PEM_MAX) {
                log_error(TAG, "mqtt_connect: client_cert too large (%u bytes, max %u)",
                          (unsigned)len, (unsigned)(A7677S_CERT_PEM_MAX - 1));
                return -1;
            }
            memcpy(dce->mqtt_cert_client, client_cert, len + 1);
            dce->mqtt_cert_client_len = (uint16_t)len;
        }
        if (dce->mqtt_cert_has_key) {
            size_t len = strlen(client_key);
            if (len >= A7677S_CERT_PEM_MAX) {
                log_error(TAG, "mqtt_connect: client_key too large (%u bytes, max %u)",
                          (unsigned)len, (unsigned)(A7677S_CERT_PEM_MAX - 1));
                return -1;
            }
            memcpy(dce->mqtt_cert_key, client_key, len + 1);
            dce->mqtt_cert_key_len = (uint16_t)len;
        }

        /* authmode per a76xx_at_cmd.md 19.2.1 / a7677s.h enum comment:
         *   0 = no client-side trust material at all
         *   1 = server-authentication only (ca_cert present, no client cert/key)
         *   2 = mutual TLS (client_cert AND client_key both present)
         * Note client_cert/client_key must both be set for mode 2; if only
         * one of the two was supplied we fall back to mode 1 rather than
         * silently attempting an incomplete mutual-auth handshake. */
        if (dce->mqtt_cert_has_client && dce->mqtt_cert_has_key) {
            dce->mqtt_ssl_authmode = 2;
        } else if (dce->mqtt_cert_has_ca) {
            dce->mqtt_ssl_authmode = 1;
        } else {
            dce->mqtt_ssl_authmode = 0;
        }
    } else {
        dce->mqtt_cert_has_ca     = 0;
        dce->mqtt_cert_has_client = 0;
        dce->mqtt_cert_has_key    = 0;
        dce->mqtt_ssl_authmode    = 0;
    }

    dce->mqtt_op_cb = cb;
    dce->mqtt_state = A7677S_MQTT_START;

    send_mqtt_dynamic(dce, "AT+CMQTTSTART\r\n",
                      "\r\n+CMQTTSTART:0\r\n", "\r\nERROR\r\n",
                      cb_mqtt_start, A7677S_TIMEOUT_MQTT_START);
    return 0;
}

static int a7677s_mqtt_disconnect(void *ctx, mqtt_cb_t cb)
{
    a7677s_t *dce = (a7677s_t *)ctx;

    if (dce->mqtt_state != A7677S_MQTT_CONNECTED) {
        log_warn(TAG, "mqtt_disconnect: not connected (state=%d)", dce->mqtt_state);
        return -1;
    }
    if (modem_is_busy(pModem(dce)) || dce->mqtt_cmd_pending) {
        log_warn(TAG, "mqtt_disconnect: modem busy");
        return -1;
    }

    dce->mqtt_op_cb = cb;
    dce->mqtt_state = A7677S_MQTT_DISC;

    /* timeout param 0 or 60-180 s; we use A7677S_MQTT_DISC_TIMEOUT_S. */
    snprintf(s_mqtt_dyn_cmd_buf, sizeof(s_mqtt_dyn_cmd_buf),
             "AT+CMQTTDISC=%u,%u\r\n",
             A7677S_MQTT_CLIENT_INDEX, A7677S_MQTT_DISC_TIMEOUT_S);
    send_mqtt_dynamic(dce, s_mqtt_dyn_cmd_buf,
                      "\r\n+CMQTTDISC:0,0\r\n", "\r\nERROR\r\n",
                      cb_mqtt_disc, A7677S_TIMEOUT_MQTT_DISC);
    return 0;
}

static int a7677s_mqtt_publish(void *ctx, const char *topic, const char *msg,
                                uint8_t qos, uint8_t retain, mqtt_cb_t cb)
{
    a7677s_t *dce = (a7677s_t *)ctx;

    if (dce->mqtt_state != A7677S_MQTT_CONNECTED) {
        log_warn(TAG, "mqtt_publish: not connected");
        return -1;
    }
    if (modem_is_busy(pModem(dce)) || dce->mqtt_cmd_pending) {
        log_warn(TAG, "mqtt_publish: modem busy");
        return -1;
    }
    if (!topic || !msg) return -1;

    uint16_t topic_len   = (uint16_t)strlen(topic);
    uint16_t payload_len = (uint16_t)strlen(msg);

    if (topic_len == 0 || topic_len > 1024 || payload_len == 0 || payload_len > 10240) {
        log_error(TAG, "mqtt_publish: topic len %u or payload len %u out of range", topic_len, payload_len);
        return -1;
    }

    strncpy(dce->mqtt_pub_topic, topic, sizeof(dce->mqtt_pub_topic) - 1);
    dce->mqtt_pub_topic[sizeof(dce->mqtt_pub_topic) - 1] = '\0';
    dce->mqtt_pub_topic_len = topic_len;

    strncpy(dce->mqtt_pub_payload, msg, sizeof(dce->mqtt_pub_payload) - 1);
    dce->mqtt_pub_payload[sizeof(dce->mqtt_pub_payload) - 1] = '\0';
    dce->mqtt_pub_payload_len = payload_len;

    dce->mqtt_pub_qos    = qos;
    dce->mqtt_pub_retain = retain;
    dce->mqtt_op_cb      = cb;
    dce->mqtt_state      = A7677S_MQTT_PUB_TOPIC;

    snprintf(s_mqtt_dyn_cmd_buf, sizeof(s_mqtt_dyn_cmd_buf),
             "AT+CMQTTTOPIC=%u,%u\r\n", A7677S_MQTT_CLIENT_INDEX, (unsigned)topic_len);
    send_mqtt_dynamic(dce, s_mqtt_dyn_cmd_buf, ">", "\r\nERROR\r\n",
                      cb_mqtt_pub_topic, A7677S_TIMEOUT_AT);
    return 0;
}

static int a7677s_mqtt_subscribe(void *ctx, const char *topic, uint8_t qos, mqtt_cb_t cb)
{
    a7677s_t *dce = (a7677s_t *)ctx;

    if (dce->mqtt_state != A7677S_MQTT_CONNECTED) {
        log_warn(TAG, "mqtt_subscribe: not connected");
        return -1;
    }
    if (modem_is_busy(pModem(dce)) || dce->mqtt_cmd_pending) {
        log_warn(TAG, "mqtt_subscribe: modem busy");
        return -1;
    }
    if (!topic) return -1;

    uint16_t topic_len = (uint16_t)strlen(topic);
    if (topic_len == 0 || topic_len > 1024) {
        log_error(TAG, "mqtt_subscribe: topic len %u out of range", topic_len);
        return -1;
    }

    strncpy(dce->mqtt_sub_topic, topic, sizeof(dce->mqtt_sub_topic) - 1);
    dce->mqtt_sub_topic[sizeof(dce->mqtt_sub_topic) - 1] = '\0';
    dce->mqtt_sub_topic_len = topic_len;
    dce->mqtt_sub_qos       = qos;
    dce->mqtt_op_cb         = cb;
    dce->mqtt_state         = A7677S_MQTT_SUB_TOPIC;

    snprintf(s_mqtt_dyn_cmd_buf, sizeof(s_mqtt_dyn_cmd_buf),
             "AT+CMQTTSUBTOPIC=%u,%u,%u\r\n",
             A7677S_MQTT_CLIENT_INDEX, (unsigned)topic_len, (unsigned)qos);
    send_mqtt_dynamic(dce, s_mqtt_dyn_cmd_buf, ">", "\r\nERROR\r\n",
                      cb_mqtt_sub_topic, A7677S_TIMEOUT_AT);
    return 0;
}

const modem_ops_t a7677s_ops = {
    .power_on_start   = a7677s_power_on_start,
    .power_off_start  = a7677s_power_off_start,
    .power_is_busy    = a7677s_power_is_busy,

    .start            = a7677s_start,
    .is_ready         = a7677s_is_ready,
    .set_on_ready     = a7677s_set_on_ready,
    .set_on_error     = a7677s_set_on_error,

    .enter_low_power  = a7677s_enter_low_power,
    .exit_low_power   = a7677s_exit_low_power,

    .mqtt_connect      = a7677s_mqtt_connect,
    .mqtt_disconnect   = a7677s_mqtt_disconnect,
    .mqtt_publish      = a7677s_mqtt_publish,
    .mqtt_subscribe    = a7677s_mqtt_subscribe,
    .mqtt_set_callbacks = a7677s_mqtt_set_callbacks,

    .get_imei         = a7677s_get_imei,
    .get_rssi         = a7677s_get_rssi,
    .get_ip           = a7677s_get_ip,

    .poll             = a7677s_poll,
};