#ifndef SX_TEMP_HUMI_H
#define SX_TEMP_HUMI_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "sht3x.h"

/* App-layer periodic reader for SHT3x, built on top of the single-shot
 * measurement API in sht3x.c/.h (sht3x_measure_single_shot() +
 * sht3x_read_measurement()) rather than SHT3x's periodic-measurement
 * mode.
 *
 * Why single-shot instead of periodic mode: per Sensirion/RIOT-OS
 * driver notes, single-shot lets the sensor drop back into its
 * low-power idle state between measurements, whereas periodic mode
 * keeps it continuously measuring. Since this app only needs one
 * reading every few seconds (SX_TEMP_HUMI_SAMPLE_PERIOD_MS), single-
 * shot is strictly more power-efficient here — and it means there is
 * no "stop periodic measurement" step needed before sleep (unlike
 * SPS30's fan/laser, which do need an explicit stop+sleep sequence):
 * as long as this module only ever calls _single_shot() (never starts
 * periodic mode), SHT3x is already idle between calls, with nothing
 * extra to quiesce before STOP mode. See sx_sleep_manager for how
 * sleep-side steps are registered — SHT3x deliberately does not need
 * one.
 *
 * Non-blocking by design (matches this project's non-RTOS, poll-driven
 * model): sht3x_measure_single_shot() only sends the command byte;
 * the sensor needs up to ~15.5ms (high repeatability, per datasheet)
 * before the result can be read. Rather than blocking/delaying for
 * that window, this module tracks elapsed time itself and only calls
 * sht3x_read_measurement() once enough time has passed. */

#define SX_TEMP_HUMI_MEASURE_CMD           SHT3X_CMD_MEASURE_HIGHREP_STRETCH
/* Measurement duration budget: datasheet gives 15.5ms max for high
 * repeatability; SHT3X_TIMEOUT_MS (50ms, sht3x.h) is this driver's I2C
 * transaction timeout, not a measurement-duration bound, so it isn't
 * reused here. 20ms gives a small margin over the datasheet max. */
#define SX_TEMP_HUMI_MEASURE_WAIT_MS       20U
/* How often to take a new reading. Independent of the wake/sleep cycle
 * — this just governs how fresh sx_temp_humi_get_temperature()/
 * _get_humidity() are while the app is awake. */
#define SX_TEMP_HUMI_SAMPLE_PERIOD_MS      5000U

typedef enum {
    SX_TEMP_HUMI_STATE_IDLE = 0,     /* waiting for the next sample period */
    SX_TEMP_HUMI_STATE_MEASURING,    /* command sent, waiting out the measurement duration */
} sx_temp_humi_state_t;

typedef struct {
    SHT3X_T                *sht3x;
    sx_temp_humi_state_t    state;
    uint32_t                period_elapsed_ms;   /* time since the last sample was kicked off */
    uint32_t                measure_elapsed_ms;  /* time since the command was sent, while MEASURING */
    bool                    has_reading;          /* true once at least one successful read has completed */
    SHT3X_STATUS_T          last_status;          /* result of the most recent read attempt, for diagnostics */
} sx_temp_humi_t;

/* sht3x must already be sht3x_init()'d (see sx_board.c) — this module
 * only drives the periodic single-shot cadence on top of it, it does
 * not own I2C setup. */
void sx_temp_humi_init(sx_temp_humi_t *th, SHT3X_T *sht3x);

/* Call once per app tick with the elapsed time since the last call.
 * Non-blocking: kicks off a new measurement every
 * SX_TEMP_HUMI_SAMPLE_PERIOD_MS, reads the result
 * SX_TEMP_HUMI_MEASURE_WAIT_MS later. */
void sx_temp_humi_poll(sx_temp_humi_t *th, uint32_t delta_ms);

/* True once at least one successful measurement has been read — i.e.
 * get_temperature()/get_humidity() return real data rather than the
 * SHT3X_T struct's zero-initialized default. */
bool sx_temp_humi_is_ready(sx_temp_humi_t *th);

float sx_temp_humi_get_temperature(sx_temp_humi_t *th);
float sx_temp_humi_get_humidity(sx_temp_humi_t *th);

#ifdef __cplusplus
}
#endif

#endif