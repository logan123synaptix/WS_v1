#ifndef POWER_MONITOR_APP_H
#define POWER_MONITOR_APP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "ads1115.h"

/* App-layer periodic reader for ADS1115, wrapping the two channels wired
 * on this board (see the doc-comment on board.ads1115 in sx_board.h).
 *
 * IMPORTANT UNIT NOTE: ADS1115_readSingleEnded() returns millivolts, not
 * volts — its voltCoef is a mV/LSB figure (see ads1115.h's PGA comments,
 * e.g. "0.0625 mV by one bit" for PGA_TWO, confirmed against the
 * 2.048V-FSR / 32768-count math). This module converts to volts (/1000)
 * before applying either formula below.
 *
 *   AIN1 = adc_current — voltage across shunt resistor R16 (confirmed by
 *     the user as 0.1 ohm), amplified by INA180A1 (gain 20V/V):
 *       Vout_AIN1 [V] = 20 * I * R16 = 20 * 0.1 * I = 2 * I
 *       => I = Vout_AIN1 / 2.0f                      (amps)
 *
 *   AIN2 = adc_vol — 12V rail through a resistor divider (R14=100k top /
 *     R15=20k bottom, confirmed against schematic):
 *       Vout_AIN2 [V] = Vin * 20k/(100k+20k) = Vin / 6
 *       => Vin = Vout_AIN2 * 6.0f                    (volts, rail voltage)
 *
 * Both conversion factors are only valid for THIS board's confirmed
 * component values (R16=0.1R, R14/R15=100k/20k, INA180A1 gain=20V/V) —
 * do not reuse get_rail_current_a()/get_rail_voltage_v() as-is on a
 * different board revision without re-deriving these constants.
 *
 * Non-blocking by design (matches this project's poll-driven model),
 * despite ADS1115_readSingleEnded() itself being a blocking call (issues
 * the config write, then polls the OS bit in a tight loop internally —
 * see ads1115.c). Per discussion with the user, rather than reading both
 * channels back-to-back in one poll() call (up to ~2x the per-read
 * blocking cost in the same tick), this module reads exactly ONE channel
 * per POWER_MONITOR_APP_SAMPLE_PERIOD_MS tick and alternates AIN1/AIN2
 * on successive samples — so any single app_process() tick never blocks
 * on more than one ADS1115 transaction. Each channel's last-known value
 * is retained independently, so get_rail_current_a()/get_rail_voltage_v()
 * stay valid using the most recent reading of that channel even while
 * the other channel is the one currently being sampled. */

#define POWER_MONITOR_APP_SAMPLE_PERIOD_MS   1000U   /* time between successive channel reads (not full AIN1+AIN2 cycles) */

/* R16 shunt resistor (ohms) — confirmed by the user. INA180A1 gain
 * (V/V) — per TI's Device Comparison table (SOT23-5, pinout A), see
 * sx_board.h's doc-comment. */
#define POWER_MONITOR_APP_INA180_GAIN         20.0f
#define POWER_MONITOR_APP_SHUNT_OHM           0.1f

/* R14/R15 rail-voltage divider (ohms) — confirmed against schematic, see
 * sx_board.h's doc-comment. */
#define POWER_MONITOR_APP_DIVIDER_R14_OHM     100000.0f
#define POWER_MONITOR_APP_DIVIDER_R15_OHM     20000.0f

typedef enum {
    POWER_MONITOR_APP_CH_CURRENT = 0,   /* AIN1 */
    POWER_MONITOR_APP_CH_VOLTAGE = 1,   /* AIN2 */
} power_monitor_app_channel_t;

typedef struct {
    ADS1115_T                   *ads1115;
    uint32_t                     period_elapsed_ms;
    power_monitor_app_channel_t  next_channel;   /* which channel the next sample reads */

    float   last_current_a;     /* most recent AIN1 reading, converted to amps */
    float   last_rail_voltage_v; /* most recent AIN2 reading, converted to rail volts */
    bool    has_current_reading;
    bool    has_voltage_reading;

    /* Diagnostics: set on the most recent read attempt for whichever
     * channel was sampled last (ADS1115_OK / ADS1115_ERR / ADS1115_ERR_TIMEOUT). */
    ADS1115_STATUS_T last_status;
} power_monitor_app_t;

/* ads1115 must already be ADS1115_Init()'d (see sx_board.c) — this module
 * only drives the periodic alternating-channel read cadence on top of
 * it, it does not own I2C setup. */
void power_monitor_app_init(power_monitor_app_t *app, ADS1115_T *ads1115);

/* Call once per app tick with the elapsed time since the last call.
 * Every POWER_MONITOR_APP_SAMPLE_PERIOD_MS, reads exactly one channel
 * (alternating AIN1/AIN2 each time) and updates that channel's last-known
 * converted value. See the module doc-comment above for why only one
 * channel is read per tick. */
void power_monitor_app_poll(power_monitor_app_t *app, uint32_t delta_ms);

bool power_monitor_app_has_current_reading(power_monitor_app_t *app);
bool power_monitor_app_has_voltage_reading(power_monitor_app_t *app);

/* Rail current in amps (AIN1, via INA180A1/R16 — see conversion above).
 * Value is UNVERIFIED against a real load until the R16=0.1R value used
 * here has been checked on real hardware — treat with appropriate
 * caution for anything safety-critical (e.g. overcurrent cutoff logic). */
float power_monitor_app_get_current_a(power_monitor_app_t *app);

/* Rail voltage in volts (AIN2, via R14/R15 divider — see conversion
 * above), i.e. the actual +12V-nominal rail voltage, not the raw ADC
 * input voltage. */
float power_monitor_app_get_rail_voltage_v(power_monitor_app_t *app);

#ifdef __cplusplus
}
#endif

#endif