#ifndef BOOT_FLASH_H
#define BOOT_FLASH_H

#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include "boot_err.h"
#ifndef SECONDARY_FLASH
#define SECONDARY_FLASH 1
#endif

#ifndef FACTORY_APP_FLASH
#define FACTORY_APP_FLASH 1
#endif

#ifndef SCRATCH_FLASH
#define SCRATCH_FLASH 1
#endif

#ifndef PARTITION_MAGIC_NUMBER
#define PARTITION_MAGIC_NUMBER 0xDEADBEEF
#endif

typedef struct BootFlashFunctions
{
    int (*init)(void);
    int (*erase)(uint32_t address, uint32_t size);
    int (*write)(uint32_t address, const uint8_t *data, uint32_t size);
    int (*read)(uint32_t address, uint8_t *data, uint32_t size);
} BootFlashFunctions_t;

typedef struct BootFlashPartition
{
    uint32_t primary_app_address;
    uint32_t primary_app_number_of_sectors;
    uint32_t primary_app_size;
#if SECONDARY_FLASH
    uint32_t secondary_app_address;
    uint32_t secondary_app_number_of_sectors;
    uint32_t secondary_app_size;
#endif
#if FACTORY_APP_FLASH
    uint32_t factory_app_address;
    uint32_t factory_app_number_of_sectors;
    uint32_t factory_app_size;
#endif
#if SCRATCH_FLASH
    uint32_t scratch_start_address;
    uint32_t scratch_number_of_sectors;
    uint32_t scratch_size;
#endif
    bool isNewFirmwareAvailable;
    bool isUpgradeInProgress;
    uint32_t magic_number;
} BootFlashPartition_t;

typedef struct BootFlash
{
    BootFlashFunctions_t *functions;
    BootFlashPartition_t partition;
} BootFlash_t;

static inline int boot_flash_init(BootFlash_t *boot_flash, BootFlashFunctions_t *functions)
{
    if (boot_flash->functions == NULL)
    {
        return BOOT_ERR_INVALID_PARAM; // Error: Invalid parameters
    }
    boot_flash->functions = functions;

    return boot_flash->functions->init();
}

static inline int boot_flash_erase(BootFlash_t *boot_flash, uint32_t address, uint32_t size)
{
    if (boot_flash->functions == NULL)
    {
        return BOOT_ERR_INVALID_PARAM; // Error: Invalid parameters
    }
    return boot_flash->functions->erase(address, size);
}

static inline int boot_flash_write(BootFlash_t *boot_flash, uint32_t address, const uint8_t *data, uint32_t size)
{
    if (boot_flash->functions == NULL)
    {
        return BOOT_ERR_INVALID_PARAM; // Error: Invalid parameters
    }
    return boot_flash->functions->write(address, data, size);
}

static inline int boot_flash_read(BootFlash_t *boot_flash, uint32_t address, uint8_t *data, uint32_t size)
{
    if (boot_flash->functions == NULL)
    {
        return BOOT_ERR_INVALID_PARAM; // Error: Invalid parameters
    }
    return boot_flash->functions->read(address, data, size);
}

static inline bool boot_flash_is_new_firmware_available(BootFlash_t *boot_flash)
{
    return boot_flash->partition.isNewFirmwareAvailable;
}

static inline bool boot_flash_is_upgrade_in_progress(BootFlash_t *boot_flash)
{
    return boot_flash->partition.isUpgradeInProgress;
}
extern BootFlashFunctions_t boot_flash_functions;
#endif // BOOT_FLASH_H
