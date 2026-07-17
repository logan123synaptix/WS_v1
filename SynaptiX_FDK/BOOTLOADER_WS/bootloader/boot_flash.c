#include "boot_flash.h"
#include "flash_define.h"
#include <stdint.h>

#if CFG_BOOT_MCU == BOOT_MCU_H5
#include "stm32h5xx_hal.h"
#include "stm32h5xx_hal_flash.h"
#include <stm32h5xx_hal_flash_ex.h>
#include "boot_debug.h"

int flash_init(void) {
    return BOOT_ERR_NO_ERROR;
}

int flash_erase(uint32_t address, uint32_t size) {
    if (HAL_FLASH_Unlock() != HAL_OK) {
        return BOOT_ERR_FLASH_ERASE_FAILED;
    }
    
    FLASH_EraseInitTypeDef erase_init;
    erase_init.TypeErase = FLASH_TYPEERASE_SECTORS;

    uint32_t sector = (address - FLASH_BASE) / FLASH_SECTOR_SIZE;
    uint32_t num_sector = size / FLASH_SECTOR_SIZE;
    // BOOT_DEBUG("Erase sector : %u, num sector : %u",sector % 128,num_sector);
    if(sector > 127){
        // BOOT_DEBUG("Erase bank 2");
        erase_init.Banks = FLASH_BANK_2;
    }
    else{
        // BOOT_DEBUG("Erase bank 1");
        erase_init.Banks = FLASH_BANK_1;
    }
    erase_init.Sector = sector % 128;
    erase_init.NbSectors = num_sector;

    uint32_t sector_error;
    if (HAL_FLASHEx_Erase(&erase_init, &sector_error) != HAL_OK) {
        BOOT_ERROR("Erase sector err : %u",(unsigned int) sector_error);
        HAL_FLASH_Lock();
        return BOOT_ERR_FLASH_ERASE_FAILED;
    }

    HAL_FLASH_Lock();
    return BOOT_ERR_NO_ERROR;
}

int flash_write(uint32_t address, const uint8_t *data, uint32_t size) {
    if (HAL_FLASH_Unlock() != HAL_OK) {
        return BOOT_ERR_FLASH_WRITE_FAILED;
    }
    uint32_t address_write = address;
    uint32_t quadword[4];
    // __disable_irq();
    BOOT_DEBUG("Write to 0x%08X from 0x%08X size %u",address,data,size);
    while(address_write < address + size) {
        memcpy(quadword, data + (address_write - address), sizeof(quadword));
        HAL_StatusTypeDef err = HAL_FLASH_Program(FLASH_TYPEPROGRAM_QUADWORD, address_write,(uint32_t) quadword);
        if (err != HAL_OK) {
            BOOT_ERROR("Write flash false : %d",err);
            HAL_FLASH_Lock();
            return BOOT_ERR_FLASH_WRITE_FAILED;
        }
        address_write += 16; // Move to the next quadword
        // HAL_Delay(10);
    }
    // __enable_irq();
    HAL_FLASH_Lock();
    return BOOT_ERR_NO_ERROR;
}

int flash_read(uint32_t address, uint8_t *data, uint32_t size) {
    memcpy(data, (const void *)address, size);
    return BOOT_ERR_NO_ERROR;
}
#elif CFG_BOOT_MCU == BOOT_MCU_F0 || CFG_BOOT_MCU == BOOT_MCU_F1 || CFG_BOOT_MCU == BOOT_MCU_F2 || CFG_BOOT_MCU == BOOT_MCU_F3 || CFG_BOOT_MCU == BOOT_MCU_F4 || CFG_BOOT_MCU == BOOT_MCU_F7

#elif CFG_BOOT_MCU == BOOT_MCU_L0 || CFG_BOOT_MCU == BOOT_MCU_L4
#elif CFG_BOOT_MCU == BOOT_MCU_U3 || CFG_BOOT_MCU == BOOT_MCU_U5
#else
#include "boot_debug.h"
int flash_init(void) {
    BOOT_DEBUG("Flash init called. No specific implementation for this MCU.");
    return BOOT_ERR_NO_ERROR;
}

int flash_erase(uint32_t address, uint32_t size) {
    BOOT_DEBUG("Flash erase called. No specific implementation for this MCU.");
    return BOOT_ERR_NO_ERROR;
}

int flash_write(uint32_t address, const uint8_t *data, uint32_t size) {
    BOOT_DEBUG("Flash write called. No specific implementation for this MCU.");
    return BOOT_ERR_NO_ERROR;
}

int flash_read(uint32_t address, uint8_t *data, uint32_t size) {
    BOOT_DEBUG("Flash read called. No specific implementation for this MCU.");
    return BOOT_ERR_NO_ERROR;
}
#endif

BootFlashFunctions_t boot_flash_functions = {
    .init = flash_init,
    .erase = flash_erase,
    .write = flash_write,
    .read = flash_read
};
