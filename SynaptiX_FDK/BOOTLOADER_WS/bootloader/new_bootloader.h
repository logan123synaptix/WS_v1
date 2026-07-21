#ifndef NEW_BOOTLOADER_H
#define NEW_BOOTLOADER_H

#include "bootloader.h"
#include <stdbool.h>

/*
 * Set true by new_bootloader_check_commands() when BKP2R holds
 * BOOT_MAGIC_UPDATE_FACTORY (flash-factory command) instead of
 * BOOT_MAGIC_ROLLBACK_FACTORY (rollback-factory command). Read by
 * tud_dfu_download_cb()/tud_dfu_manifest_cb() in main.c to redirect the
 * DFU write target from Secondary to Factory for this DFU session only.
 * False (default) means DFU targets Secondary, as before this feature.
 */
extern bool g_dfu_target_factory;

/*
 * Checks the three one-shot command backup registers (see
 * new_boot_backup_reg.h) written by the main application before a software
 * reset, and acts on whichever one is set:
 *
 *   BKP2R - two possible magic values, checked in this order:
 *     BOOT_MAGIC_UPDATE_FACTORY (flash-factory) - highest priority. Enters
 *       DFU-wait mode with the write target redirected to Factory (see
 *       g_dfu_target_factory); does NOT jump to application.
 *     BOOT_MAGIC_ROLLBACK_FACTORY (rollback-factory) - copies Factory app
 *       to Primary (one-way, via Scratch) then jumps to Primary.
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