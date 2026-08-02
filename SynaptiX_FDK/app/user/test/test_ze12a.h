#ifndef TEST_ZE12A_H
#define TEST_ZE12A_H

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