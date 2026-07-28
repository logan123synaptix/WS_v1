#ifndef TEST_IMU_H
#define TEST_IMU_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

void test_imu_init(void);
void test_imu_poll(uint32_t delta_ms);

#ifdef __cplusplus
}
#endif

#endif