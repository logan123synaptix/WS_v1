/* CLI command table for V1's USB CDC shell. Follows WS_v0's
 * apps/cli_shell_command.c pattern (const Cli_Shell_Cmd[] + a single
 * "settings -i/-c" command), extended to cover network_config_t's
 * broker/APN fields in addition to the pump/sensing/sleep timing and pump
 * PWM duty (-duty, WS_v0's separate "-duty" flag) WS_v0 had. TLS certs are
 * deliberately NOT exposed here — PEM text is too long for a CLI line;
 * use a future USB MSC file-edit path for those. */

#include "cli_shell.h"
#include "network_config.h"
#include "sx_board.h"
#include "ota_trigger.h"
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

static int cli_cmd_restart(ShellContext_t *shell, int argc, char *argv[]);
static int cli_cmd_settings(ShellContext_t *shell, int argc, char *argv[]);
static int cli_cmd_ota(ShellContext_t *shell, int argc, char *argv[]);

static const Cli_Shell_Cmd s_shell_commands[] = {
    {"help",     cli_shell_help_handler, "Lists all commands\r\n"},
    {"restart",  cli_cmd_restart,        "Restart device\r\n"},
    {"ota",      cli_cmd_ota,            "Reboot into bootloader DFU update mode (no physical DFU button on this board)\r\n"},
    {"settings", cli_cmd_settings,
        "settings -i : show current settings\r\n"
        "settings -c : configure settings (any subset of flags below)\r\n"
        "\t\t-deviceid [str]      device ID (reported in payloads)\r\n"
        "\t\t-pump [seconds]      pump-on duration\r\n"
        "\t\t-duty [0-100]        pump PWM strength (%)\r\n"
        "\t\t-sensing [seconds]   sensing duration\r\n"
        "\t\t-sleep [seconds]     STOP-mode sleep duration\r\n"
        "\t\t-host [str]          MQTT broker host\r\n"
        "\t\t-port [n]            MQTT broker port\r\n"
        "\t\t-clientid [str]      MQTT client id\r\n"
        "\t\t-user [str]          MQTT username (empty = no auth)\r\n"
        "\t\t-pass [str]          MQTT password (empty = no auth)\r\n"
        "\t\t-keepalive [n]       MQTT keepalive (seconds)\r\n"
        "\t\t-apn [str]           GSM APN\r\n"
        "\t\t-apnuser [str]       GSM APN username\r\n"
        "\t\t-apnpass [str]       GSM APN password\r\n"
        "example : settings -c -pump 30 -duty 100 -sensing 60 -sleep 300 -host mqtt.example.com\r\n"},
};

const Cli_Shell_Cmd *const g_shell_commands = s_shell_commands;
const size_t g_num_shell_commands = ARRAY_SIZE(s_shell_commands);

static int cli_cmd_restart(ShellContext_t *shell, int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    cli_shell_put_line(shell, "Device restarting...");
    NVIC_SystemReset();
    return 0; /* unreachable */
}

static int cli_cmd_ota(ShellContext_t *shell, int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    cli_shell_put_line(shell, "Entering DFU update mode...");
    int rc = ota_trigger_enter_dfu();
    /* Only reached if the flash write failed — on success,
     * ota_trigger_enter_dfu() resets the MCU and never returns here. */
    cli_shell_printf(shell, "Failed to enter DFU mode (err %d)\r\n", rc);
    return -1;
}

static void print_settings(ShellContext_t *shell)
{
    const network_config_t *cfg = network_config_get();
    cli_shell_printf(shell,
        "---- Application Settings ----\r\n"
        "Device ID: %s\r\n"
        "Pump on (s): %lu\r\n"
        "Pump duty (%%): %u\r\n"
        "Sensing (s): %lu\r\n"
        "Sleep (s): %lu\r\n"
        "Host: %s\r\n"
        "Port: %u\r\n"
        "Client ID: %s\r\n"
        "Username: %s\r\n"
        "Password: %s\r\n"
        "Keepalive (s): %u\r\n"
        "APN: %s\r\n"
        "APN username: %s\r\n"
        "APN password: %s\r\n"
        "------------------------------\r\n",
        cfg->device_id,
        (unsigned long)(cfg->pump_on_ms / 1000U),
        cfg->pump_duty_percent,
        (unsigned long)(cfg->sensing_ms / 1000U),
        (unsigned long)(cfg->sleep_ms / 1000U),
        cfg->host,
        cfg->port,
        cfg->client_id,
        cfg->username,
        cfg->password,
        cfg->keepalive_s,
        cfg->apn,
        cfg->apn_username,
        cfg->apn_password);
}

/* settings -c parses argv in -flag value pairs starting at index 2 (argv[0]
 * is "settings", argv[1] is "-c"), same shape as WS_v0's cli_settings().
 * Unlike WS_v0, a value of 0 for the timing flags is rejected rather than
 * silently ignored, and any unrecognized flag aborts the whole command
 * (rather than being silently skipped) so a typo doesn't leave the user
 * thinking a setting was applied when it wasn't. */
static int cli_cmd_settings(ShellContext_t *shell, int argc, char *argv[])
{
    if (argc == 2 && strcmp(argv[1], "-i") == 0) {
        print_settings(shell);
        return 0;
    }

    if (!(argc >= 4 && (argc % 2) == 0 && strcmp(argv[1], "-c") == 0)) {
        cli_shell_help_handler(shell, 0, NULL);
        return -1;
    }

    for (int i = 2; i < argc; i += 2) {
        const char *flag = argv[i];
        const char *val  = argv[i + 1];

        if (strcmp(flag, "-deviceid") == 0) {
            network_config_set_device_id(val);
        } else if (strcmp(flag, "-pump") == 0) {
            long seconds = strtol(val, NULL, 10);
            if (seconds <= 0) {
                cli_shell_printf(shell, "-pump must be > 0\r\n");
                return -1;
            }
            network_config_set_pump_on_ms((uint32_t)seconds * 1000U);
        } else if (strcmp(flag, "-duty") == 0) {
            long duty = strtol(val, NULL, 10);
            if (duty < 0 || duty > 100) {
                cli_shell_printf(shell, "-duty must be 0-100\r\n");
                return -1;
            }
            network_config_set_pump_duty_percent((uint8_t)duty);
        } else if (strcmp(flag, "-sensing") == 0) {
            long seconds = strtol(val, NULL, 10);
            if (seconds <= 0) {
                cli_shell_printf(shell, "-sensing must be > 0\r\n");
                return -1;
            }
            network_config_set_sensing_ms((uint32_t)seconds * 1000U);
        } else if (strcmp(flag, "-sleep") == 0) {
            long seconds = strtol(val, NULL, 10);
            if (seconds <= 0) {
                cli_shell_printf(shell, "-sleep must be > 0\r\n");
                return -1;
            }
            network_config_set_sleep_ms((uint32_t)seconds * 1000U);
        } else if (strcmp(flag, "-host") == 0) {
            network_config_set_host(val);
        } else if (strcmp(flag, "-port") == 0) {
            network_config_set_port((uint16_t)strtol(val, NULL, 10));
        } else if (strcmp(flag, "-clientid") == 0) {
            network_config_set_client_id(val);
        } else if (strcmp(flag, "-user") == 0) {
            network_config_set_username(val);
        } else if (strcmp(flag, "-pass") == 0) {
            network_config_set_password(val);
        } else if (strcmp(flag, "-keepalive") == 0) {
            network_config_set_keepalive((uint16_t)strtol(val, NULL, 10));
        } else if (strcmp(flag, "-apn") == 0) {
            network_config_set_apn(val);
        } else if (strcmp(flag, "-apnuser") == 0) {
            network_config_set_apn_username(val);
        } else if (strcmp(flag, "-apnpass") == 0) {
            network_config_set_apn_password(val);
        } else {
            cli_shell_printf(shell, "Unknown flag: %s\r\n", flag);
            return -1;
        }
    }

    if (!network_config_save()) {
        cli_shell_printf(shell, "Failed to save settings to flash\r\n");
        return -1;
    }

    cli_shell_printf(shell, "Settings saved. Reboot not required — "
                             "timing takes effect on the next cycle "
                             "tick, broker settings on next MQTT (re)connect.\r\n");
    print_settings(shell);
    return 0;
}