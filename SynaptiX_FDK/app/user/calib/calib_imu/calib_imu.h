#ifndef __CALIB_IMU_H
#define __CALIB_IMU_H

#ifdef __cplusplus
extern "C" {
#endif

#include "app_config.h"
#include <stdint.h>
#include <stdbool.h>
#include "bno055.h"

/* Persists the BNO055's OWN internal sensor-fusion calibration
 * (bno055_calib_data_t: accel/mag/gyro offsets + accel/mag radius) to
 * flash, at FILE_SAVE_IMU_CALIBRATION (app_config.h).
 *
 * This is a DIFFERENT calibration than imu_velocity.h's Stage A/A2/A3
 * bias — that module's bias corrects for imu_velocity's own integration
 * math on top of whatever the chip hands back. This module instead
 * persists the chip's onboard fusion algorithm's own settling-in state
 * (the same offsets bno055_get_calib_stat()'s sys/gyro/accel/mag scores
 * describe the convergence of), so that settling-in does not have to
 * happen from scratch (which can take several minutes of the board
 * being moved through multiple orientations, per Bosch's calibration
 * guidance) every single boot.
 *
 * imu_velocity's Stage A/B DEPENDS on this chip-level calibration being
 * reasonably converged -- bno055_get_linear_accel() and
 * bno055_get_gravity() are themselves outputs of the chip's fusion
 * algorithm, so a poorly-calibrated chip means noisy/wrong input to
 * imu_velocity's own bias learning, silently. See
 * test_imu_velocity.c's CALIB_MIN_SYS/CALIB_MIN_ACCEL gate for the
 * runtime safety net this module is meant to make less necessary (by
 * getting the chip calibrated fast via a flash-restored offset set)
 * but does not replace (a restored offset set can still go stale if
 * the board is later remounted differently, so that gate is kept
 * regardless of whether this module found a saved file).
 *
 * Usage:
 *   calib_imu_init(dev)      -- call once after bno055_init(dev), before
 *                                relying on any bno055_get_gravity() /
 *                                bno055_get_linear_accel() reading. If a
 *                                saved file exists, loads it and pushes
 *                                it into the chip immediately via
 *                                bno055_set_calib_data() -- the chip is
 *                                then already calibrated, no waiting.
 *                                If no file exists (first boot on this
 *                                board, or file unreadable), does
 *                                nothing further here; the chip starts
 *                                uncalibrated and calib_imu_poll() is
 *                                responsible for catching it once it
 *                                settles on its own.
 *   calib_imu_poll(dev)      -- call periodically (see
 *                                CALIB_IMU_POLL_PERIOD_MS) from the main
 *                                loop / test poll. Cheap no-op once
 *                                already saved this boot -- reads
 *                                calib_stat every call (see this
 *                                function's own doc-comment in
 *                                calib_imu.c for why that read itself is
 *                                considered cheap enough not to gate
 *                                further), but only reads calib_data
 *                                and writes to flash the ONE time per
 *                                boot the chip first reaches
 *                                CALIB_SAVE_MIN_SYS/CALIB_SAVE_MIN_ACCEL
 *                                and no file was loaded at init (or the
 *                                loaded file's data no longer matches
 *                                what the chip has now converged to --
 *                                see calib_imu.c).
 *   calib_imu_is_loaded(void) -- true if calib_imu_init() successfully
 *                                restored a saved file this boot. Purely
 *                                informational (e.g. for status logging
 *                                in test_imu_velocity.c) -- nothing in
 *                                this module's own behavior branches on
 *                                the caller checking this. */

void calib_imu_init(bno055_t *dev);
void calib_imu_poll(bno055_t *dev);
bool calib_imu_is_loaded(void);

#ifdef __cplusplus
}
#endif

#endif