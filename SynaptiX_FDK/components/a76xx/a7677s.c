#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
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
#define CMD_DYNAMIC         9   /* CGDCONT/CGAUTH/CGACT - built at runtime, need apn/user/pass */

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

/* Helper: send a dynamic MQTT command (built into s_mqtt_dyn_cmd_buf). */
static void send_mqtt_dynamic(a7677s_t *dce, const char *cmd_str,
                               const char *res_success, const char *res_fail,
                               modem_command_response_callback_t cb,
                               uint32_t timeout_ms);

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
    dce->mqtt_incoming_cb  = NULL;
    dce->mqtt_connlost_cb  = NULL;
    dce->mqtt_user_ctx     = NULL;

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
     * attach sequence complete, since CGACT/CREG already succeeded; a
     * failed final read-back should not block is_ready(). */
    if (res == MODEM_RESPONSE_SUCCESS && response) {
        log_info(TAG, "CGDCONT: %s", response);
    } else {
        log_warn(TAG, "CGDCONT read-back failed (non-fatal)");
    }
    dce->init_retry_count = 0;
    dce->init_state       = A7677S_INIT_READY;
    log_info(TAG, "Network attach complete, ready for MQTT");
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

void a7677s_mqtt_register_callbacks(a7677s_t *dce,
                                     mqtt_incoming_cb_t incoming_cb,
                                     mqtt_connlost_cb_t connlost_cb,
                                     void *user_ctx)
{
    dce->mqtt_incoming_cb = incoming_cb;
    dce->mqtt_connlost_cb = connlost_cb;
    dce->mqtt_user_ctx    = user_ctx;
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

    dce->mqtt_state = A7677S_MQTT_ACCQ;
    snprintf(s_mqtt_dyn_cmd_buf, sizeof(s_mqtt_dyn_cmd_buf),
             "AT+CMQTTACCQ=%u,\"%s\",%u\r\n",
             A7677S_MQTT_CLIENT_INDEX, dce->mqtt_client_id, (unsigned)dce->mqtt_use_ssl);
    send_mqtt_dynamic(dce, s_mqtt_dyn_cmd_buf, "\r\nOK\r\n", "\r\nERROR\r\n",
                      cb_mqtt_accq, A7677S_TIMEOUT_AT);
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
     * reads exactly that many bytes without needing \r\n. */
    dce->mqtt_state = A7677S_MQTT_PUB_TOPIC_DATA;
    strncpy(s_mqtt_dyn_cmd_buf, dce->mqtt_pub_topic, sizeof(s_mqtt_dyn_cmd_buf) - 1);
    s_mqtt_dyn_cmd_buf[sizeof(s_mqtt_dyn_cmd_buf) - 1] = '\0';
    send_mqtt_dynamic(dce, s_mqtt_dyn_cmd_buf, "\r\nOK\r\n", "\r\nERROR\r\n",
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

    dce->mqtt_state = A7677S_MQTT_PUB_PAYLOAD_DATA;
    strncpy(s_mqtt_dyn_cmd_buf, dce->mqtt_pub_payload, sizeof(s_mqtt_dyn_cmd_buf) - 1);
    s_mqtt_dyn_cmd_buf[sizeof(s_mqtt_dyn_cmd_buf) - 1] = '\0';
    send_mqtt_dynamic(dce, s_mqtt_dyn_cmd_buf, "\r\nOK\r\n", "\r\nERROR\r\n",
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

    dce->mqtt_state = A7677S_MQTT_SUB_TOPIC_DATA;
    strncpy(s_mqtt_dyn_cmd_buf, dce->mqtt_sub_topic, sizeof(s_mqtt_dyn_cmd_buf) - 1);
    s_mqtt_dyn_cmd_buf[sizeof(s_mqtt_dyn_cmd_buf) - 1] = '\0';
    send_mqtt_dynamic(dce, s_mqtt_dyn_cmd_buf, "\r\nOK\r\n", "\r\nERROR\r\n",
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
                                uint8_t use_ssl, uint16_t keepalive,
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

    .enter_low_power  = a7677s_enter_low_power,
    .exit_low_power   = a7677s_exit_low_power,

    .mqtt_connect     = a7677s_mqtt_connect,
    .mqtt_disconnect  = a7677s_mqtt_disconnect,
    .mqtt_publish     = a7677s_mqtt_publish,
    .mqtt_subscribe   = a7677s_mqtt_subscribe,

    .poll             = a7677s_poll,
};