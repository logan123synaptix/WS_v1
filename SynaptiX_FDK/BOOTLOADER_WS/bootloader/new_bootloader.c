#include "new_bootloader.h"
#include "new_boot_backup_reg.h"
#include "new_magic_flash.h"
#include "boot_flash.h"
#include "boot_err.h"
#include "boot_debug.h"
#include <assert.h>

// Declared in bootloader.c but not exposed in bootloader.h; caller (main.c
// build) exposes it there per the 3-touch-point change documented in the
// handoff. Re-declared here so this translation unit can call it without
// depending on that header edit having been applied yet.
extern int boot_swap_firmware(Bootloader_t *bootloader);

#if SECONDARY_FLASH && SCRATCH_FLASH && FACTORY_APP_FLASH

/*
 * One-way copy Factory app -> Primary app, through Scratch, one
 * scratch_size block at a time - same block-by-block pattern as
 * boot_swap_firmware() in bootloader.c, but simpler since Factory itself
 * is never modified (read-only source, no need to preserve its old
 * contents anywhere).
 *
 * Extra assert vs. boot_swap_firmware(): factory_app_size must be >=
 * primary_app_size. boot_swap_firmware() doesn't need this check because
 * Primary and Secondary are provisioned identically; here Factory could in
 * principle be a different (smaller) partition, which would silently copy
 * a truncated image into Primary. Fail loudly instead.
 */
static int new_boot_copy_factory_to_primary(Bootloader_t *bootloader)
{
    BOOT_DEBUG("Starting factory rollback (Factory -> Primary) copy.");
    BootFlashPartition_t *partition = &bootloader->boot_flash.partition;
    BootFlash_t *flash = &bootloader->boot_flash;

    uint32_t primary_size = partition->primary_app_size;
    uint32_t factory_size = partition->factory_app_size;
    uint32_t scratch_size = partition->scratch_size;
    uint32_t primary_addr = partition->primary_app_address;
    uint32_t factory_addr = partition->factory_app_address;
    uint32_t scratch_addr = partition->scratch_start_address;

    assert(primary_size != 0);
    assert(scratch_size != 0);
    assert((primary_size % scratch_size) == 0);
    assert(factory_size >= primary_size);

    for (uint32_t i = 0; i < primary_size / scratch_size; i++)
    {
        // Erase scratch
        if (BOOT_ERR_NO_ERROR != boot_flash_erase(flash, scratch_addr, scratch_size))
        {
            BOOT_ERROR("Erase scratch failure");
            return BOOT_ERR_FLASH_ERASE_FAILED;
        }
        // Read Factory block i into scratch
        if (BOOT_ERR_NO_ERROR != boot_flash_write(flash, scratch_addr, (uint8_t *)factory_addr + i * scratch_size, scratch_size))
        {
            BOOT_ERROR("Write scratch failure");
            return BOOT_ERR_FLASH_WRITE_FAILED;
        }
        // Erase Primary block i
        if (BOOT_ERR_NO_ERROR != boot_flash_erase(flash, primary_addr + i * scratch_size, scratch_size))
        {
            BOOT_ERROR("Erase primary block %u failure", (unsigned int)i);
            return BOOT_ERR_FLASH_ERASE_FAILED;
        }
        // Write scratch (= Factory block i) into Primary block i
        if (BOOT_ERR_NO_ERROR != boot_flash_write(flash, primary_addr + i * scratch_size, (uint8_t *)scratch_addr, scratch_size))
        {
            BOOT_ERROR("Write primary block %u failure", (unsigned int)i);
            return BOOT_ERR_FLASH_WRITE_FAILED;
        }
        // Next block
    }

    BOOT_DEBUG("Factory rollback copy completed successfully.");
    return BOOT_ERR_NO_ERROR;
}

#endif // SECONDARY_FLASH && SCRATCH_FLASH && FACTORY_APP_FLASH

void new_bootloader_check_commands(Bootloader_t *bootloader)
{
    boot_backup_reg_init();

    uint32_t rollback_factory_flag = boot_backup_reg_read(BOOT_BACKUP_REG_ROLLBACK_FACTORY);
    uint32_t rollback_prev_flag    = boot_backup_reg_read(BOOT_BACKUP_REG_ROLLBACK_PREV);
    uint32_t update_flag           = boot_backup_reg_read(BOOT_BACKUP_REG_UPDATE);

    if (rollback_factory_flag == BOOT_MAGIC_ROLLBACK_FACTORY)
    {
        boot_backup_reg_clear(BOOT_BACKUP_REG_ROLLBACK_FACTORY);
#if SECONDARY_FLASH && SCRATCH_FLASH && FACTORY_APP_FLASH
        BOOT_DEBUG("rollback-factory command detected.");
        new_boot_copy_factory_to_primary(bootloader);
        bootloader_jump_to_application(bootloader);
#else
        BOOT_ERROR("rollback-factory requested but Factory/Secondary/Scratch flash not configured.");
#endif
        return;
    }

    if (rollback_prev_flag == BOOT_MAGIC_ROLLBACK_PREV)
    {
        boot_backup_reg_clear(BOOT_BACKUP_REG_ROLLBACK_PREV);
#if SECONDARY_FLASH && SCRATCH_FLASH
        BOOT_DEBUG("rollback-previous command detected.");
        boot_swap_firmware(bootloader);
        bootloader_jump_to_application(bootloader);
#else
        BOOT_ERROR("rollback-previous requested but Secondary/Scratch flash not configured.");
#endif
        return;
    }

    if (update_flag == BOOT_MAGIC_UPDATE)
    {
        boot_backup_reg_clear(BOOT_BACKUP_REG_UPDATE);
        BOOT_DEBUG("update command detected. Entering firmware update mode.");
        bootloader->boot_flash.partition.isUpgradeInProgress = true;
        bootloader_save_partition(bootloader);
        // Deliberately do NOT jump to application here - the caller's
        // while(1) { tud_task(); ... } DFU-wait loop in main.c takes over.
        return;
    }

    // No flag set: normal boot, existing bootloader_init()/bootloader_process()
    // flow decides what happens next.
}