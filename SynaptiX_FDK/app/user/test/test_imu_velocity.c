#include "test_imu_velocity.h"
#include "sx_board.h"
#include "accel_app.h"
#include "imu_velocity.h"
#include "bno055.h"
#include "logger.h"

static const char *TAG = "TEST_IMU_VEL";

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

void test_imu_velocity_init(void)
{
    log_info(TAG, "=== TEST imu_velocity (stationary-only stages) ===");

    /* board.imu already bno055_init()'d inside sx_board_init()
     * (sx_board.c) -- same assumption test_imu.c's own init makes. */
    accel_app_init(&s_accel, &board.imu);
    imu_velocity_init(&s_imu_vel);

    s_stationary_accum_ms     = 0;
    s_velocity_poll_accum_ms  = 0;
    s_status_log_accum_ms     = 0;

    log_info(TAG, "Init done -- hold the board PERFECTLY STILL to calibrate "
                   "Stage A (bias). Stationary must be continuously held for "
                   "%u ms before each sample counts (see "
                   "STATIONARY_CONFIRM_MS). For Stage A2 (temperature-"
                   "compensated bias) to fully calibrate, leave the board "
                   "running long enough to warm up after this cold start -- "
                   "see status log below for temp_cluster_low_c/high_c.",
              (unsigned)STATIONARY_CONFIRM_MS);
    log_warn(TAG, "Stage B's forward axis and Stage C's scale factor CANNOT "
                   "be validated by this test -- both require real "
                   "GPS-referenced vehicle motion (see imu_velocity.h's top "
                   "comment). velocity_kph below is expected to stay near 0 "
                   "the entire time this board sits still.");
}

void test_imu_velocity_poll(uint32_t delta_ms)
{
    accel_app_poll(&s_accel, delta_ms);

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
    }
}