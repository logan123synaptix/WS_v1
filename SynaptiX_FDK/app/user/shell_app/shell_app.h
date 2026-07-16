#ifndef SHELL_APP_H
#define SHELL_APP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "sx_user_cdc.h"
#include "cli_shell.h"

/* Wires services/shell/cli_shell.c's generic parser (ShellContext_t) to
 * USB CDC as the transport, and owns the sx_user_cdc_t instance for it.
 * The actual command table (help/restart/settings) lives in
 * shell_commands.c, per cli_shell.h's g_shell_commands/g_num_shell_commands
 * externs — this module only does transport wiring + per-tick polling,
 * it does not know what any command does.
 *
 * usb must point at an already-initialized sx_usb_tiny_t (e.g. &board.usb,
 * initialized by sx_board_init() before app_init() runs). */
void shell_app_init(sx_usb_tiny_t *usb);

/* Call every tick from app_process(). Drains whatever bytes sx_user_cdc
 * has received since the last call and feeds them one at a time to
 * cli_shell_receive_char(), same pattern the original UART-console CLI
 * in WS_v0 used per received byte. No-op if nothing has been received or
 * shell_app_init() hasn't been called yet. */
void shell_app_poll(void);

/* Returns the shell's ShellContext_t, so shell_commands.c's handlers can
 * be written independent of this module's internal sx_user_cdc_t state,
 * consistent with how every cli_shell.h handler takes a ShellContext_t*
 * rather than reaching for a global. Returns NULL if shell_app_init()
 * hasn't been called yet. */
ShellContext_t *shell_app_get_context(void);

#ifdef __cplusplus
}
#endif

#endif