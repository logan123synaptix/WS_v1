#include "shell_app.h"
#include <stddef.h>
#include <stdbool.h>
#include <string.h>

static sx_uart_t *s_uart = NULL;
static ShellContext_t s_shell;
static bool s_initialized = false;

/* --- cli_shell.h's Cli_ShellImpl adapters: sx_uart_t works in
 * buffer+len, cli_shell.c wants one character (send_char) or a whole
 * null-terminated string (send_str) at a time — see cli_shell.c's
 * prv_echo_str(), which calls impl.send_str directly rather than looping
 * send_char per character. Both are required (not optional) per that
 * file's actual usage, despite cli_shell.h's header comment marking
 * receive_char (not these) as the only optional field. */
static int shell_send_char(void *arg, char c)
{
    sx_uart_t *uart = (sx_uart_t *)arg;
    uint8_t byte = (uint8_t)c;
    sx_uart_write(uart, &byte, 1);
    return 0;
}

static int shell_send_str(void *arg, char *str)
{
    sx_uart_t *uart = (sx_uart_t *)arg;
    sx_uart_write(uart, (const uint8_t *)str, (int)strlen(str));
    return 0;
}

void shell_app_init(sx_uart_t *uart)
{
    /* uart is expected to already be sx_uart_init()'d by the caller
     * (this project passes &board.log_uart, set up in sx_board_init()
     * with RX interrupt already armed) — this module does not init the
     * UART itself, only wires the shell's byte transport onto it. */
    s_uart = uart;

    s_shell.impl.arg          = s_uart;
    s_shell.impl.send_char    = shell_send_char;
    s_shell.impl.send_str     = shell_send_str;
    s_shell.impl.receive_char = NULL; /* unused by cli_shell.c today, see shell_app_poll() */

    cli_shell_boot(&s_shell);
    s_initialized = true;
}

void shell_app_poll(void)
{
    if (!s_initialized) {
        return;
    }

    /* Drain whatever bytes have arrived since the last poll, one at a
     * time into cli_shell_receive_char() — timeout_ms=0 so this never
     * blocks the main loop waiting on more UART data than what's
     * already buffered in s_uart's RX ring (fed by the UART's RX
     * interrupt, see sx_board.c's HAL_UART_RxCpltCallback). Unlike the
     * old USB CDC path, there is no separate "process()" step needed
     * here — sx_uart_t's RX path is already interrupt-driven end to
     * end, nothing to pump each tick beyond draining the queue. */
    uint8_t byte;
    while (sx_uart_available(s_uart) > 0) {
        if (sx_uart_read(s_uart, &byte, 1, 0) != 1) {
            break;
        }
        cli_shell_receive_char(&s_shell, (char)byte);
    }
}

ShellContext_t *shell_app_get_context(void)
{
    return s_initialized ? &s_shell : NULL;
}