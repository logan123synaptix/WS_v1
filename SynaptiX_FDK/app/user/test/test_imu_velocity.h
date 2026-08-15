#ifndef TEST_IMU_VELOCITY_H
#define TEST_IMU_VELOCITY_H

#ifdef __cplusplus
extern "C" {
#endif

/* Standalone bring-up test for imu_velocity.c/.h (see that header's
 * top comment for the full 3-stage design). Exercises exactly the
 * stages that are actually verifiable without a moving vehicle:
 *
 *   Stage A  (bias)            -- fully testable stationary
 *   Stage A2 (temp-comp bias)  -- testable stationary, but needs the
 *                                  board to sit through two
 *                                  sufficiently different temperatures
 *                                  (e.g. right after power-on vs. after
 *                                  running warm for a while) before
 *                                  bias_slope_calibrated goes true
 *   Stage B  (down axis only)  -- testable stationary via
 *                                  bno055_get_gravity()
 *
 * Stage B's forward axis and Stage C's scale factor CANNOT be
 * validated here -- both require real GPS-referenced vehicle motion
 * (see imu_velocity.h's top comment). This test logs velocity_kph
 * from imu_velocity_poll() for visibility, but with no vehicle motion
 * to integrate against, expect it to sit near 0 the whole time this
 * test runs stationary -- that is the CORRECT behavior once Stage A
 * is calibrated, not evidence Stage C works.
 *
 * Call test_imu_velocity_init() once after sx_board_init() (same
 * point test_imu_init() is called from, Core/Src/main.c), then
 * test_imu_velocity_poll(delta_ms) every tick in the main while(1)
 * loop -- same calling convention as the other test_*_poll()
 * functions. Can run alongside test_imu.c's own test in the same
 * build; they read the same board.imu but do not share state. */

#include <stdint.h>

void test_imu_velocity_init(void);
void test_imu_velocity_poll(uint32_t delta_ms);

#ifdef __cplusplus
}
#endif

#endif