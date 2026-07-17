#ifndef NEW_BOOTLOADER_H
#define NEW_BOOTLOADER_H

#include "bootloader.h"

/*
 * Checks the three one-shot command backup registers (see
 * new_boot_backup_reg.h) written by the main application before a software
 * reset, and acts on whichever one is set:
 *
 *   BKP2R (rollback-factory) - highest priority. Copies Factory app to
 *     Primary (one-way, via Scratch) then jumps to Primary.
 *   BKP1R (rollback-previous) - swaps Primary <-> Secondary (existing
 *     boot_swap_firmware()) then jumps to Primary.
 *   BKP0R (update) - only sets isUpgradeInProgress + saves partition; does
 *     NOT jump to application. Lets the caller's normal DFU-wait loop in
 *     main.c take over instead.
 *
 * Priority order (factory > previous > update) only matters in the unlikely
 * case more than one flag is set at once; normal use sets exactly one flag
 * before reset. Each flag is cleared as soon as it is read, before the
 * corresponding action runs, so a crash/reset mid-action cannot re-trigger
 * the same command forever.
 *
 * If no flag is set, this function returns immediately and does nothing -
 * the caller's existing boot flow (bootloader_init()'s own new-firmware /
 * upgrade-in-progress checks) continues exactly as before this feature was
 * added.
 *
 * Must be called after bootloader->boot_flash.partition has been loaded and
 * validated (i.e. after the magic-number check in bootloader_init()), since
 * the rollback actions read/write partition fields.
 */
void new_bootloader_check_commands(Bootloader_t *bootloader);

#endif // NEW_BOOTLOADER_H