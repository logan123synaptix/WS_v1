#include "calib_imu.h"
#include "sx_ex_storage.h"
#include "logger.h"
#include <string.h>

static const char *TAG = "CALIB_IMU";

/* Minimum calib_stat level (0-3 scale) required before this module
 * trusts the chip's current calib_data enough to save it. Deliberately
 * the max (3) for BOTH sys and accel here -- unlike
 * test_imu_velocity.c's own CALIB_MIN_SYS/CALIB_MIN_ACCEL gate (which
 * uses a low threshold of 1 so imu_velocity's Stage A can start
 * sampling reasonably early), this module only writes to flash ONCE
 * per boot and that write is meant to be trustworthy for every future
 * boot until the board is remounted -- so it is worth waiting for full
 * convergence here even if that means this module's own save doesn't
 * happen for several minutes (or a full drive) after boot. A
 * lower-quality save would persist a mediocre calibration indefinitely
 * across reboots, which is worse than just letting the chip re-settle
 * from scratch each time. */
#define CALIB_SAVE_MIN_SYS     3U
#define CALIB_SAVE_MIN_ACCEL   3U

/* calib_stat is a single 1-byte I2C register read (REG_CALIB_STAT,
 * 0x35, see bno055.c) -- cheap enough to poll every call rather than
 * gate it behind its own timer, unlike calib_data (22 bytes, and
 * requires the mode-switch-to-CONFIGMODE dance bno055_get_calib_data()
 * does internally) or a flash write, both of which only ever happen
 * the one time this module actually saves. Caller (test_imu_velocity.c
 * for now) still decides its own overall poll cadence; this module
 * does not impose one. */

static bool s_loaded_from_flash = false;
static bool s_saved_this_boot   = false;

void calib_imu_init(bno055_t *dev)
{
    s_loaded_from_flash = false;
    s_saved_this_boot   = false;

    int32_t size = sx_storage_size(FILE_SAVE_IMU_CALIBRATION);
    if (size <= 0) {
        log_info(TAG, "No saved BNO055 calib file - chip will self-calibrate "
                       "from scratch this boot");
        return;
    }

    bno055_calib_data_t saved;
    if ((uint32_t)size != sizeof(saved)) {
        /* Stale/foreign file (e.g. from an older struct layout) --
         * refuse to reinterpret bytes as today's struct rather than
         * risk feeding the chip garbage offsets. Same "don't guess"
         * posture as the rest of this codebase's flash-loading paths. */
        log_warn(TAG, "Saved BNO055 calib file size mismatch (got %ld, "
                       "expected %u) - ignoring, chip will self-calibrate",
                  (long)size, (unsigned)sizeof(saved));
        return;
    }

    sx_storage_err_t err = sx_storage_read(FILE_SAVE_IMU_CALIBRATION, &saved, sizeof(saved));
    if (err != SX_STORAGE_OK) {
        log_warn(TAG, "Failed to read saved BNO055 calib file (err=%d) - "
                       "chip will self-calibrate", err);
        return;
    }

    if (bno055_set_calib_data(dev, &saved) != BNO055_OK) {
        log_warn(TAG, "bno055_set_calib_data() failed while restoring "
                       "saved calib - chip will self-calibrate");
        return;
    }

    s_loaded_from_flash = true;
    /* Do NOT set s_saved_this_boot here -- restoring a file is not the
     * same event as this boot having produced a fresh save. If the
     * board gets remounted and the chip's fusion re-converges to a
     * DIFFERENT calib_data than what was just restored, calib_imu_poll()
     * below should still be free to overwrite the file with the new,
     * better-matching values. */
    log_info(TAG, "Restored BNO055 calibration from flash "
                   "(acc=%d,%d,%d mag=%d,%d,%d gyr=%d,%d,%d acc_r=%d mag_r=%d)",
              saved.acc_x, saved.acc_y, saved.acc_z,
              saved.mag_x, saved.mag_y, saved.mag_z,
              saved.gyr_x, saved.gyr_y, saved.gyr_z,
              saved.acc_radius, saved.mag_radius);
}

void calib_imu_poll(bno055_t *dev)
{
    if (s_saved_this_boot) return;

    bno055_calib_stat_t stat;
    if (bno055_get_calib_stat(dev, &stat) != BNO055_OK) {
        /* Transient I2C hiccup -- try again next poll, nothing to log
         * here every call or this would spam at whatever cadence the
         * caller polls at. */
        return;
    }

    if (stat.sys < CALIB_SAVE_MIN_SYS || stat.accel < CALIB_SAVE_MIN_ACCEL) {
        return;
    }

    bno055_calib_data_t data;
    if (bno055_get_calib_data(dev, &data) != BNO055_OK) {
        log_warn(TAG, "calib_stat reached save threshold (sys=%u accel=%u) "
                       "but bno055_get_calib_data() failed - will retry "
                       "next poll", stat.sys, stat.accel);
        return;
    }

    sx_storage_err_t err = sx_storage_write(FILE_SAVE_IMU_CALIBRATION, &data, sizeof(data));
    if (err != SX_STORAGE_OK) {
        log_error(TAG, "Failed to save BNO055 calibration to flash (err=%d) "
                        "- will retry next poll", err);
        return;
    }

    s_saved_this_boot = true;
    log_info(TAG, "Saved BNO055 calibration to flash (sys=%u accel=%u gyro=%u mag=%u) "
                   "(acc=%d,%d,%d mag=%d,%d,%d gyr=%d,%d,%d acc_r=%d mag_r=%d)",
              stat.sys, stat.accel, stat.gyro, stat.mag,
              data.acc_x, data.acc_y, data.acc_z,
              data.mag_x, data.mag_y, data.mag_z,
              data.gyr_x, data.gyr_y, data.gyr_z,
              data.acc_radius, data.mag_radius);
}

bool calib_imu_is_loaded(void)
{
    return s_loaded_from_flash;
}