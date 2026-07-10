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

    /* --- TODO (next piece): network attach state (COPS/CGATT/CGDCONT),
     * MQTT client state, low power (CFUN) state. is_ready() currently just
     * reflects power_state == A7677S_PWR_READY (AT responsive), NOT full
     * network attach — this will change once the attach sequence is added. */
};

void a7677s_init(a7677s_t *dce);

/* modem_ops_t implementation (see a7677s.c). The service layer should only
 * ever touch this driver through a modem_handle_t { .ops = &a7677s_ops,
 * .ctx = &board.a7677s }, never call a7677s_* functions directly. */
extern const modem_ops_t a7677s_ops;

#ifdef __cplusplus
}
#endif
#endif // A7677S_H