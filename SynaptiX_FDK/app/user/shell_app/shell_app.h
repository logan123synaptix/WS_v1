#ifndef SHELL_APP_H
#define SHELL_APP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "sx_uart.h"
#include "cli_shell.h"

/* Wires services/shell/cli_shell.c's generic parser (ShellContext_t) to
 * UART6 (this project's log/debug UART, see board.log_uart in
 * sx_board.h) as the transport. The actual command table
 * (help/restart/settings/ota/...) lives in shell_commands.c, per
 * cli_shell.h's g_shell_commands/g_num_shell_commands externs — this
 * module only does transport wiring + per-tick polling, it does not
 * know what any command does.
 *
 * CHANGED from USB CDC to UART6 because this board revision has no USB
 * connector wired/accessible at all (confirmed by the user) — there is
 * no board variant on this project that still needs the USB CDC path,
 * so it was removed here rather than kept behind a config option. If a
 * future board revision brings USB back, this module will need
 * transport selection added back in (e.g. taking a pre-configured
 * Cli_ShellImpl/ShellContext_t from the caller instead of owning the
 * transport instance itself) — see the discussion that led to this
 * change for context.
 *
 * CAVEAT (also true of the UART6 console in general): UART6 is also
 * this project's log output (log_print() in sx_board.c writes every
 * log_info/log_warn/log_error/log_debug call to this same UART). Shell
 * input/output and log lines share one physical wire with no
 * separation — log lines can interleave with shell prompt/output on
 * whatever terminal is attached. This is an accepted trade-off for a
 * board with no USB connector, not a bug in this module.
 *
 * uart must point at an already-initialized sx_uart_t — this project
 * calls it with &board.log_uart (initialized by sx_board_init() before
 * app_init() runs, RX interrupt already armed there). */
void shell_app_init(sx_uart_t *uart);

/* Call every tick from app_process(). Drains whatever bytes board.log_uart
 * has received since the last call and feeds them one at a time to
 * cli_shell_receive_char(), same pattern the original UART-console CLI
 * in WS_v0 used per received byte. No-op if nothing has been received or
 * shell_app_init() hasn't been called yet. */
void shell_app_poll(void);

/* Returns the shell's ShellContext_t, so shell_commands.c's handlers can
 * be written independent of this module's internal sx_uart_t state,
 * consistent with how every cli_shell.h handler takes a ShellContext_t*
 * rather than reaching for a global. Returns NULL if shell_app_init()
 * hasn't been called yet. */
ShellContext_t *shell_app_get_context(void);

#ifdef __cplusplus
}
#endif

#endif