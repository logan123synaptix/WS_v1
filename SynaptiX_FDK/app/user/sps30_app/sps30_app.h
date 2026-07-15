#ifndef SPS30_APP_H
#define SPS30_APP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "sx_gpio.h"

/* App-layer state machine for SPS30, built on the SHDLC command API in
 * sps30_uart.c/.h. Ported from WS_v0's SynaptiX/apps/sps30/sps30_app.c
 * with two deliberate changes, both per conversation with the user:
 *
 *  1. Dropped the mc_pm10p0==mc_pm2p5 "invalid data" heuristic from the
 *     old STOP_MEASUREMENT state. Per the SPS30 datasheet
 *     (Documents/SPS30_dust_sensor (1).md), equal PM10/PM2.5 values are
 *     not a documented error condition — physically it just means clean
 *     air with no particles above 2.5um. Real error detection is
 *     sps30_read_device_status_register() (SPEED/LASER/FAN bits), not
 *     implemented here yet — see sps30_app_read_device_status()'s TODO.
 *
 *  2. Added POWER_ON/POWER_OFF states wrapping the original wake->
 *     measure->stop sequence, since WS_v1's board has an EN_PW_DUST
 *     power-enable line (opto+MOSFET, active-HIGH — see sx_board.h)
 *     that WS_v0's board did not. SPS30 is fully unpowered between
 *     measurement cycles, not just put through its own SHDLC sleep
 *     command — sps30_app_sleep_step_*() below drives both (SHDLC
 *     sleep() first per the datasheet's recommended shutdown sequence,
 *     then the power-enable line), and is what sx_sleep_manager's
 *     sleep_steps array calls to fully power SPS30 down before STOP
 *     mode. */

typedef enum {
    SPS30_APP_STATE_IDLE = 0,
    SPS30_APP_STATE_POWER_ON,          /* drive EN_PW_DUST high, wait for the sensor to be ready to talk */
    SPS30_APP_STATE_WAKE_UP,           /* sps30_wake_up_sequence() */
    SPS30_APP_STATE_START_MEASUREMENT,
    SPS30_APP_STATE_WAIT_MEASUREMENT,  /* let the fan/laser stabilize before reading (datasheet: ~a few seconds) */
    SPS30_APP_STATE_READ_MEASUREMENT,
    SPS30_APP_STATE_STOP_MEASUREMENT,
    SPS30_APP_STATE_SLEEP,             /* sps30_sleep() (SHDLC), then EN_PW_DUST low */
    SPS30_APP_STATE_DONE,
} sps30_app_state_t;

typedef struct {
    float mc_1p0, mc_2p5, mc_4p0, mc_10p0;               /* mass concentration, ug/m3 */
    float nc_0p5, nc_1p0, nc_2p5, nc_4p0, nc_10p0;       /* number concentration, #/cm3 */
    float typical_particle_size;                          /* um */
} sps30_app_measurement_t;

typedef struct {
    sx_gpio_t                *power_gpio;   /* EN_PW_DUST, from sx_board_get_sps30_power_gpio() */
    sps30_app_state_t         state;
    uint32_t                  state_elapsed_ms;
    sps30_app_measurement_t   last_measurement;
    bool                      has_measurement;   /* true once at least one READ_MEASUREMENT has succeeded */
} sps30_app_t;

/* power_gpio must already be sx_gpio_init()'d (done in sx_board_init())
 * — this module only drives it high/low, it does not own GPIO setup. */
void sps30_app_init(sps30_app_t *app, sx_gpio_t *power_gpio);

/* Kicks off one full measurement cycle: power on -> wake -> start ->
 * wait -> read -> stop. No-op if a cycle is already in progress (state
 * != IDLE and != DONE). Does NOT power off afterwards — call
 * sps30_app_sleep_*() explicitly when done, e.g. from app.c's SENDING
 * state after telemetry has been published, or from sx_sleep_manager's
 * sleep_steps before entering STOP mode. */
void sps30_app_start_cycle(sps30_app_t *app);

/* Call once per app tick with the elapsed time since the last call.
 * Advances the state machine; no-op once state == DONE (check via
 * sps30_app_is_cycle_done()) or == IDLE (nothing started yet). */
void sps30_app_poll(sps30_app_t *app, uint32_t delta_ms);

bool sps30_app_is_cycle_done(sps30_app_t *app);

/* True once at least one measurement cycle has completed successfully
 * — i.e. sps30_app_get_measurement() returns real data. */
bool sps30_app_has_measurement(sps30_app_t *app);

const sps30_app_measurement_t *sps30_app_get_measurement(sps30_app_t *app);

/* Resets state back to IDLE, ready for the next sps30_app_start_cycle().
 * Call after consuming DONE's result (e.g. after publishing it). */
void sps30_app_reset(sps30_app_t *app);

/* ===== sleep_step-compatible power-down (see sx_sleep_step_t in
 * sx_sleep_service.h) — non-blocking start()/is_done() pair intended
 * to be registered directly into sx_sleep_manager's sleep_steps array,
 * NOT meant to be called mid-measurement-cycle. Caller (sx_sleep_manager
 * or app.c) is responsible for only invoking this once a measurement
 * cycle is DONE/IDLE, not while POWER_ON..STOP_MEASUREMENT are active
 * — cutting power mid-measurement would leave SPS30's fan/laser
 * abruptly stopped rather than shut down per the datasheet's
 * recommended sequence. */
void sps30_app_sleep_step_start(void *ctx);
uint8_t sps30_app_sleep_step_is_done(void *ctx);

#ifdef __cplusplus
}
#endif

#endif