#ifndef FLASH_DEFINE_H
#define FLASH_DEFINE_H

#define SECONDARY_FLASH 1
#define FACTORY_APP_FLASH 1
#define SCRATCH_FLASH 1

#define FLASH_NUMBER_OF_SECTORS 256

#define BOOT_FLASH_SECTOR_SIZE 0x2000 // 8KB sector size for STM32H5 series

#define BOOTLOADER_FLASH_START_ADDRESS 0x08000000
#define BOOTLOADER_FLASH_NUMBER_OF_SECTORS 7
#define BOOTLOADER_FLASH_SIZE (BOOTLOADER_FLASH_NUMBER_OF_SECTORS * BOOT_FLASH_SECTOR_SIZE)

#define PARTITION_FLASH_START_ADDRESS (BOOTLOADER_FLASH_START_ADDRESS + BOOTLOADER_FLASH_SIZE)
#define PARTITION_FLASH_NUMBER_OF_SECTORS 1
#define PARTITION_FLASH_SIZE BOOT_FLASH_SECTOR_SIZE // 8KB partition size for STM32H5 series

#define PRIMARY_APP_FLASH_START_ADDRESS (PARTITION_FLASH_START_ADDRESS + PARTITION_FLASH_SIZE)
#define PRIMARY_APP_FLASH_NUMBER_OF_SECTORS 60
#define PRIMARY_APP_FLASH_SIZE (PRIMARY_APP_FLASH_NUMBER_OF_SECTORS * BOOT_FLASH_SECTOR_SIZE)

/* Bug fix (2026-08-05): Secondary used to sit right after Primary
 * (0x08088000-0x08100000), which is entirely inside FLASH BANK 1 on the
 * STM32H563 (2MB dual-bank part: Bank 1 = 0x08000000-0x080FFFFF, Bank 2 =
 * 0x08100000-0x081FFFFF). fota.c's fota_download_attempt() runs from the
 * Primary app (itself in Bank 1, see PRIMARY_APP_FLASH_START_ADDRESS
 * above) and calls sx_flash_erase()/sx_flash_write() directly on
 * Secondary while the CPU is actively executing code from that same
 * bank - confirmed on real hardware to HardFault immediately inside
 * fota_download_attempt() (erasing/programming a bank while fetching
 * instructions from it is not safe on this part; RWW only works across
 * banks, not within one). This never showed up in the bootloader's own
 * boot_swap_firmware()/new_boot_copy_factory_to_primary() paths because
 * the bootloader always executes from 0x08000000-0x0800E000, never from
 * Secondary or Factory, so it was always safe there regardless of which
 * bank Secondary lived in.
 *
 * Fix: keep FACTORY_APP_FLASH_START_ADDRESS exactly where it already is
 * (0x08100000, start of Bank 2) - a real factory app has already been
 * flashed there via flash-factory/rollback-factory on the test board and
 * must not move. Place Secondary AFTER Factory instead of before it, so
 * Secondary also lands in Bank 2 (0x08178000-0x081F0000) without
 * disturbing Factory's address. Scratch follows Secondary as before,
 * also now in Bank 2 (0x081F0000-0x081F2000) - Bank 2 total is 1MB
 * (ends 0x08200000), so Factory+Secondary+Scratch = 480K+480K+8K =
 * 968K leaves 56K of headroom, confirmed to fit.
 *
 * IMPORTANT DEPLOYMENT NOTE: boot_swap_firmware() reads
 * secondary_app_address from the on-flash partition table
 * (PARTITION_FLASH_START_ADDRESS, written once and only rewritten when
 * magic_number mismatches - see bootloader_init() in bootloader.c), NOT
 * from these compile-time constants directly. Any board already
 * provisioned with the old partition table (secondary_app_address =
 * 0x08088000) must have ONLY the partition table sector erased
 * (PARTITION_FLASH_START_ADDRESS, 8KB) before this new firmware's first
 * boot, so bootloader_init() detects the magic mismatch and re-derives
 * the partition table from these updated constants. Do NOT erase
 * Factory's sectors - the factory app already flashed there must survive
 * this partition-table reset untouched. */
#define FACTORY_APP_FLASH_START_ADDRESS (PRIMARY_APP_FLASH_START_ADDRESS + PRIMARY_APP_FLASH_SIZE)
#define FACTORY_APP_FLASH_NUMBER_OF_SECTORS 60
#define FACTORY_APP_FLASH_SIZE (FACTORY_APP_FLASH_NUMBER_OF_SECTORS * BOOT_FLASH_SECTOR_SIZE)

#define SECONDARY_APP_FLASH_START_ADDRESS (FACTORY_APP_FLASH_START_ADDRESS + FACTORY_APP_FLASH_SIZE)
#define SECONDARY_APP_FLASH_NUMBER_OF_SECTORS 60
#define SECONDARY_APP_FLASH_SIZE (SECONDARY_APP_FLASH_NUMBER_OF_SECTORS * BOOT_FLASH_SECTOR_SIZE)

#define SCRATCH_FLASH_START_ADDRESS (SECONDARY_APP_FLASH_START_ADDRESS + SECONDARY_APP_FLASH_SIZE)
#define SCRATCH_FLASH_NUMBER_OF_SECTORS 1
#define SCRATCH_FLASH_SIZE (SCRATCH_FLASH_NUMBER_OF_SECTORS * BOOT_FLASH_SECTOR_SIZE)

#define BOOT_MCU_C0 0
#define BOOT_MCU_C5 1
#define BOOT_MCU_G0 2
#define BOOT_MCU_F0 3
#define BOOT_MCU_F1 4
#define BOOT_MCU_F2 5
#define BOOT_MCU_F3 6
#define BOOT_MCU_F4 7
#define BOOT_MCU_F7 8
#define BOOT_MCU_H5 9
#define BOOT_MCU_H7 10
#define BOOT_MCU_L0 11
#define BOOT_MCU_L4 12
#define BOOT_MCU_L5 13
#define BOOT_MCU_U3 14
#define BOOT_MCU_U5 15

// #define CFG_BOOT_MCU BOOT_MCU_H5

#endif // FLASH_DEFINE_H