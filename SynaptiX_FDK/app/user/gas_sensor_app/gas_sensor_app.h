#ifndef GAS_SENSOR_APP_H
#define GAS_SENSOR_APP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "ze12a.h"

/* App-layer wrapper for ZE12A (gas_sensor_app / ze12a.c). Deliberately
 * thin compared to sps30_app.c/sx_temp_humi.c: ze12a.c already owns its
 * entire runtime lifecycle by itself —
 *
 *   - gas_sensor_poll(delta_ms) drives the mux round-robin, byte-assembly
 *     state machine, and per-gas-type connection timeout internally
 *     (static s_mux_channel/s_rx_state/etc in ze12a.c).
 *   - The GAS_SENSOR_COUNT=5 result slots (CO/SO2/NO2/O3/H2S) live in the
 *     module-global gas_sensor[] array (ze12a.h), populated directly by
 *     ze12a.c's own frame parser — there is no separate "app state
 *     machine" to run on top of it, unlike SPS30 (which needs POWER_ON/
 *     WAKE_UP/... sequencing) or SHT3x (which needs single-shot command/
 *     wait/read sequencing).
 *
 * This module's job is just:
 *   1. Call gas_sensor_poll() once per app tick — the one thing app.c
 *      needs to remember to do (mirrors sps30_app_poll()/
 *      sx_temp_humi_poll()'s call-once-per-tick contract).
 *   2. Provide a lookup-by-type convenience API (gas_sensor_app_get_*())
 *      instead of every caller re-implementing the linear search over
 *      gas_sensor[] that ze12a_handle_frame() already does internally.
 *
 * No state of its own is needed (no struct instance to pass around) —
 * unlike sps30_app_t/sx_temp_humi_t, all runtime state already lives in
 * ze12a.c's statics and the gas_sensor[] array it owns. Sleep/wake
 * handling (Question & Answer vs Active Upload mode) is driven directly
 * from sx_sleep_manager's sleep_steps/wake_steps via
 * gas_sensor_switch_to_qa_mode()/gas_sensor_switch_to_active_mode() —
 * see sx_sleep_manager.c — not through this module. */

/* Call once per app tick with the elapsed time since the last call.
 * Thin pass-through to gas_sensor_poll() — kept as a separate entry
 * point (rather than having app.c call gas_sensor_poll() directly) so
 * app.c only ever depends on "*_app" headers for sensor polling, the
 * same pattern as sps30_app_poll()/sx_temp_humi_poll(). */
void gas_sensor_app_poll(uint32_t delta_ms);

/* True once at least one valid frame for this gas type has been seen
 * and its GAS_SENSOR_TIMEOUT_MS window hasn't lapsed yet — i.e.
 * gas_sensor_app_get_value() returns a live reading, not stale/zeroed
 * data left over from before the module went out of range or was
 * disconnected. Mirrors gas_sensor[i].isConnected. */
bool gas_sensor_app_is_connected(GasSensorType_t type);

/* Raw sensor value for the given gas type, in the sensor's own unit
 * (see gas_sensor[i].unitPPB for the unit code that came with the last
 * valid frame — ppm vs ppb, per the ZE12A datasheet's Gas unit byte;
 * this module does not attempt any unit conversion). Returns 0 if the
 * type has never been seen or gas_sensor_app_is_connected() is false —
 * callers should check is_connected() first rather than trusting a
 * bare 0 to mean "no gas detected". */
uint16_t gas_sensor_app_get_value(GasSensorType_t type);

/* Unit code from the most recent valid frame for this gas type (raw
 * byte, see gas_sensor[i].unitPPB / the ZE12A datasheet's Gas unit
 * table) — 0 if never seen. */
uint8_t gas_sensor_app_get_unit(GasSensorType_t type);

#ifdef __cplusplus
}
#endif

#endif