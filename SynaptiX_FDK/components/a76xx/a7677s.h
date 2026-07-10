#ifndef A7677S_H
#define A7677S_H
#ifdef __cplusplus
extern "C" {
#endif

#include "modem.h"
#include "modem_ops.h"

/* Timeouts (ms) for the power state machine and AT init sequence. */
#define A7677S_PULSE_HIGH_MS   50U
#define A7677S_PULSE_LOW_MS    500U
#define A7677S_BOOT_PROBE_MS   500U     /* interval between "AT" probes while waiting for boot */
#define A7677S_TIMEOUT_AT      2500U
#define A7677S_TIMEOUT_CPOF    5000U
#define A7677S_TIMEOUT_NETWORK 9000U    /* CGDCONT/CGAUTH/CGACT/COPS, per a76xx_at_cmd.md MaxResponseTime */
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

    /* --- TODO (next piece): MQTT client state, low power (CFUN) state. */
};

void a7677s_init(a7677s_t *dce);

/* Sets APN/username/password before calling a7677s_ops.start(ctx) (i.e.
 * before the AT init/network-attach sequence begins). username/password may
 * be NULL or "" if the APN does not require authentication (CGAUTH is then
 * skipped entirely). Mirrors sim76xx_set_full_apn(). */
void a7677s_set_full_apn(a7677s_t *dce, const char *apn, const char *username, const char *password);

/* modem_ops_t implementation (see a7677s.c). The service layer should only
 * ever touch this driver through a modem_handle_t { .ops = &a7677s_ops,
 * .ctx = &board.a7677s }, never call a7677s_* functions directly. */
extern const modem_ops_t a7677s_ops;

#ifdef __cplusplus
}
#endif
#endif // A7677S_H