#ifndef TEST_SHELL_H
#define TEST_SHELL_H

#ifdef __cplusplus
extern "C" {
#endif

/* Standalone bring-up test for the CLI shell over UART6 console.
 *
 * IMPORTANT — this is NOT the same shell path as shell_app.c
 * (app/user/shell_app/). That module binds cli_shell.c's transport-
 * agnostic Cli_ShellImpl to USB CDC (sx_user_cdc_t) — this test binds
 * the SAME cli_shell.c core to board.log_uart (UART6) instead, because
 * this board revision has no USB connector wired/accessible. Both
 * reuse the exact same command table (g_shell_commands, shell_commands.c
 * — help/restart/ota/rollback-prev/rollback-factory/flash-factory/
 * settings), only the byte-transport differs.
 *
 * board.log_uart is already sx_uart_init()'d inside sx_board_init()
 * (sx_board.c) with RX interrupt already armed (HAL_UART_Receive_IT,
 * see sx_board.c's HAL_UART_RxCpltCallback branch for UART_LOG) — this
 * test does not re-init the UART, it only starts reading from it.
 *
 * CAVEAT — UART6 is also this project's log output (log_print() in
 * sx_board.c writes every log_info/log_warn/log_error/log_debug call
 * to this same UART). Running this shell test means log lines and
 * your typed shell input/output share one physical wire with no
 * separation — expect log lines to interleave with what you're typing.
 * This is a deliberate trade-off for a board with no USB connector, not
 * a bug in this test. If it's too noisy in practice, consider raising
 * the logger level (logger_init()'s level param, sx_board.c) to reduce
 * volume while testing shell commands.
 *
 * Call test_shell_init() once after sx_board_init() (Core/Src/main.c),
 * then test_shell_poll(delta_ms) every tick in the main while(1) loop
 * — same calling convention as the other test_*_poll() functions
 * (delta_ms is currently unused, kept for calling-convention
 * consistency, see test_shell.c). */

#include "stdint.h"

void test_shell_init(void);
void test_shell_poll(uint32_t delta_ms);

#ifdef __cplusplus
}
#endif

#endif