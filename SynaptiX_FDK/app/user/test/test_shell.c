#include "test_shell.h"
#include "sx_board.h"
#include "cli_shell.h"
#include "logger.h"
#include <stdint.h>
#include <string.h>

static const char *TAG = "TEST_SHELL";

static ShellContext_t s_shell;
static bool s_initialized = false;

/* --- Cli_ShellImpl adapters over board.log_uart (UART6), same role as
 * shell_app.c's shell_send_char/shell_send_str but for sx_uart_t
 * instead of sx_user_cdc_t. Both send_char and send_str are required
 * (not optional) by cli_shell.c's actual usage — see shell_app.c's own
 * comment on this, still true here since this test drives the same
 * cli_shell.c core. */
static int uart_shell_send_char(void *arg, char c)
{
    sx_uart_t *uart = (sx_uart_t *)arg;
    uint8_t byte = (uint8_t)c;
    sx_uart_write(uart, &byte, 1);
    return 0;
}

/* PATCH (2026-07-29) — root cause: sx_uart_write() -> HAL_UART_Transmit()
 * is blocking/polling and briefly locks the UART peripheral (STM32 HAL's
 * __HAL_LOCK on huart->State) for the whole call. If a byte arrives on
 * UART6 RX while that lock is held, HAL_UART_RxCpltCallback()'s own
 * re-arm call (HAL_UART_Receive_IT(), sx_board.c) silently fails with
 * HAL_BUSY and is NOT retried anywhere — UART6 RX interrupts stop firing
 * permanently after that single collision, which is exactly the observed
 * symptom (shell echoes a few characters then goes completely silent,
 * no more RX at all). The original one-shot send_str(whole string) held
 * the lock for the string's entire TX duration (up to ~1000ms per
 * sx_uart_write()'s HAL_UART_Transmit(...,1000) timeout for a long
 * help-menu line), making a collision far more likely than
 * send_char()'s single-byte call above.
 *
 * This is a mitigation, NOT a full fix — splitting into one HAL call per
 * byte shrinks the lock window down close to send_char()'s (which was
 * observed to survive in the known-good standalone main.c test), but a
 * byte could still in principle arrive in the handful of microseconds a
 * single HAL_UART_Transmit(...,1,...) call holds the lock for one byte
 * at 115200 baud (~87us/byte). The real fix — making HAL_UART_Receive_IT()
 * retry from the main loop instead of failing silently once inside the
 * ISR, or moving sx_uart_write() to HAL_UART_Transmit_IT() so it never
 * locks against RX at all — is tracked separately, not done here. */
static int uart_shell_send_str(void *arg, char *str)
{
    sx_uart_t *uart = (sx_uart_t *)arg;
    for (const char *p = str; *p != '\0'; ++p) {
        uint8_t byte = (uint8_t)*p;
        sx_uart_write(uart, &byte, 1);
    }
    return 0;
}

void test_shell_init(void)
{
    log_info(TAG, "=== TEST SHELL over UART6 console (no USB on this board revision) ===");
    log_info(TAG, "Type 'help' on the UART6 console to list commands. "
                   "Log lines will interleave with shell I/O on the same wire — see test_shell.h.");

    /* board.log_uart was already sx_uart_init()'d inside sx_board_init()
     * (sx_board.c), RX interrupt already armed — do NOT re-init it here,
     * that would disturb the logger's own TX path on the same instance. */
    s_shell.impl.arg          = &board.log_uart;
    s_shell.impl.send_char    = uart_shell_send_char;
    s_shell.impl.send_str     = uart_shell_send_str;
    s_shell.impl.receive_char = NULL; /* unused by cli_shell.c today, same as shell_app.c */

    cli_shell_boot(&s_shell);
    s_initialized = true;
}

void test_shell_poll(uint32_t delta_ms)
{
    (void)delta_ms; /* no timing dependency — this just drains whatever bytes have arrived */

    if (!s_initialized) {
        return;
    }

    /* Drain whatever bytes have arrived since the last poll, one at a
     * time into cli_shell_receive_char() — timeout_ms=0 so this never
     * blocks the main loop waiting on more UART data than what's
     * already buffered in board.log_uart's RX ring (armed via
     * HAL_UART_Receive_IT in sx_board.c). Same drain pattern as
     * shell_app_poll()'s sx_user_cdc_read() loop. */
    uint8_t byte;
    while (sx_uart_available(&board.log_uart) > 0) {
        if (sx_uart_read(&board.log_uart, &byte, 1, 0) != 1) {
            break;
        }
        cli_shell_receive_char(&s_shell, (char)byte);
    }
}