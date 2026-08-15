#ifndef IMU_VELOCITY_H
#define IMU_VELOCITY_H

/* GPS-referenced IMU velocity estimator (2026-08-13, per the user).
 *
 * Goal: read vehicle speed (km/h) from the BNO055 IMU, calibrated
 * against GPS speed as ground truth, so a speed threshold (e.g. "< 20
 * km/h -> turn pump on, air intake vent handles flow above that") can be
 * evaluated even during the seconds/minutes GPS has no fix (tunnels,
 * dense urban canyon) — see the pump-control feature discussion this
 * session for the full motivation.
 *
 * IMPORTANT — why this cannot be a plain accel->velocity integrator:
 * per BNO055's own datasheet (Documents/bno055.md, section 3.6.5.6):
 * "The linear acceleration signal typically cannot be integrated to
 * recover velocity... The error typically becomes larger than the
 * signal within less than 1 second if other sensor sources are not used
 * to compensate this integration error." GPS speed is that other
 * source. This module is a three-stage calibration + fusion pipeline,
 * not a naive integrator:
 *
 *   STAGE A — Bias calibration (implemented, testable stationary, no
 *     GPS/vehicle needed): even sitting perfectly still, linear_accel
 *     never reads exactly (0,0,0) — sensor noise, slight mounting tilt,
 *     temperature drift. That nonzero reading, left uncorrected, would
 *     integrate into phantom velocity even at a dead stop. This stage
 *     samples linear_accel while confirmed stationary (see
 *     imu_velocity_bias_calib_is_stationary()'s caller-supplied
 *     condition) through a median filter (outlier rejection) feeding a
 *     moving-average filter (see SynaptiX_FDK/components/third_party/
 *     embfilt/), and holds the result as accel_bias_{x,y,z} once the MA
 *     window fills and settles.
 *
 *     STAGE A2 — Temperature-compensated bias (2026-08-13, per the
 *     user's request for a calibration good enough to run independent
 *     of GPS for meaningful stretches): a single bias value calibrated
 *     at one temperature does not hold as the board warms up during
 *     operation — this is well documented in MEMS accelerometer
 *     literature as "Temperature Drift of Bias" (TDB), typically
 *     modeled as roughly linear in temperature over an accelerometer's
 *     working range (see e.g. Melo et al., "Lightweight Thermal
 *     Compensation Technique for MEMS Capacitive Accelerometer",
 *     Sensors 2021, PMC8124870 — measured 1.3 mg/°C TDB on a comparable
 *     consumer MEMS part, and reported ~47% drift reduction from a
 *     two-point linear compensation alone). Rather than one
 *     accel_bias[3], this module fits bias as a linear function of
 *     BNO055's onboard temperature sensor (bno055_get_temperature(),
 *     added to the driver alongside this feature) once samples exist
 *     at two sufficiently different temperatures:
 *         bias(T) = bias_intercept + bias_slope * T
 *     Every Stage A sample updates a per-axis two-point (or
 *     least-squares once >2 clusters exist) fit rather than a single
 *     running average. Until a second distinct temperature cluster has
 *     been seen, bias_slope stays 0 and this degrades to exactly the
 *     single-temperature behavior described above — still correct, just
 *     not yet temperature-compensated.
 *
 *     STAGE A3 — Re-calibration on every stationary period (ZUPT +
 *     rolling bias refresh, 2026-08-13, per the user): per the "static
 *     detection" technique reported in IMU drift-compensation practice
 *     (bias is only truly known good at the moment of a confirmed
 *     zero-velocity condition — see imu_velocity_zero_velocity_update()
 *     below), every stationary period (not just the first one at boot)
 *     both zeroes velocity AND feeds a fresh sample into the Stage A2
 *     fit at the *current* temperature. This means bias_slope/intercept
 *     keep improving over the vehicle's operating life as more
 *     temperature points are seen (cold start, warmed up, hot day vs.
 *     cold night, etc.) rather than being fixed forever from one boot's
 *     calibration run.
 *
 *   STAGE B — Reference axes (partially testable stationary): which of
 *     the sensor's 3 axes is "down" can be determined stationary, from
 *     bno055_get_gravity() alone — no vehicle motion needed, just the
 *     board sitting in the orientation it will actually be mounted in.
 *     Which axis is "forward" (the direction of travel) CANNOT be
 *     determined stationary — there's no accelerating motion yet to
 *     compare against GPS course. Until Stage C runs for the first time
 *     with real vehicle motion, forward_axis_calibrated stays false and
 *     the module falls back to a configurable assumed mounting axis
 *     (see IMU_VELOCITY_ASSUMED_FORWARD_AXIS) — this is a guess, not a
 *     calibrated value, and is logged as such.
 *
 *   STAGE C — Scale factor (NOT testable without a real GPS-fixed drive
 *     test; stubbed here, defaults to 1.0x, framework only): compares
 *     accumulated IMU-integrated velocity against GPS speed at each new
 *     GPS fix, running an average of the ratio to correct for systematic
 *     sensor scale error. Call imu_velocity_scale_calib_update() once
 *     real drive-test data is available; until then this is inert.
 *
 * None of this is safety-critical navigation — it only needs to be good
 * enough to answer "is the vehicle currently below ~20 km/h", not
 * precise enough for dead-reckoning position.
 *
 * HONEST LIMITATION (2026-08-13, per the user asking specifically about
 * running independent of GPS indefinitely): temperature compensation
 * (Stage A2) and per-stop bias refresh (Stage A3) substantially reduce
 * drift versus a single static bias, and ZUPT resets accumulated error
 * to exactly 0 every time the vehicle is confirmed stationary — but
 * neither individually nor combined do they eliminate drift entirely.
 * This is not a gap in this implementation; it is a physical property
 * of MEMS accelerometer integration that no calibration scheme fully
 * removes (residual noise/bias after even ideal temperature compensation
 * still integrates into growing velocity error the longer a stretch of
 * continuous motion runs without a ZUPT reset or GPS correction — see
 * e.g. "How to Handle IMU Gyroscope Temperature Drift Effectively"
 * (guidenav.com, 2025): "Even with strong hardware design, precise
 * calibration, and real-time compensation, small residual drift will
 * always remain"). Practically, for this project's actual use case —
 * short tunnels/traffic stops on the order of tens of seconds to a few
 * minutes between ZUPT-triggering stops — the *time window* drift has
 * to accumulate in before the next reset is naturally short, which is
 * why this approach is viable for a binary <20kph threshold even though
 * it would not be viable for, say, dead-reckoning position over a
 * GPS-denied hour. */

#include <stdint.h>
#include <stdbool.h>
#include "bno055.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Sample period for Stage A/C's IMU reads, distinct from
 * ACCEL_APP_SAMPLE_PERIOD_MS (accel_app.h, 3000ms — far too slow to
 * integrate meaningfully, see this header's top comment and that
 * file's own doc-comment on why 3000ms is fine for its own purpose,
 * simple movement/no-movement detection). 100ms (10Hz) is a starting
 * point that keeps I2C/CPU load low while still being fast enough that
 * rectangular-integration error over one period is small for
 * city-traffic accelerations (a few m/s^2 at most) -- revisit once real
 * drive-test data (Stage C) shows whether 10Hz is actually sufficient
 * or needs to go higher. */
#define IMU_VELOCITY_SAMPLE_PERIOD_MS   100U

/* Stage A: number of stationary samples the median+MA filter pipeline
 * needs before accel_bias_* is considered settled and usable. At
 * IMU_VELOCITY_SAMPLE_PERIOD_MS=100ms this is ~5s of confirmed-
 * stationary sampling -- long enough to average out I2C-transient
 * glitches (the median stage) and short-term jitter (the MA stage)
 * without making the user wait unreasonably long for a bias calib run. */
#define IMU_VELOCITY_BIAS_MEDIAN_WINDOW  7U   /* must be odd, see median_filt.h */
#define IMU_VELOCITY_BIAS_MA_WINDOW      50U

/* Stage A2: minimum temperature separation (degC) between the low and
 * high calibration clusters before bias_slope is trusted/computed.
 * Below this, the two points are too close together for a slope
 * estimate to be numerically stable (small denominator in
 * (bias_high-bias_low)/(temp_high-temp_low)) — see PMC8124870's
 * two-temperature-point method, which the user asked this to follow;
 * that paper used a 20 degC span in its own validation. 10 degC here is
 * a lower, more easily reached bar appropriate for opportunistic
 * calibration during normal vehicle operation (cold start vs. warmed
 * up engine bay) rather than a controlled lab test — err toward
 * reaching *some* slope estimate sooner even if less precise than the
 * paper's controlled 20 degC span, since 0 slope (the fallback while
 * uncalibrated) is guaranteed wrong once the board's temperature
 * actually varies during real operation. */
#define IMU_VELOCITY_TEMP_CLUSTER_MIN_SEPARATION_C   10.0f

/* Stage B fallback: which raw sensor axis to assume is "forward" until
 * Stage C's first real drive test determines and locks in the true
 * axis from GPS course comparison. 0=X, 1=Y, 2=Z. This is a guess based
 * on how the board is expected to be mounted, not a measurement --
 * imu_velocity_get_state()'s forward_axis_calibrated tells the caller
 * whether to trust it as more than that. */
#define IMU_VELOCITY_ASSUMED_FORWARD_AXIS   0U

typedef struct {
    /* Stage A / A2 / A3 state: temperature-compensated bias, per axis.
     * bias(T) = bias_intercept[axis] + bias_slope[axis] * temperature_c.
     * bias_slope starts at 0 (flat line == old single-value behavior)
     * and only becomes meaningful once two sufficiently separated
     * temperature clusters have been sampled — see
     * temp_cluster_low_c/temp_cluster_high_c and
     * imu_velocity_bias_calib_sample()'s doc-comment in imu_velocity.c
     * for the two-point fit this uses. */
    bool     bias_calibrated;          /* true once the low-temperature cluster's MA window has filled at least once */
    float    bias_intercept[3];        /* m/s^2, bias at temperature_c == 0 per the current fit */
    float    bias_slope[3];            /* m/s^2 per degC */
    bool     bias_slope_calibrated;    /* true once a second, sufficiently different temperature cluster has also filled */
    float    temp_cluster_low_c;       /* degC of the first (coldest-seen) calibration cluster */
    float    temp_cluster_high_c;      /* degC of the most recent distinct cluster used to fit bias_slope */

    /* Stage B state */
    bool     forward_axis_calibrated;   /* true once Stage C has run at least once */
    uint8_t  forward_axis;              /* 0/1/2 -- IMU_VELOCITY_ASSUMED_FORWARD_AXIS until calibrated */
    float    gravity_axis_down[3];      /* unit vector, from bno055_get_gravity() while stationary */

    /* Stage C state (framework only -- see this header's top comment) */
    float    scale_factor;         /* multiplier applied to raw IMU-integrated speed; 1.0 = uncalibrated */
    bool     scale_calibrated;

    /* Fused output */
    float    velocity_kph;         /* current best estimate, 0 until bias_calibrated */
    uint32_t last_gps_fix_tick_ms; /* HAL_GetTick() at last GPS-referenced correction, 0 = never */
} imu_velocity_state_t;

/* Zeroes state; does not touch the IMU (caller's bno055_t is passed
 * per-call, this module holds no IMU handle of its own — matches
 * accel_app_t's ownership pattern where the caller (app.c) owns the
 * bno055_t and passes a pointer in). */
void imu_velocity_init(imu_velocity_state_t *state);

/* ===== Stage A: bias calibration ===== */

/* Call every IMU_VELOCITY_SAMPLE_PERIOD_MS while the caller has
 * independently confirmed the vehicle is stationary (e.g.
 * accel_app_is_movement_detected() == false held for several seconds,
 * AND gps speed == 0 if a fix is available -- this module does not
 * decide "stationary" itself, it only consumes the raw IMU sample once
 * the caller has decided it's safe to sample for bias). Internally
 * reads bno055_get_temperature() itself (no separate caller-supplied
 * temperature parameter -- keeps this call symmetric with
 * imu_velocity_axis_calib_sample()'s signature, and the temperature
 * read is cheap, one extra register byte on the same I2C bus already in
 * use) and routes each sample into whichever of two temperature
 * clusters (low/high, see temp_cluster_low_c/temp_cluster_high_c) it's
 * closer to, each independently median+MA filtered per axis (six filter
 * chains total: 3 axes x 2 clusters). Once the low cluster's MA window
 * fills, bias_calibrated flips true (matches old single-bias behavior).
 * Once a second cluster at least IMU_VELOCITY_TEMP_CLUSTER_MIN_
 * SEPARATION_C away also fills, bias_slope_calibrated flips true and
 * bias_slope[]/bias_intercept[] are (re)computed from the two clusters'
 * settled means — see this header's Stage A2 doc-comment above for why
 * a temperature-dependent bias matters here. Safe to call repeatedly
 * across the vehicle's entire operating life (not just once at boot) —
 * see Stage A3's doc-comment above for why continuing to feed this
 * every stationary period, not just the first, matters. */
void imu_velocity_bias_calib_sample(imu_velocity_state_t *state, bno055_t *imu);

/* ===== Stage B: reference axes ===== */

/* Call once while confirmed stationary (same caller-supplied condition
 * as Stage A, can be the same call site) to capture which raw axis is
 * "down" via bno055_get_gravity(). Does not determine the forward axis
 * (see this header's top comment) -- state->forward_axis stays at
 * IMU_VELOCITY_ASSUMED_FORWARD_AXIS/forward_axis_calibrated==false until
 * Stage C runs with real motion. */
void imu_velocity_axis_calib_sample(imu_velocity_state_t *state, bno055_t *imu);

/* ===== Stage C: scale factor (framework only, see top comment) ===== */

/* Call whenever a fresh GPS fix + valid speed becomes available.
 * gps_speed_kph: ground truth from GPS for this instant.
 * NOT YET EXERCISED WITH REAL DATA — no drive test has been run as of
 * 2026-08-13 (stationary-only testing so far). Safe to leave uncalled;
 * state->scale_factor simply stays at its 1.0 default and
 * scale_calibrated stays false, and imu_velocity_get_state()'s output
 * degrades gracefully to an uncalibrated (but bias-corrected)
 * integration. */
void imu_velocity_scale_calib_update(imu_velocity_state_t *state, float gps_speed_kph);

/* ===== Runtime velocity estimate ===== */

/* Call every IMU_VELOCITY_SAMPLE_PERIOD_MS during normal operation
 * (moving or possibly moving) to integrate bias-corrected, axis-
 * projected acceleration into state->velocity_kph. Only meaningful once
 * state->bias_calibrated is true -- returns/leaves velocity_kph at 0
 * otherwise, same as never having called this. Does not itself decide
 * whether to trust the result over long GPS-outage periods (that policy
 * belongs in app.c's pump-control logic, once this module is wired in
 * there — see this header's top comment). */
void imu_velocity_poll(imu_velocity_state_t *state, bno055_t *imu, uint32_t delta_ms);

/* Hard reset of the integrated velocity to 0 — call this on a confirmed
 * zero-velocity condition (e.g. accel_app_is_movement_detected()==false
 * held for several seconds) to clear accumulated integration drift.
 * Standard "ZUPT" (zero-velocity update) technique in inertial
 * navigation; without periodic resets like this, velocity_kph would
 * drift away from 0 during long stationary periods (traffic lights,
 * tunnels) even with a well-calibrated bias, since the bias correction
 * itself is never perfectly exact.
 *
 * Only resets velocity_kph — does NOT itself call
 * imu_velocity_bias_calib_sample(). Call both together at every
 * confirmed-stationary event (see Stage A3's doc-comment in this
 * header's top comment) — app.c's integration of this module does so
 * already; kept as two separate calls here rather than one combined
 * function so a caller that only wants the velocity reset (e.g. right
 * after Stage A/A3 sampling already ran this tick) isn't forced to
 * re-sample. */
void imu_velocity_zero_velocity_update(imu_velocity_state_t *state);

#ifdef __cplusplus
}
#endif

#endif