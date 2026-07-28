#ifndef TEST_SHT3X_H
#define TEST_SHT3X_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* Standalone bring-up test for SHT3x (temperature/humidity, I2C1),
 * going through the real driver + app-wrapper layers (sht3x.c/.h +
 * sx_temp_humi.c/.h), same pattern as test_lte_mqtt.c: reuse the real
 * stack, don't re-implement I2C access here.
 *
 * board.sht3x is already sht3x_init()'d inside sx_board_init()
 * (sx_board.c) — this test only drives the periodic single-shot
 * cadence on top of it via sx_temp_humi_poll(), it does not touch I2C
 * directly and does not call sht3x_init() again.
 *
 * Call test_sht3x_init() once after sx_board_init() (Core/Src/main.c),
 * then test_sht3x_poll(delta_ms) every tick in the main while(1) loop —
 * same calling convention as test_lte_mqtt_poll(). */

void test_sht3x_init(void);
void test_sht3x_poll(uint32_t delta_ms);

#ifdef __cplusplus
}
#endif

#endif