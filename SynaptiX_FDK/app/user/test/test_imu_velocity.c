#include "test_imu_velocity.h"
#include "sx_board.h"
#include "accel_app.h"
#include "imu_velocity.h"
#include "calib_imu.h"
#include "bno055.h"
#include "logger.h"

static const char *TAG = "TEST_IMU_VEL";

/* gps->speed is in KNOTS (see gps.c's minmea_tofloat(&rmc.speed) //
 * knots comment) -- imu_velocity's whole interface is km/h throughout
 * (velocity_kph, imu_velocity_scale_calib_update()'s gps_speed_kph
 * param), so this conversion happens right at the boundary here rather
 * than anywhere inside imu_velocity.c, keeping that module unit-agnostic
 * of GPS's own on-wire unit choice. */
#define KPH_PER_KNOT   1.852f

/* No explicit "fix valid" flag exists on sx_gps_t (see gps.h) -- the
 * closest available signal is satellites (GGA's satellites_tracked,
 * see gps.c). 4 is the conventional minimum for a usable 3D fix (GPS
 * needs 4 satellites to solve for x/y/z/clock-bias); below that,
 * speed/position from a 2D-only or marginal fix can be unreliable
 * enough that feeding it into Stage C's scale_factor running average
 * risks polluting it with a bad ratio. Not from this project's own
 * measurements -- a standard GPS-domain rule of thumb, revisit if real
 * drive-test log ever shows fixes at exactly 4 satellites still being
 * too noisy for Stage C's needs. */
#define GPS_MIN_SATELLITES_FOR_FIX   4

/* Reuses accel_app_t purely as this test's "is the vehicle stationary"
 * signal -- same role app.c's real integration will eventually give
 * it (see imu_velocity.h's Stage A doc-comment: this module does not
 * decide "stationary" itself, it only consumes the caller's decision).
 * Owned here rather than shared with test_imu.c since each test_*.c
 * in this directory stands alone (see test_imu.c's own s_accel for
 * the same reasoning) -- run only one of the two IMU tests at a time
 * if sharing board.imu's sampling cadence matters, otherwise both are
 * safe to run together since accel_app_poll() is stateless per caller
 * instance. */
static accel_app_t s_accel;
static imu_velocity_state_t s_imu_vel;

/* Stage A/A3's own "confirmed stationary" gate is intentionally
 * stricter than accel_app's raw per-tick flag (see
 * imu_velocity.h's Stage A doc-comment on
 * imu_velocity_bias_calib_sample()) -- accel_app_is_movement_detected()
 * can flicker false on a single quiet tick even while genuinely
 * moving slowly; requiring it to hold false continuously for
 * STATIONARY_CONFIRM_MS avoids feeding a moving sample into the bias
 * filters. Matches the value/reasoning the ft/heartbeat-session
 * handoff notes app.c's real integration uses. */
#define STATIONARY_CONFIRM_MS   3000U
static uint32_t s_stationary_accum_ms = 0;

static uint32_t s_velocity_poll_accum_ms = 0;
#define VELOCITY_POLL_PERIOD_MS   IMU_VELOCITY_SAMPLE_PERIOD_MS

static uint32_t s_status_log_accum_ms = 0;
#define STATUS_LOG_PERIOD_MS   2000U

/* Stage C is exercised here (unlike the bring-up test's other stages)
 * because, unlike Stage A/A2/B, it needs no vehicle motion to at least
 * START wiring up correctly -- imu_velocity_scale_calib_update() itself
 * early-returns harmlessly while velocity_kph stays near 0 (see its
 * own guard in imu_velocity.c), so calling it here even on a stationary
 * bench is safe and means the GPS-reading + unit-conversion + call
 * wiring is already exercised and logging BEFORE the first real drive
 * test, rather than being unwritten/untested code on day one of an
 * actual drive. The actual scale_factor values produced while
 * stationary are meaningless (ratio of ~0/~0) and are expected to not
 * update at all per that guard -- only the wiring itself is what this
 * validates ahead of a real drive. */
static uint32_t s_gps_poll_accum_ms = 0;
#define GPS_POLL_PERIOD_MS   1000U

void test_imu_velocity_init(void)
{
    log_info(TAG, "=== TEST imu_velocity (stationary-only stages) ===");

    /* board.imu already bno055_init()'d inside sx_board_init()
     * (sx_board.c) -- same assumption test_imu.c's own init makes. */
    accel_app_init(&s_accel, &board.imu);

    /* Chip-level BNO055 calibration restore happens before
     * imu_velocity_init()/bias_calib_load() -- Stage A's own bias
     * samples are only trustworthy once the chip's own fusion has
     * converged (see calib_imu.h's top comment), and restoring a saved
     * chip calibration is what lets that convergence happen fast
     * instead of from scratch this boot. */
    calib_imu_init(&board.imu);

    imu_velocity_init(&s_imu_vel);
    bool bias_loaded = imu_velocity_bias_calib_load(&s_imu_vel);

    s_stationary_accum_ms     = 0;
    s_velocity_poll_accum_ms  = 0;
    s_status_log_accum_ms     = 0;
    s_gps_poll_accum_ms       = 0;

    log_info(TAG, "Init done -- chip calib %s, bias calib %s",
              calib_imu_is_loaded() ? "RESTORED from flash" : "NOT restored (will self-calibrate)",
              bias_loaded ? "RESTORED from flash (usable immediately)" : "NOT restored (Stage A starts from scratch)");
    log_info(TAG, "Hold the board PERFECTLY STILL to calibrate/refine Stage A "
                   "(bias). Stationary must be continuously held for %u ms "
                   "before each sample counts (see STATIONARY_CONFIRM_MS). "
                   "For Stage A2 (temperature-compensated bias) to fully "
                   "calibrate, leave the board running long enough to warm "
                   "up after this cold start -- see status log below for "
                   "temp_cluster_low_c/high_c.",
              (unsigned)STATIONARY_CONFIRM_MS);
    log_warn(TAG, "Stage B's forward axis and Stage C's scale factor CANNOT "
                   "be meaningfully validated stationary -- both require "
                   "real GPS-referenced vehicle motion (see imu_velocity.h's "
                   "top comment). velocity_kph below is expected to stay "
                   "near 0 the entire time this board sits still. Stage C's "
                   "GPS-read wiring is exercised here regardless (see "
                   "GPS_POLL_PERIOD_MS block below) so it is not untested "
                   "code on day one of a real drive.");
}

void test_imu_velocity_poll(uint32_t delta_ms)
{
    accel_app_poll(&s_accel, delta_ms);

    /* Cheap (single I2C register read most calls, see calib_imu.c's own
     * doc-comment on why calib_stat itself isn't gated behind a timer
     * here) -- no-ops immediately once already saved this boot. */
    calib_imu_poll(&board.imu);

    bool stationary_now = !accel_app_is_movement_detected(&s_accel);

    if (stationary_now) {
        s_stationary_accum_ms += delta_ms;
    } else {
        if (s_stationary_accum_ms >= STATIONARY_CONFIRM_MS) {
            /* Was confirmed-stationary and just started moving --
             * this is exactly the "vehicle about to depart" edge
             * app.c's real ZUPT integration fires on (see
             * imu_velocity.h's imu_velocity_zero_velocity_update()
             * doc-comment). Logged here for visibility even though
             * this bring-up test has nothing moving to hand off to. */
            log_info(TAG, "Movement detected after %u ms stationary -- "
                           "ZUPT would fire here in the real integration",
                      (unsigned)s_stationary_accum_ms);
        }
        s_stationary_accum_ms = 0;
    }

    if (s_stationary_accum_ms >= STATIONARY_CONFIRM_MS) {
        /* Re-sampling every tick once the confirm window is already
         * satisfied matches Stage A3's "keep feeding samples every
         * stationary period" intent (imu_velocity.h) rather than
         * sampling once and going quiet -- the median+MA filter
         * chains internally handle repeated near-identical samples
         * without harm (see imu_velocity.c's _cluster_index_for_temp()
         * folding logic). */
        imu_velocity_bias_calib_sample(&s_imu_vel, &board.imu);
        imu_velocity_axis_calib_sample(&s_imu_vel, &board.imu);
        imu_velocity_zero_velocity_update(&s_imu_vel);

        /* Cheap to call every stationary tick regardless -- internally
         * decides whether an actual flash write is warranted this call
         * (see imu_velocity_bias_calib_save_if_needed()'s doc-comment
         * in imu_velocity.h for the three conditions), so this is not
         * "write to flash every 3 seconds while parked". */
        imu_velocity_bias_calib_save_if_needed(&s_imu_vel);
    }

    /* Poll runs regardless of stationary/moving state, same as the
     * real integration would once wired into app.c's main loop --
     * imu_velocity_poll() itself no-ops (leaves velocity_kph
     * untouched at 0) until bias_calibrated is true, see its
     * doc-comment in imu_velocity.h. */
    s_velocity_poll_accum_ms += delta_ms;
    if (s_velocity_poll_accum_ms >= VELOCITY_POLL_PERIOD_MS) {
        imu_velocity_poll(&s_imu_vel, &board.imu, s_velocity_poll_accum_ms);
        s_velocity_poll_accum_ms = 0;
    }

    /* Stage C: feed GPS speed (when a usable fix is available) into
     * the scale-factor running average. See this file's top comment on
     * KPH_PER_KNOT/GPS_MIN_SATELLITES_FOR_FIX for why the unit
     * conversion and fix-quality gate live here rather than inside
     * imu_velocity.c. board.gps is populated by gps_process(), driven
     * from the GPS UART elsewhere in the app -- this test only reads
     * it, same "caller owns the peripheral, this module just consumes
     * already-parsed state" pattern as board.imu. */
    s_gps_poll_accum_ms += delta_ms;
    if (s_gps_poll_accum_ms >= GPS_POLL_PERIOD_MS) {
        s_gps_poll_accum_ms = 0;

        if (board.gps.satellites >= GPS_MIN_SATELLITES_FOR_FIX) {
            float gps_speed_kph = board.gps.speed * KPH_PER_KNOT;
            imu_velocity_scale_calib_update(&s_imu_vel, gps_speed_kph);
        }
        /* Below GPS_MIN_SATELLITES_FOR_FIX: silently skip, same as
         * "no fix at all" -- imu_velocity_scale_calib_update() is safe
         * to simply not call (see its own doc-comment in
         * imu_velocity.h), no need to log every second this test sits
         * on a bench with no sky view. */
    }

    s_status_log_accum_ms += delta_ms;
    if (s_status_log_accum_ms >= STATUS_LOG_PERIOD_MS) {
        s_status_log_accum_ms = 0;

        log_info(TAG, "stationary=%s (%u/%u ms)  bias_cal=%s  bias_slope_cal=%s  "
                       "temp_low=%.1f temp_high=%.1f  velocity_kph=%.2f",
                  stationary_now ? "yes" : "NO",
                  (unsigned)s_stationary_accum_ms, (unsigned)STATIONARY_CONFIRM_MS,
                  s_imu_vel.bias_calibrated ? "yes" : "no",
                  s_imu_vel.bias_slope_calibrated ? "yes" : "no",
                  s_imu_vel.temp_cluster_low_c, s_imu_vel.temp_cluster_high_c,
                  s_imu_vel.velocity_kph);

        if (s_imu_vel.bias_calibrated) {
            log_info(TAG, "bias_intercept=(%.4f,%.4f,%.4f) bias_slope=(%.5f,%.5f,%.5f) m/s^2",
                      s_imu_vel.bias_intercept[0], s_imu_vel.bias_intercept[1], s_imu_vel.bias_intercept[2],
                      s_imu_vel.bias_slope[0], s_imu_vel.bias_slope[1], s_imu_vel.bias_slope[2]);
        }

        /* down_mag near 0 means Stage B never got a valid stationary
         * gravity sample yet -- worth calling out explicitly since
         * imu_velocity_poll() silently degrades to a less-accurate
         * fallback in that case (see its doc-comment in
         * imu_velocity.c) rather than failing loudly. */
        float d0 = s_imu_vel.gravity_axis_down[0];
        float d1 = s_imu_vel.gravity_axis_down[1];
        float d2 = s_imu_vel.gravity_axis_down[2];
        bool has_down_axis = (d0 * d0 + d1 * d1 + d2 * d2) > 0.01f;
        log_info(TAG, "down_axis=%s (%.3f,%.3f,%.3f)  forward_axis=%u (%s)",
                  has_down_axis ? "sampled" : "NOT sampled yet",
                  d0, d1, d2,
                  (unsigned)s_imu_vel.forward_axis,
                  s_imu_vel.forward_axis_calibrated ? "calibrated" : "ASSUMED, not measured");

        /* satellites logged alongside scale_factor so it's obvious from
         * this line alone WHY scale_factor isn't moving if the board
         * has no sky view (stationary indoor bring-up, the expected
         * case for this test) vs. a real "why hasn't this converged
         * during an actual drive" question later. */
        log_info(TAG, "Stage C: scale_factor=%.3f scale_calibrated=%s "
                       "gps_satellites=%d (need >=%d for a fix this test trusts)",
                  s_imu_vel.scale_factor,
                  s_imu_vel.scale_calibrated ? "yes" : "no",
                  board.gps.satellites, GPS_MIN_SATELLITES_FOR_FIX);
    }
}