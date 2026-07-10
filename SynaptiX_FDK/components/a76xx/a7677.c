#include <stdint.h>
#include "a7677s.h"
#include "sx_gpio.h"
#include "logger.h"
#include "string.h"

#define pModem(dce) ((modem_t *)&dce->base)
#define pDCE(modem) ((a7677s_t *)modem)

static const char *TAG = "A7677S";

/* --- AT command table (mirrors the pattern already used in sim76xx.c) --- */
#define CMD_AT   0
#define CMD_CPOF 1

static modem_command_t command[] = {
    [CMD_AT]   = {.cmd = "AT\r\n",       .res_success = "\r\nOK\r\n", .res_fail = "\r\nERROR\r\n"},
    [CMD_CPOF] = {.cmd = "AT+CPOF\r\n",  .res_success = "\r\nOK\r\n", .res_fail = "\r\nERROR\r\n"},
};

static void cb_at_probe(modem_t *modem, const char *response, modem_response_st_t res, void *arg);
static void cb_cpof(modem_t *modem, const char *response, modem_response_st_t res, void *arg);

/* ------------------------------------------------------------------ */
/*  Init                                                                */
/* ------------------------------------------------------------------ */

void a7677s_init(a7677s_t *dce)
{
    modem_init(pModem(dce));
    dce->power_state    = A7677S_PWR_IDLE;
    dce->power_elapsed  = 0;
    dce->at_probe_pending = 0;
    log_debug(TAG, "Initializing");
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
}

/* ------------------------------------------------------------------ */
/*  modem_ops_t vtable                                                 */
/* ------------------------------------------------------------------ */
/* TODO (next piece, not yet implemented):
 *   - start()/is_ready(): currently is_ready() only reflects "AT responsive"
 *     (power_state == READY), NOT full network attach. A proper AT init
 *     sequence (COPS/CGATT/CGDCONT, mirroring the pattern already used in
 *     sim76xx.c's state machine) still needs to be added.
 *   - enter_low_power()/exit_low_power(): AT+CFUN=0/1, not yet implemented.
 *   - mqtt_connect/disconnect/publish/subscribe: AT+CMQTT* sequence, not yet
 *     implemented (see confirmed AT command set in Documents/a76xx_at_cmd.md
 *     section 18.2, and the errcode-parsing bug fix noted in the handoff).
 * These functions are stubbed to fail safely (return -1 / false) so the
 * vtable compiles and is safe to wire up incrementally.
 */
static int a7677s_start_stub(void *ctx) { (void)ctx; return -1; }
static bool a7677s_is_ready(void *ctx)
{
    a7677s_t *dce = (a7677s_t *)ctx;
    return dce->power_state == A7677S_PWR_READY;
}
static int a7677s_enter_low_power_stub(void *ctx, power_mode_cb_t cb) { (void)ctx; (void)cb; return -1; }
static int a7677s_exit_low_power_stub(void *ctx, power_mode_cb_t cb)  { (void)ctx; (void)cb; return -1; }
static int a7677s_mqtt_connect_stub(void *ctx, const char *broker, uint16_t port,
                                     uint8_t use_ssl, uint16_t keepalive,
                                     uint8_t clean_session, const char *username,
                                     const char *password, mqtt_cb_t cb)
{
    (void)ctx; (void)broker; (void)port; (void)use_ssl; (void)keepalive;
    (void)clean_session; (void)username; (void)password; (void)cb;
    return -1;
}
static int a7677s_mqtt_disconnect_stub(void *ctx, mqtt_cb_t cb) { (void)ctx; (void)cb; return -1; }
static int a7677s_mqtt_publish_stub(void *ctx, const char *topic, const char *msg,
                                     uint8_t qos, uint8_t retain, mqtt_cb_t cb)
{
    (void)ctx; (void)topic; (void)msg; (void)qos; (void)retain; (void)cb;
    return -1;
}
static int a7677s_mqtt_subscribe_stub(void *ctx, const char *topic, uint8_t qos, mqtt_cb_t cb)
{
    (void)ctx; (void)topic; (void)qos; (void)cb;
    return -1;
}

const modem_ops_t a7677s_ops = {
    .power_on_start   = a7677s_power_on_start,
    .power_off_start  = a7677s_power_off_start,
    .power_is_busy    = a7677s_power_is_busy,

    .start            = a7677s_start_stub,          /* TODO next piece */
    .is_ready         = a7677s_is_ready,

    .enter_low_power  = a7677s_enter_low_power_stub, /* TODO next piece */
    .exit_low_power   = a7677s_exit_low_power_stub,  /* TODO next piece */

    .mqtt_connect     = a7677s_mqtt_connect_stub,    /* TODO next piece */
    .mqtt_disconnect  = a7677s_mqtt_disconnect_stub, /* TODO next piece */
    .mqtt_publish     = a7677s_mqtt_publish_stub,    /* TODO next piece */
    .mqtt_subscribe   = a7677s_mqtt_subscribe_stub,  /* TODO next piece */

    .poll             = a7677s_poll,
};