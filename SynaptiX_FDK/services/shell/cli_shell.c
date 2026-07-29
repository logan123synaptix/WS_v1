#include "cli_shell.h"


#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>



#define SHELL_FOR_EACH_COMMAND(command) \
  for (const Cli_Shell_Cmd *command = g_shell_commands; \
    command < &g_shell_commands[g_num_shell_commands]; \
    ++command)

static bool prv_booted(ShellContext_t *s_shell) {
  return s_shell->impl.send_char != NULL;
}

static void prv_send_char(ShellContext_t *s_shell,char c) {
  if (!prv_booted(s_shell)) {
    return;
  }
  s_shell->impl.send_char(s_shell->impl.arg,c);
}

static void prv_echo(ShellContext_t *s_shell,char c) {
  if ('\n' == c) {
    prv_send_char(s_shell,'\r');
    prv_send_char(s_shell,'\n');
  } else if ('\b' == c) {
    prv_send_char(s_shell,'\b');
    prv_send_char(s_shell,' ');
    prv_send_char(s_shell,'\b');
  } else {
    prv_send_char(s_shell,c);
  }
}

static char prv_last_char(ShellContext_t *s_shell) {
  return s_shell->rx_buffer[s_shell->rx_size - 1];
}

static bool prv_is_rx_buffer_full(ShellContext_t *s_shell) {
  return s_shell->rx_size >= SHELL_RX_BUFFER_SIZE;
}

static void prv_reset_rx_buffer(ShellContext_t *s_shell) {
  memset(s_shell->rx_buffer, 0, sizeof(s_shell->rx_buffer));
  s_shell->rx_size = 0;
}

static void prv_echo_str(ShellContext_t *s_shell,const char *str) {
  s_shell->impl.send_str(s_shell->impl.arg,str);
  // for (const char *c = str; *c != '\0'; ++c) {
  //   prv_echo(s_shell,*c);
  // }
}

static void prv_send_prompt(ShellContext_t *s_shell) {
  prv_echo_str(s_shell,SHELL_PROMPT);
}

static const Cli_Shell_Cmd *prv_find_command(const char *name) { // @suppress("Type cannot be resolved")
  SHELL_FOR_EACH_COMMAND(command) {
    if (strcmp(command->command, name) == 0) {
      return command;
    }
  }
  return NULL;
}

/* DEBUG (2026-07-29) — temporary diagnostic logging to find exactly
 * where prv_process() stalls on real hardware (shell echoes "help" but
 * never prints the command list or a new prompt afterwards). Remove
 * once root cause is confirmed. Uses log_info() directly (bypasses the
 * shell's own UART6 send_char/send_str path entirely) so this diagnostic
 * output is visible even if the shell's own TX path is the thing that's
 * stuck — if these log lines ALSO stop appearing, the problem is
 * upstream of prv_process() (i.e. in cli_shell_receive_char() or the
 * UART RX path itself, not in this function). */
#include "logger.h"
static const char *DBG_TAG = "CLI_DBG";

static void prv_process(ShellContext_t *s_shell) {
  log_info(DBG_TAG, "prv_process enter, rx_size=%u last_char=0x%02X",
            (unsigned)s_shell->rx_size,
            s_shell->rx_size > 0 ? (unsigned char)prv_last_char(s_shell) : 0);

  if (prv_last_char(s_shell) != '\n' && !prv_is_rx_buffer_full(s_shell)) {
    log_info(DBG_TAG, "prv_process: early return (not a complete line yet)");
    return;
  }

  log_info(DBG_TAG, "prv_process: tokenizing rx_buffer=\"%s\"", s_shell->rx_buffer);

  char *argv[SHELL_MAX_ARGS] = {0};
  int argc = 0;

  char *next_arg = NULL;
  for (size_t i = 0; i < s_shell->rx_size && argc < SHELL_MAX_ARGS; ++i) {
    char *const c = &s_shell->rx_buffer[i];
    if (*c == ' ' || *c == '\n' || i == s_shell->rx_size - 1) {
      *c = '\0';
      if (next_arg) {
        argv[argc++] = next_arg;
        next_arg = NULL;
      }
    } else if (!next_arg) {
      next_arg = c;
    }
  }

  log_info(DBG_TAG, "prv_process: argc=%d argv[0]=%s", argc, argc > 0 ? argv[0] : "(none)");

  if (s_shell->rx_size == SHELL_RX_BUFFER_SIZE) {
    prv_echo(s_shell,'\n');
  }

  if (argc >= 1) {
    const Cli_Shell_Cmd *command = prv_find_command(argv[0]);
    log_info(DBG_TAG, "prv_process: command lookup for \"%s\" -> %s",
              argv[0], command ? "FOUND" : "NOT FOUND");
    if (!command) {
      prv_echo_str(s_shell,"Unknown command: ");
      prv_echo_str(s_shell,argv[0]);
      prv_echo(s_shell,'\n');
      prv_echo_str(s_shell,"Type 'help' to list all commands\n");
    } else {
      log_info(DBG_TAG, "prv_process: calling handler...");
      command->handler(s_shell,argc, argv);
      log_info(DBG_TAG, "prv_process: handler returned");
    }
  }
  log_info(DBG_TAG, "prv_process: resetting buffer + sending prompt");
  prv_reset_rx_buffer(s_shell);
  prv_send_prompt(s_shell);
  log_info(DBG_TAG, "prv_process: done");
}

void cli_shell_boot(ShellContext_t *s_shell) {
  prv_reset_rx_buffer(s_shell);
  prv_echo_str(s_shell, "\n" SHELL_PROMPT);
}

void cli_shell_receive_char(ShellContext_t *s_shell,char c) {
  if (c == '\r' || prv_is_rx_buffer_full(s_shell) || !prv_booted(s_shell)) {
    return;
  }
  prv_echo(s_shell,c);

  if (c == '\b') {
    if (s_shell->rx_size > 0) {
      s_shell->rx_buffer[--s_shell->rx_size] = '\0';
    }
    return;
  }

  s_shell->rx_buffer[s_shell->rx_size++] = c;

  prv_process(s_shell);
}

void cli_shell_put_line(ShellContext_t *s_shell,const char *str) {
  prv_echo_str(s_shell,str);
  prv_echo(s_shell,'\n');
}

void cli_shell_printf(ShellContext_t *shell, const char *fmt, ...){
    char buffer[2048];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    if(len > 0){
        prv_echo_str(shell,buffer);
    }
}

int cli_shell_help_handler(ShellContext_t *s_shell,int argc, char *argv[]) {
 (void) argc;
 (void) argv;
  SHELL_FOR_EACH_COMMAND(command) {
    prv_echo_str(s_shell,command->command);
    prv_echo_str(s_shell,": ");
    prv_echo_str(s_shell,command->help);
    prv_echo(s_shell,'\n');
  }
  return 0;
}