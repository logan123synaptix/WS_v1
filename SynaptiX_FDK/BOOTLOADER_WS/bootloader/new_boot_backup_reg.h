#ifndef NEW_BOOT_BACKUP_H
#define NEW_BOOT_BACKUP_H

#include <stdint.h>

/*
 * Access to TAMP backup registers (TAMP->BKPxR), used to pass a one-shot
 * command flag from the main application to the bootloader across a
 * software reset (NVIC_SystemReset()).
 *
 * These registers survive System reset / software reset. They do NOT
 * survive a full power loss unless VBAT is connected (STM32H563 backup
 * domain). Since every flag written here is always followed immediately
 * by a reset (no power cycle in between), this is sufficient for the
 * intended use case (shell/MQTT-triggered update or rollback commands).
 *
 * Register assignment (see new_magic_flash.h for the magic values):
 *   BKP0R - "enter firmware update mode" flag (value = BOOT_MAGIC_UPDATE)
 *   BKP1R - "rollback to previous (secondary) app" flag (value = BOOT_MAGIC_ROLLBACK_PREV)
 *   BKP2R - "rollback to factory app" flag (value = BOOT_MAGIC_ROLLBACK_FACTORY)
 *
 * Both the bootloader and the main application must include this file to
 * agree on register indices and magic values.
 */

#define BOOT_BACKUP_REG_UPDATE            0U
#define BOOT_BACKUP_REG_ROLLBACK_PREV      1U
#define BOOT_BACKUP_REG_ROLLBACK_FACTORY   2U

/* Must be called once before any read/write to a backup register.
 * Enables the RTC/TAMP APB3 clock and backup domain write access.
 * Safe to call multiple times. */
void boot_backup_reg_init(void);

/* Read the raw value of backup register `index` (0..31, see indices above). */
uint32_t boot_backup_reg_read(uint32_t index);

/* Write `value` into backup register `index`. */
void boot_backup_reg_write(uint32_t index, uint32_t value);

/* Convenience: write 0 into backup register `index`. Used to consume a
 * one-shot flag immediately after reading it, so an unrelated future
 * reset does not re-trigger the same command. */
void boot_backup_reg_clear(uint32_t index);

#endif