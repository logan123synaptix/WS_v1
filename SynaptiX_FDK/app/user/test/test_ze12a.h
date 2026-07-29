#ifndef TEST_SPS30_H
#define TEST_SPS30_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

void test_ze12a_init(void);
void test_ze12a_poll(uint32_t delta_ms);

#ifdef __cplusplus
}
#endif

#endif