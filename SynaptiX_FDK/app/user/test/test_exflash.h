#ifndef TEST_EXFLASH_H
#define TEST_EXFLASH_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

void test_exflash_init(void);
void test_exflash_poll(uint32_t delta_ms);

#ifdef __cplusplus
}
#endif

#endif