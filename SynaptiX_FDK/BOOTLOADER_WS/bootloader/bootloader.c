#include "bootloader.h"
#include "boot_flash.h"
#include "flash_define.h"
#include "boot_debug.h"
#include <assert.h>

static void boot_partition_printf(BootFlashPartition_t *partition) {
    BOOT_DEBUG("Boot Flash Partition Info:");
    BOOT_DEBUG("Primary App Address: 0x%08X",(unsigned int) partition->primary_app_address);
    BOOT_DEBUG("Primary App Number of Sectors: %u",(unsigned int) partition->primary_app_number_of_sectors);
    BOOT_DEBUG("Primary App Size: %u bytes",(unsigned int) partition->primary_app_size);
#if SECONDARY_FLASH
    BOOT_DEBUG("Secondary App Address: 0x%08X",(unsigned int) partition->secondary_app_address);
    BOOT_DEBUG("Secondary App Number of Sectors: %u",(unsigned int) partition->secondary_app_number_of_sectors);
    BOOT_DEBUG("Secondary App Size: %u bytes",(unsigned int) partition->secondary_app_size);
#endif
#if FACTORY_APP_FLASH
    BOOT_DEBUG("Factory App Address: 0x%08X",(unsigned int) partition->factory_app_address);
    BOOT_DEBUG("Factory App Number of Sectors: %u",(unsigned int) partition->factory_app_number_of_sectors);
    BOOT_DEBUG("Factory App Size: %u bytes",(unsigned int) partition->factory_app_size);
#endif
#if SCRATCH_FLASH
    BOOT_DEBUG("Scratch Start Address: 0x%08X",(unsigned int) partition->scratch_start_address);
    BOOT_DEBUG("Scratch Number of Sectors: %u",(unsigned int) partition->scratch_number_of_sectors);
    BOOT_DEBUG("Scratch Size: %u bytes",(unsigned int) partition->scratch_size);
#endif
    BOOT_DEBUG("Is New Firmware Available: %s",(unsigned int) partition->isNewFirmwareAvailable ? "Yes" : "No");
    BOOT_DEBUG("Is Upgrade In Progress: %s",(unsigned int) partition->isUpgradeInProgress ? "Yes" : "No");
    BOOT_DEBUG("Magic Number: 0x%08X",(unsigned int) partition->magic_number);
}

#if SECONDARY_FLASH && SCRATCH_FLASH

static int boot_swap_firmware(Bootloader_t *bootloader)
{
    BOOT_DEBUG("Starting firmware swap process.");
    BootFlashPartition_t *partition = &bootloader->boot_flash.partition;
    BootFlash_t *flash = &bootloader->boot_flash;
    uint32_t app_size = partition->primary_app_size;
    uint32_t scratch_size = partition->scratch_size;
    uint32_t app_address = partition->primary_app_address;
    uint32_t scratch_addr = partition->scratch_start_address;
    uint32_t secondary_app_address = partition->secondary_app_address;
    assert(app_size != 0);
    assert(scratch_size != 0);
    assert((app_size % scratch_size) == 0);
    for(uint32_t i = 0;i<app_size / scratch_size; i++){

        // Erase scratch
        if(BOOT_ERR_NO_ERROR != boot_flash_erase(flash,scratch_addr,scratch_size)){
            BOOT_ERROR("Erase scratch failue");
            return BOOT_ERR_FLASH_ERASE_FAILED;
        }
        // Write Primary app sector i to scratch
        if(BOOT_ERR_NO_ERROR != boot_flash_write(flash,scratch_addr,(uint8_t*)app_address + i*scratch_size,scratch_size)){
            BOOT_ERROR("Write scratch failue");
            return BOOT_ERR_FLASH_WRITE_FAILED;
        }
        // Erase Primary app sector i
        if(BOOT_ERR_NO_ERROR != boot_flash_erase(flash,app_address + i * scratch_size,scratch_size)){
            BOOT_ERROR("Erase primary block %u failue",(unsigned int) i);
            return BOOT_ERR_FLASH_ERASE_FAILED;
        }
        // Write Secondary app sector i to Primary app sector i
        if(BOOT_ERR_NO_ERROR != boot_flash_write(flash,app_address + i * scratch_size,(uint8_t*)secondary_app_address + i*scratch_size,scratch_size)){
            BOOT_ERROR("Write primary block %u failue",(unsigned int) i);
            return BOOT_ERR_FLASH_WRITE_FAILED;
        }
        // Erase Secondary app sector i
        if(BOOT_ERR_NO_ERROR != boot_flash_erase(flash,secondary_app_address + i * scratch_size,scratch_size)){
            BOOT_ERROR("Erase secondary block %u failue",(unsigned int) i);
            return BOOT_ERR_FLASH_ERASE_FAILED;
        }
        // Write Scratch to Primary app sector i
        if(BOOT_ERR_NO_ERROR != boot_flash_write(flash,secondary_app_address + i * scratch_size,(uint8_t*)scratch_addr,scratch_size)){
            BOOT_ERROR("Write secondary block %u failue",(unsigned int) i);
            return BOOT_ERR_FLASH_WRITE_FAILED;
        }
        // Next block 
    }


    BOOT_DEBUG("Firmware swap process completed successfully.");
    return BOOT_ERR_NO_ERROR; // Return success for now
}

#endif

int bootloader_init(Bootloader_t *bootloader, void (*jump_to_application)(uint32_t addr),int (*read_boot_pin)(void),BootFlashFunctions_t *boot_flash_functions){
    BOOT_INFO("Version %s ",PROJECT_VERSION);
    BOOT_INFO("Auther : %s",PROJECT_AUTHER);
    BOOT_INFO("Phone : %s",PROJECT_PHONE_NUMBER);
    BOOT_INFO("Time : %s",PROJECT_TIME);
    if (bootloader == NULL|| jump_to_application == NULL || boot_flash_functions == NULL || read_boot_pin == NULL)
    {
        BOOT_ERROR("Bootloader initialization failed due to invalid parameters.");
        return BOOT_ERR_INVALID_PARAM; // Error: Invalid parameters
    }
    bootloader->jump_to_application = jump_to_application;
    bootloader->read_boot_pin = read_boot_pin;
    if(BOOT_ERR_NO_ERROR != boot_flash_init(&bootloader->boot_flash, boot_flash_functions)){
        BOOT_ERROR("Bootloader initialization failed due to flash initialization failure.");
        return BOOT_ERR_FLASH_INIT_FAILED; // Error: Flash initialization failed
    }

    if(BOOT_ERR_NO_ERROR != boot_flash_read(&bootloader->boot_flash,PARTITION_FLASH_START_ADDRESS,(uint8_t*)&bootloader->boot_flash.partition,sizeof(BootFlashPartition_t))){
        BOOT_ERROR("Bootloader initialization failed due to flash read partition failure.");
        return BOOT_ERR_FLASH_READ_FAILED;
    }
    if(bootloader->boot_flash.partition.magic_number != PARTITION_MAGIC_NUMBER){
        BOOT_WARN("Partition magic number mismatch. Reinitializing partition.");
        bootloader->boot_flash.partition.isNewFirmwareAvailable = true;
        bootloader->boot_flash.partition.isUpgradeInProgress = false;
        bootloader->boot_flash.partition.magic_number = PARTITION_MAGIC_NUMBER;
        #if FACTORY_APP_FLASH
        bootloader->boot_flash.partition.factory_app_address = FACTORY_APP_FLASH_START_ADDRESS;
        bootloader->boot_flash.partition.factory_app_number_of_sectors = FACTORY_APP_FLASH_NUMBER_OF_SECTORS;
        bootloader->boot_flash.partition.factory_app_size = FACTORY_APP_FLASH_SIZE;
        #endif
        #if SECONDARY_FLASH
        bootloader->boot_flash.partition.secondary_app_address = SECONDARY_APP_FLASH_START_ADDRESS;
        bootloader->boot_flash.partition.secondary_app_number_of_sectors = SECONDARY_APP_FLASH_NUMBER_OF_SECTORS;
        bootloader->boot_flash.partition.secondary_app_size = SECONDARY_APP_FLASH_SIZE;
        #endif
        #if SCRATCH_FLASH
        bootloader->boot_flash.partition.scratch_start_address = SCRATCH_FLASH_START_ADDRESS;
        bootloader->boot_flash.partition.scratch_number_of_sectors = SCRATCH_FLASH_NUMBER_OF_SECTORS;
        bootloader->boot_flash.partition.scratch_size = SCRATCH_FLASH_SIZE;
        #endif
        bootloader->boot_flash.partition.primary_app_address = PRIMARY_APP_FLASH_START_ADDRESS;
        bootloader->boot_flash.partition.primary_app_number_of_sectors = PRIMARY_APP_FLASH_NUMBER_OF_SECTORS;
        bootloader->boot_flash.partition.primary_app_size = PRIMARY_APP_FLASH_SIZE;
        if(BOOT_ERR_NO_ERROR != boot_flash_erase(&bootloader->boot_flash,PARTITION_FLASH_START_ADDRESS,PARTITION_FLASH_SIZE)){
            BOOT_ERROR("Bootloader initialization failed due to flash erase partition failure.");
            return BOOT_ERR_FLASH_ERASE_FAILED;
        }
        if(BOOT_ERR_NO_ERROR != boot_flash_write(&bootloader->boot_flash,PARTITION_FLASH_START_ADDRESS,(uint8_t*)&bootloader->boot_flash.partition,sizeof(BootFlashPartition_t))){
            BOOT_ERROR("Bootloader initialization failed due to flash write partition failure.");
            return BOOT_ERR_FLASH_WRITE_FAILED;
        }
        BOOT_DEBUG("Partition reinitialized successfully.");
        boot_partition_printf(&bootloader->boot_flash.partition);
        return BOOT_PARTITION_MGIC_NUMBER_MISMATCH;
    }
    if(bootloader->read_boot_pin() == ENTER_BOOT_UPDATE_MODE){
        bootloader->boot_flash.partition.isUpgradeInProgress = true;
    }
    // bootloader->boot_flash.partition.isNewFirmwareAvailable = true;
    boot_partition_printf(&bootloader->boot_flash.partition);

    if(!bootloader_check_for_new_firmware(bootloader) && !boot_flash_is_upgrade_in_progress(&bootloader->boot_flash)){
        BOOT_DEBUG("No new firmware available. Jumping to application.");
        return bootloader_jump_to_application(bootloader);
    }
    return BOOT_ERR_NO_ERROR; // Success
    
}
int bootloader_save_partition(Bootloader_t *bootloader){
    if(BOOT_ERR_NO_ERROR != boot_flash_erase(&bootloader->boot_flash,PARTITION_FLASH_START_ADDRESS,PARTITION_FLASH_SIZE)){
            BOOT_ERROR("Bootloader initialization failed due to flash erase partition failure.");
            return BOOT_ERR_FLASH_ERASE_FAILED;
        }
        if(BOOT_ERR_NO_ERROR != boot_flash_write(&bootloader->boot_flash,PARTITION_FLASH_START_ADDRESS,(uint8_t*)&bootloader->boot_flash.partition,sizeof(BootFlashPartition_t))){
            BOOT_ERROR("Bootloader initialization failed due to flash write partition failure.");
            return BOOT_ERR_FLASH_WRITE_FAILED;
        }
    BOOT_DEBUG("Partition reinitialized successfully.");
    boot_partition_printf(&bootloader->boot_flash.partition);
    return BOOT_ERR_NO_ERROR;
}
int bootloader_process(Bootloader_t *boot){
    if(bootloader_check_for_new_firmware(boot)){
        #if SECONDARY_FLASH && SCRATCH_FLASH
            boot_swap_firmware(boot);
            boot->boot_flash.partition.isNewFirmwareAvailable = false;
            bootloader_save_partition(boot);
            bootloader_jump_to_application(boot);
        #endif
        return 0;
    }
    if(boot_flash_is_upgrade_in_progress(&boot->boot_flash)){
        return 0;
    }
    else{
        bootloader_jump_to_application(boot);
        return 0;
    }
    return 0;
}

