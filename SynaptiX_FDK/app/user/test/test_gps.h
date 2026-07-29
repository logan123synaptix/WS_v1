#ifndef TEST_GPS_H
#define TEST_GPS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* Standalone bring-up test for GPS GP02 (UART2, 9600 baud), going
 * through the real driver (gps.c/.h + minmea parser), same pattern as
 * test_sht3x.c / test_rtc.c: reuse the real stack, don't re-implement
 * NMEA parsing here.
 *
 * board.gps is already gps_init()'d inside sx_board_init() (sx_board.c)
 * — this test does not call gps_init() again. gps_init() already
 * drives N/F and RST high (module powered, out of reset) as part of
 * init, so no explicit gps_power_on()/gps_reset() call is needed here
 * for a fresh boot.
 *
 * gps_process() already logs every successfully parsed RMC/GGA sentence
 * itself (see gps.c's gps_callback_task(), log_info calls) — this test
 * does not duplicate that logging, it only adds a periodic "still
 * waiting for fix" heartbeat so you know the loop is alive even before
 * the first valid RMC/GGA sentence arrives (cold start under open sky
 * can take 30s to a few minutes).
 *
 * Call test_gps_init() once after sx_board_init() (Core/Src/main.c),
 * then test_gps_poll(delta_ms) every tick in the main while(1) loop —
 * same calling convention as test_sht3x_poll() / test_rtc_poll(). */

void test_gps_init(void);
void test_gps_poll(uint32_t delta_ms);

#ifdef __cplusplus
}
#endif

#endif