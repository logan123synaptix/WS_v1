#include "shell_app.h"
#include <stddef.h>
#include <stdbool.h>
#include <string.h>

static sx_user_cdc_t s_cdc;
static ShellContext_t s_shell;
static bool s_initialized = false;

/* --- cli_shell.h's Cli_ShellImpl adapters: sx_user_cdc_t works in
 * buffer+len, cli_shell.c wants one character (send_char) or a whole
 * null-terminated string (send_str) at a time — see cli_shell.c's
 * prv_echo_str(), which calls impl.send_str directly rather than looping
 * send_char per character. Both are required (not optional) per that
 * file's actual usage, despite cli_shell.h's header comment marking
 * receive_char (not these) as the only optional field. */
static int shell_send_char(void *arg, char c)
{
    sx_user_cdc_t *cdc = (sx_user_cdc_t *)arg;
    uint8_t byte = (uint8_t)c;
    sx_user_cdc_write(cdc, &byte, 1);
    return 0;
}

static int shell_send_str(void *arg, char *str)
{
    sx_user_cdc_t *cdc = (sx_user_cdc_t *)arg;
    sx_user_cdc_write(cdc, (uint8_t *)str, strlen(str));
    return 0;
}

void shell_app_init(sx_usb_tiny_t *usb)
{
    sx_user_cdc_init(&s_cdc, usb);

    s_shell.impl.arg          = &s_cdc;
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

    /* Drives the underlying tinyusb CDC endpoint (must run every tick
     * regardless of whether a host is connected/typing, same reasoning
     * as every other *_process()/*_poll() call in app_process()). */
    sx_user_cdc_process(&s_cdc);

    /* Drain whatever bytes have arrived since the last poll, one at a
     * time into cli_shell_receive_char() — timeout_ms=0 so this never
     * blocks the main loop waiting on more USB data than what's already
     * buffered. */
    uint8_t byte;
    while (sx_user_cdc_read(&s_cdc, &byte, 1, 0) == 1) {
        cli_shell_receive_char(&s_shell, (char)byte);
    }
}

ShellContext_t *shell_app_get_context(void)
{
    return s_initialized ? &s_shell : NULL;
}