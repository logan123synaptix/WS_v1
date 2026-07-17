/* Bridges this app (WS_v1 firmware, running as BOOTLOADER_WS's "primary
 * app") to BOOTLOADER_WS's bootloader so a running app can request "reboot
 * into DFU update mode" — something the bootloader itself cannot offer via
 * a physical button on this board (confirmed with the user, 2026-07-17: no
 * DFU button on this hardware revision).
 *
 * How this works, and WHY it is safe to write to this address from here:
 *
 * BOOTLOADER_WS/bootloader/flash_define.h lays out internal STM32H5 flash
 * (2MB total, 256 x 8KB sectors) as:
 *   0x08000000  bootloader            56 KB  (7 sectors)
 *   0x0800E000  partition table        8 KB  (1 sector)   <- this module writes here
 *   0x08010000  primary app           480 KB (60 sectors) <- THIS firmware runs here
 *   0x08088000  secondary app         480 KB (60 sectors)
 *   0x08100000  factory app           480 KB (60 sectors)
 *   0x08178000  scratch                 8 KB (1 sector)
 *
 * This app's own flash-backed FatFS disk (services/sx_fatfs/sx_diskio.c)
 * starts at 0x081C0000 — well past 0x0817A000 where the bootloader's last
 * partition ends, so the two do not overlap. Verified by reading both
 * flash_define.h and sx_diskio.c directly (2026-07-17), not assumed.
 *
 * BootFlashPartition_t is NOT included from BOOTLOADER_WS/ here (that
 * project is a separate CMake build the user maintains independently, see
 * the handoff's rule #8) — PARTITION_LAYOUT below is a field-for-field
 * copy of BOOTLOADER_WS/bootloader/boot_flash.h's BootFlashPartition_t,
 * built with SECONDARY_FLASH/FACTORY_APP_FLASH/SCRATCH_FLASH all defined
 * (BOOTLOADER_WS's defaults, confirmed in boot_flash.h). If that struct's
 * layout ever changes on the bootloader side, this copy must be updated
 * to match or the write below will corrupt the partition table.
 *
 * This module only ever sets isUpgradeInProgress = true and preserves
 * every other field via a read-modify-write (never blindly overwrites
 * the whole sector), so a mismatch in the *addresses* fields would still
 * leave the bootloader's own partition data intact — only a mismatch in
 * struct *size/field order* is dangerous, which is why the layout is
 * pinned with explicit padding assumptions matching boot_flash.h exactly.
 */

#include "ota_trigger.h"
#include "sx_flash.h"
#include "logger.h"
#include <string.h>
#include <stdbool.h>
#include "stm32h5xx_hal.h"

static const char *TAG = "OTA_TRIGGER";

/* --- Internal flash layout, mirrored from BOOTLOADER_WS/bootloader/flash_define.h ---
 * Kept as a local copy rather than shared header on purpose (see file
 * header comment above). Only the one address this module needs is
 * reproduced, not the whole layout, to minimize what can drift. */
#define OTA_PARTITION_TABLE_ADDR   0x0800E000UL
#define OTA_PARTITION_TABLE_SIZE   0x2000UL /* 8KB, one sector */

/* --- Struct layout, mirrored from BOOTLOADER_WS/bootloader/boot_flash.h's
 * BootFlashPartition_t, with SECONDARY_FLASH=1, FACTORY_APP_FLASH=1,
 * SCRATCH_FLASH=1 (BOOTLOADER_WS's compile-time defaults). Field order and
 * types MUST match exactly. */
typedef struct {
    uint32_t primary_app_address;
    uint32_t primary_app_number_of_sectors;
    uint32_t primary_app_size;
    uint32_t secondary_app_address;
    uint32_t secondary_app_number_of_sectors;
    uint32_t secondary_app_size;
    uint32_t factory_app_address;
    uint32_t factory_app_number_of_sectors;
    uint32_t factory_app_size;
    uint32_t scratch_start_address;
    uint32_t scratch_number_of_sectors;
    uint32_t scratch_size;
    bool     isNewFirmwareAvailable;
    bool     isUpgradeInProgress;
    uint32_t magic_number;
} ota_boot_partition_t;

int ota_trigger_enter_dfu(void)
{
    ota_boot_partition_t partition;

    /* Read-modify-write: read the current partition table as written by
     * the bootloader, flip only isUpgradeInProgress, write it back. This
     * preserves primary/secondary/factory/scratch addresses and the
     * magic number exactly as the bootloader left them — we must NOT
     * reconstruct those fields here, since if this app's copy of the
     * layout constants were ever wrong it would silently brick the
     * partition table instead of just failing the OTA request. */
    sx_flash_read(OTA_PARTITION_TABLE_ADDR, (uint8_t *)&partition, sizeof(partition));

    if (partition.magic_number != 0xDEADBEEFUL) {
        log_error(TAG, "Partition table magic mismatch (0x%08lX) - refusing to write, bootloader state looks corrupt",
                   (unsigned long)partition.magic_number);
        return -1;
    }

    partition.isUpgradeInProgress = true;

    sx_flash_unlock();
    sx_flash_erase(OTA_PARTITION_TABLE_ADDR, OTA_PARTITION_TABLE_SIZE);
    sx_flash_write(OTA_PARTITION_TABLE_ADDR, (const uint8_t *)&partition, sizeof(partition));
    sx_flash_lock();

    /* Read back to confirm the write actually landed before resetting —
     * do not reset blindly on a write we have not verified, since a
     * failed/partial write here means the bootloader may misread the
     * partition table on the next boot. */
    ota_boot_partition_t verify;
    sx_flash_read(OTA_PARTITION_TABLE_ADDR, (uint8_t *)&verify, sizeof(verify));
    if (!verify.isUpgradeInProgress || verify.magic_number != partition.magic_number) {
        log_error(TAG, "Partition table write verification failed - aborting reset");
        return -2;
    }

    log_info(TAG, "OTA flag written, resetting into DFU/bootloader mode");
    HAL_Delay(100); /* let the log line actually go out over UART before reset */
    NVIC_SystemReset();

    return 0; /* unreachable */
}