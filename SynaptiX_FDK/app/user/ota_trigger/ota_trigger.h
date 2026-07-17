#ifndef OTA_TRIGGER_H
#define OTA_TRIGGER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Sets the isUpgradeInProgress flag inside BOOTLOADER_WS's on-chip
 * partition table (internal STM32H5 flash, see ota_trigger.c's
 * doc-comment for the address/layout this depends on) and then calls
 * NVIC_SystemReset(). After reset, BOOTLOADER_WS's bootloader_init()
 * reads the partition table, sees isUpgradeInProgress == true, and stays
 * in bootloader/DFU mode instead of jumping straight to this app — see
 * bootloader.c's bootloader_process()/bootloader_init().
 *
 * This board has no physical DFU button (per the user, 2026-07-17), so
 * this function is the ONLY way to enter DFU mode — it must be called
 * from both the CLI ("ota" command, shell_commands.c) and MQTT RPC
 * ("enterOtaMode" method, mqtt_rpc.c), which is why the logic lives here
 * as a single shared entry point rather than being duplicated in each
 * caller.
 *
 * Returns 0 on success (does not actually return in practice — the
 * function resets the MCU before returning). Returns a negative value if
 * the flash write failed, in which case the reset is NOT performed and
 * the caller should report the failure (do not proceed to reset the
 * device into a possibly-corrupt DFU flag state without knowing the
 * write actually succeeded).
 */
int ota_trigger_enter_dfu(void);

#ifdef __cplusplus
}
#endif

#endif /* OTA_TRIGGER_H */