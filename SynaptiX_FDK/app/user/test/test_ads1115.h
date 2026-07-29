#ifndef TEST_ADS1115_H
#define TEST_ADS1115_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* Standalone bring-up test for ADS1115 (I2C1, shared bus), going
 * through the real app-layer wrapper (power_monitor_app.c/.h), same
 * pattern as test_sht3x.c / test_rtc.c: reuse the real conversion
 * math, don't re-implement the mV->A / mV->V formulas here.
 *
 * board.ads1115 is already ADS1115_Init()'d inside sx_board_init()
 * (sx_board.c, PGA=ADS1115_PGA_TWO, data rate=250SPS) — this test does
 * not call ADS1115_Init() again.
 *
 * R16 shunt value note: as of this test, POWER_MONITOR_APP_SHUNT_OHM
 * (power_monitor_app.h) has been updated to 0.02 ohm per user
 * confirmation (board revision change from an earlier 0.1 ohm value).
 * If AIN1/current readings look implausible, check that constant first
 * before suspecting the ADC or wiring.
 *
 * power_monitor_app_poll() alternates AIN1 (current)/AIN2 (voltage)
 * one channel per POWER_MONITOR_APP_SAMPLE_PERIOD_MS tick (see that
 * module's doc-comment for why) — this test just drives that cadence
 * and logs both channels' latest values once both have been read at
 * least once.
 *
 * Call test_ads1115_init() once after sx_board_init() (Core/Src/main.c),
 * then test_ads1115_poll(delta_ms) every tick in the main while(1)
 * loop — same calling convention as the other test_*_poll() functions. */

void test_ads1115_init(void);
void test_ads1115_poll(uint32_t delta_ms);

#ifdef __cplusplus
}
#endif

#endif