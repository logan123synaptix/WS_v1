#ifndef ACCEL_APP_H
#define ACCEL_APP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "bno055.h"

/* App-layer periodic reader + movement detector for BNO055, ported from
 * WS_v0's SynaptiX/apps/accelerometer/accelerometer.c with deliberate
 * changes per discussion with the user:
 *
 *  1. Clean app-layer pattern (matches sps30_app/sx_temp_humi) instead of
 *     WS_v0's style of writing straight into a global weatherstation.movement
 *     field: state kept in accel_app_t, exposed via a getter
 *     (accel_app_is_movement_detected()).
 *
 *  2. Sleep support added: WS_v0's board had no equivalent step at all.
 *     Per the BNO055 datasheet (Documents/bno055.md, section 3.2.3 Suspend
 *     Mode): "In suspend mode the system is paused and all the sensors and
 *     the microcontroller are put into sleep mode." This is a real
 *     power-down (unlike ZE12A's Question & Answer mode, which does not
 *     reduce the sensor's own current draw) — the chip must first be
 *     dropped to CONFIGMODE before writing PWR_MODE (bno055_set_pwr_mode()
 *     already does this internally, see bno055.c). To leave suspend, the
 *     datasheet says only "the mode should be changed by writing to the
 *     PWR_MODE register" — but since bno055_init() itself does not run
 *     again on wake, this module's wake step must also explicitly restore
 *     the fusion operation mode (NDOF) that bno055_init() originally set,
 *     via bno055_set_opr_mode() — the datasheet's PWR_MODE write alone
 *     brings the chip back to NORMAL power but does not by itself resume
 *     NDOF output.
 *
 *  3. ACCEL_APP_MOVEMENT_THRESHOLD / ACCEL_APP_FILTER_ALPHA are #defines
 *     (not hardcoded inline like WS_v0) carried over unchanged from
 *     WS_v0's values (0.005f / 0.1f) — their origin/derivation is not
 *     documented anywhere in WS_v0, so they are NOT validated for this
 *     board revision and MUST be recalibrated against real hardware
 *     before being trusted. Flagged again below at the #define site. */

#define ACCEL_APP_SAMPLE_PERIOD_MS      100U    /* matches WS_v0's accelerometer_poll() cadence */

/* NOT validated for WS_v1 hardware — carried over from WS_v0 with unknown
 * calibration basis. Recalibrate against real board + real-world motion
 * before relying on this for anything beyond a rough placeholder. */
#define ACCEL_APP_MOVEMENT_THRESHOLD    0.005f
#define ACCEL_APP_FILTER_ALPHA          0.1f

typedef struct {
    bno055_t   *imu;
    uint32_t    period_elapsed_ms;
    float       filtered_mag;       /* low-pass filtered |linear_accel| */
    bool        has_reading;        /* true once at least one sample has been filtered */
    bool        movement_detected;
} accel_app_t;

/* imu must already be bno055_init()'d (see sx_board.c) — this module only
 * drives the periodic sampling + filtering on top of it, it does not own
 * I2C/reset GPIO setup. */
void accel_app_init(accel_app_t *app, bno055_t *imu);

/* Call once per app tick with the elapsed time since the last call.
 * Non-blocking: reads linear acceleration every ACCEL_APP_SAMPLE_PERIOD_MS,
 * low-pass filters the magnitude, and flags movement_detected when the
 * filtered value changes by more than ACCEL_APP_MOVEMENT_THRESHOLD between
 * consecutive samples — same detection logic as WS_v0's accelerometer_poll(),
 * just kept in accel_app_t instead of a global. */
void accel_app_poll(accel_app_t *app, uint32_t delta_ms);

bool accel_app_is_movement_detected(accel_app_t *app);

/* ===== sleep_step-compatible power-down/wake-up (see sx_sleep_step_t in
 * sx_sleep_service.h) — intended to be registered directly into
 * sx_sleep_manager's sleep_steps/wake_steps arrays.
 *
 * Sleep: bno055_set_pwr_mode(SUSPEND) — a real power-down per the
 * datasheet (see doc-comment above), not just a UART/processing-load
 * reduction like ZE12A's QA mode.
 *
 * Wake: bno055_set_pwr_mode(NORMAL) then bno055_set_opr_mode(NDOF) to
 * restore fusion output, since the chip does not resume its previous
 * operation mode automatically on leaving suspend.
 *
 * Both are cheap one-shot I2C writes with a short internal delay
 * (sx_delay_ms(DELAY_MODE_SWITCH_MS) inside bno055_set_opr_mode()) —
 * fire-and-forget, is_done() always reports done on first poll, same
 * style as the ZE12A wrapper steps in sx_sleep_manager.c. */
void accel_app_sleep_step_start(void *ctx);
uint8_t accel_app_sleep_step_is_done(void *ctx);

void accel_app_wake_step_start(void *ctx);
uint8_t accel_app_wake_step_is_done(void *ctx);

#ifdef __cplusplus
}
#endif

#endif