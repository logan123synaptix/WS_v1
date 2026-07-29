#ifndef TEST_RTC_H
#define TEST_RTC_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* Standalone bring-up test for RTC RX8130CE (I2C1), going through the
 * real driver (sx_ex_rtc.c/.h), same pattern as test_sht3x.c: reuse the
 * real stack, don't re-implement I2C access here.
 *
 * board.rtc is already rx8130ce_init()'d inside sx_board_init()
 * (sx_board.c) — this test does not call rx8130ce_init() again, it only
 * reads back time + the VLF (valid) flag periodically, and can
 * optionally set the time once at boot (see TEST_RTC_SET_TIME_ON_INIT
 * in test_rtc.c) so you have something non-zero to read back.
 *
 * Note: app/user/time_synch/time_sync.c also calls rx8130ce_set_time()
 * from modem NITZ / GPS — but only once modem or GPS is actually
 * running and time-synced. This standalone test does not init modem or
 * GPS, so there is no writer racing with this test's reads.
 *
 * Call test_rtc_init() once after sx_board_init() (Core/Src/main.c),
 * then test_rtc_poll(delta_ms) every tick in the main while(1) loop —
 * same calling convention as test_sht3x_poll(). */

void test_rtc_init(void);
void test_rtc_poll(uint32_t delta_ms);

#ifdef __cplusplus
}
#endif

#endif