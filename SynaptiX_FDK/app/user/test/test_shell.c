#include "test_shell.h"
#include "sx_board.h"
#include "shell_app.h"
#include "logger.h"

static const char *TAG = "TEST_SHELL";

/* Thin wrapper over shell_app.c — DRY per user request (2026-07-29):
 * shell_app.c already implements the exact same UART6 Cli_ShellImpl
 * adapter this test used to duplicate (shell_send_char/shell_send_str
 * over sx_uart_t), now that shell_app.h has been changed to take UART6
 * directly instead of USB CDC (see shell_app.h's own doc-comment on
 * that change). This test no longer owns a ShellContext_t or any
 * adapter functions itself — it only calls shell_app_init()/
 * shell_app_poll(), same as app_init()/app_process() do in the non-TEST
 * build path (Core/Src/main.c's #if TEST / #else split).
 *
 * Kept as a separate test_shell_* entry point (rather than having
 * main.c call shell_app_init()/shell_app_poll() directly under #if
 * TEST) purely so main.c's TEST-branch call list stays visually
 * consistent with the other test_*_init()/test_*_poll() lines — no
 * functional difference from calling shell_app_* directly. */
void test_shell_init(void)
{
    log_info(TAG, "=== TEST SHELL over UART6 console (no USB on this board revision) ===");
    log_info(TAG, "Type 'help' on the UART6 console to list commands. "
                   "Log lines will interleave with shell I/O on the same wire.");

    /* board.log_uart was already sx_uart_init()'d + RX-interrupt-armed
     * inside sx_board_init() (sx_board.c) — shell_app_init() only wires
     * the shell transport onto it, it does not re-init the UART. */
    shell_app_init(&board.log_uart);
}

void test_shell_poll(uint32_t delta_ms)
{
    (void)delta_ms; /* shell_app_poll() has no timing dependency either — see its own comment */
    shell_app_poll();
}