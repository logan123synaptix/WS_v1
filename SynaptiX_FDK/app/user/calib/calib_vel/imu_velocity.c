#include "imu_velocity.h"
#include <string.h>
#include <math.h>
#include "logger.h"
#include "ma_filt.h"
#include "median_filt.h"
#include "app_config.h"
#include "sx_ex_storage.h"

static const char *TAG = "IMU_VELOCITY";

/* Same /100.0f LSB->m/s^2 conversion accel_app.c already established for
 * linear_accel (see that file's doc-comment quoting Documents/bno055.md's
 * "1 m/s^2 = 100 LSB" unit table) — Gravity Vector uses the identical
 * representation per the datasheet's Table 3-35, so one constant serves
 * both registers read in this file. */
#define IMU_VELOCITY_LSB_PER_MS2   100.0f

#define IMU_VELOCITY_KPH_PER_MPS   3.6f

/* ===== Stage A / A2 / A3: temperature-compensated bias calibration ===== */

/* Two independent per-axis median+MA filter chains -- one per
 * temperature cluster (low/high, see temp_cluster_low_c/
 * temp_cluster_high_c in imu_velocity_state_t) -- rather than one. Each
 * incoming stationary sample is routed to whichever cluster it's
 * numerically closer to (see _cluster_index_for_temp() below), so the
 * two clusters naturally separate into "the two most different
 * temperatures seen so far" without needing the caller to pre-sort
 * samples by temperature itself. Static/buffer-pointer reasoning is the
 * same as the single-cluster version this replaces (see this file's
 * git history) -- exactly one BNO055 on this board, buffers must
 * outlive individual calls, kept out of imu_velocity_state_t so that
 * struct stays plain/copyable data. */
static median_t s_bias_median[2][3];
static float    s_bias_median_buf[2][3][IMU_VELOCITY_BIAS_MEDIAN_WINDOW * 2];
static ma_t     s_bias_ma[2][3];
static float    s_bias_ma_buf[2][3][IMU_VELOCITY_BIAS_MA_WINDOW];
static uint32_t s_bias_ma_sample_count[2];
static float    s_cluster_settled_bias[2][3];   /* last MA output per cluster/axis, valid once s_bias_ma_sample_count[cluster] > 0 */
static bool     s_bias_filters_inited;

static void _bias_filters_init(void)
{
    for (int cluster = 0; cluster < 2; cluster++) {
        for (int axis = 0; axis < 3; axis++) {
            median_init(&s_bias_median[cluster][axis], s_bias_median_buf[cluster][axis], IMU_VELOCITY_BIAS_MEDIAN_WINDOW);
            ma_init(&s_bias_ma[cluster][axis], s_bias_ma_buf[cluster][axis], IMU_VELOCITY_BIAS_MA_WINDOW);
        }
        s_bias_ma_sample_count[cluster] = 0;
    }
    s_bias_filters_inited = true;
}

/* Decides which of the two temperature clusters (0=low, 1=high) this
 * sample's temperature belongs to, creating/relocating the "high"
 * cluster as new, more-separated temperatures are seen. Returns -1 if
 * the sample is too close to an already-settled cluster to usefully add
 * new information (avoids diluting a settled cluster's MA window with
 * near-duplicate temperatures, which would just slow convergence
 * without improving the slope estimate).
 *
 * Logic, in order:
 *   1. No cluster seeded yet (first-ever call) -> becomes the low
 *      cluster unconditionally, at whatever temperature is first seen
 *      (could be a cold start or a warm re-init, no preference).
 *   2. Only the low cluster is seeded -> if this sample's temperature
 *      is at least IMU_VELOCITY_TEMP_CLUSTER_MIN_SEPARATION_C away from
 *      it, becomes the high cluster's first sample. Otherwise folds
 *      into the low cluster (refines it, same as Stage A3's "keep
 *      sampling every stationary period" intent for a single cluster).
 *   3. Both seeded -> compare distance to each cluster's current
 *      temperature; folds into whichever is closer (keeps both clusters
 *      internally consistent as temperature drifts slowly over the
 *      vehicle's operating life, e.g. engine bay warming gradually
 *      across a trip, rather than only ever the first two temperatures
 *      ever seen). If this sample is further from BOTH clusters than
 *      IMU_VELOCITY_TEMP_CLUSTER_MIN_SEPARATION_C, it becomes a new,
 *      more extreme low or high cluster (whichever side it's on),
 *      discarding the old opposite-side cluster's filter state -- keeps
 *      the two clusters as far apart as actually observed, which is
 *      what the linear fit's accuracy depends on. */
static int _cluster_index_for_temp(imu_velocity_state_t *state, float temp_c)
{
    if (s_bias_ma_sample_count[0] == 0 && s_bias_ma_sample_count[1] == 0) {
        state->temp_cluster_low_c = temp_c;
        return 0;
    }

    if (s_bias_ma_sample_count[1] == 0) {
        /* Only cluster 0 seeded so far. */
        float dist = fabsf(temp_c - state->temp_cluster_low_c);
        if (dist >= IMU_VELOCITY_TEMP_CLUSTER_MIN_SEPARATION_C) {
            state->temp_cluster_high_c = temp_c;
            return 1;
        }
        return 0;
    }

    /* Both seeded. */
    float dist_low  = fabsf(temp_c - state->temp_cluster_low_c);
    float dist_high = fabsf(temp_c - state->temp_cluster_high_c);

    if (dist_low <= dist_high) {
        if (dist_low < IMU_VELOCITY_TEMP_CLUSTER_MIN_SEPARATION_C) {
            return 0;
        }
        /* Further from the low cluster than the separation threshold,
         * and even closer to (or equal to) it than the high cluster --
         * this is a new, more extreme point on the low side. Re-seed
         * cluster 0. */
        state->temp_cluster_low_c = temp_c;
        for (int axis = 0; axis < 3; axis++) {
            median_reset(&s_bias_median[0][axis]);
            ma_reset(&s_bias_ma[0][axis]);
        }
        s_bias_ma_sample_count[0] = 0;
        return 0;
    } else {
        if (dist_high < IMU_VELOCITY_TEMP_CLUSTER_MIN_SEPARATION_C) {
            return 1;
        }
        state->temp_cluster_high_c = temp_c;
        for (int axis = 0; axis < 3; axis++) {
            median_reset(&s_bias_median[1][axis]);
            ma_reset(&s_bias_ma[1][axis]);
        }
        s_bias_ma_sample_count[1] = 0;
        return 1;
    }
}

static void _refit_bias_line(imu_velocity_state_t *state)
{
    if (s_bias_ma_sample_count[0] < IMU_VELOCITY_BIAS_MA_WINDOW) {
        return;   /* low cluster not settled yet -- nothing to fit */
    }

    if (!state->bias_calibrated) {
        state->bias_calibrated = true;
        log_info(TAG, "Stage A bias calibrated (single temperature %.1f degC): bias=(%.4f, %.4f, %.4f) m/s^2",
                  state->temp_cluster_low_c,
                  s_cluster_settled_bias[0][0], s_cluster_settled_bias[0][1], s_cluster_settled_bias[0][2]);
    }

    if (s_bias_ma_sample_count[1] < IMU_VELOCITY_BIAS_MA_WINDOW) {
        /* High cluster not settled yet -- keep the flat (slope==0) fit,
         * intercept == the low cluster's own settled bias (a flat line
         * through one point). */
        for (int axis = 0; axis < 3; axis++) {
            state->bias_intercept[axis] = s_cluster_settled_bias[0][axis];
            state->bias_slope[axis]     = 0.0f;
        }
        return;
    }

    float temp_span = state->temp_cluster_high_c - state->temp_cluster_low_c;
    if (fabsf(temp_span) < IMU_VELOCITY_TEMP_CLUSTER_MIN_SEPARATION_C) {
        /* Shouldn't happen given _cluster_index_for_temp()'s own
         * separation guard, but avoid a near-zero-denominator slope
         * blowing up if it ever does (e.g. future refactor loosens that
         * guard). */
        return;
    }

    for (int axis = 0; axis < 3; axis++) {
        float slope = (s_cluster_settled_bias[1][axis] - s_cluster_settled_bias[0][axis]) / temp_span;
        float intercept = s_cluster_settled_bias[0][axis] - slope * state->temp_cluster_low_c;
        state->bias_slope[axis]     = slope;
        state->bias_intercept[axis] = intercept;
    }

    if (!state->bias_slope_calibrated) {
        state->bias_slope_calibrated = true;
        log_info(TAG, "Stage A2 temperature-compensated bias calibrated: "
                       "low=%.1fdegC high=%.1fdegC slope=(%.5f,%.5f,%.5f) intercept=(%.4f,%.4f,%.4f)",
                  state->temp_cluster_low_c, state->temp_cluster_high_c,
                  state->bias_slope[0], state->bias_slope[1], state->bias_slope[2],
                  state->bias_intercept[0], state->bias_intercept[1], state->bias_intercept[2]);
    }
}

void imu_velocity_bias_calib_sample(imu_velocity_state_t *state, bno055_t *imu)
{
    if (!s_bias_filters_inited) {
        _bias_filters_init();
    }

    bno055_vec3_t v;
    if (bno055_get_linear_accel(imu, &v) != BNO055_OK) {
        log_warn(TAG, "bias calib: linear accel read failed");
        return;
    }

    int8_t temp_c_raw;
    if (bno055_get_temperature(imu, &temp_c_raw) != BNO055_OK) {
        log_warn(TAG, "bias calib: temperature read failed");
        return;
    }
    float temp_c = (float)temp_c_raw;

    int cluster = _cluster_index_for_temp(state, temp_c);
    if (cluster < 0) {
        return;   /* not currently possible per _cluster_index_for_temp()'s logic, but keep the contract explicit */
    }

    float raw[3] = {
        (float)v.x / IMU_VELOCITY_LSB_PER_MS2,
        (float)v.y / IMU_VELOCITY_LSB_PER_MS2,
        (float)v.z / IMU_VELOCITY_LSB_PER_MS2,
    };

    for (int axis = 0; axis < 3; axis++) {
        float despiked = median(&s_bias_median[cluster][axis], raw[axis]);
        s_cluster_settled_bias[cluster][axis] = ma(&s_bias_ma[cluster][axis], despiked);
    }

    if (s_bias_ma_sample_count[cluster] < IMU_VELOCITY_BIAS_MA_WINDOW) {
        s_bias_ma_sample_count[cluster]++;
    }

    _refit_bias_line(state);
}

/* Evaluates the temperature-compensated bias line at the IMU's current
 * temperature. Used by imu_velocity_poll() below rather than a plain
 * state->bias_intercept[]/bias_slope[] pair, since bias is no longer a single constant
 * -- see this file's Stage A2 doc-comment. */
static void _bias_at_current_temp(imu_velocity_state_t *state, bno055_t *imu, float out_bias[3])
{
    int8_t temp_c_raw;
    float temp_c;
    if (bno055_get_temperature(imu, &temp_c_raw) == BNO055_OK) {
        temp_c = (float)temp_c_raw;
    } else {
        /* Temperature read failed -- fall back to the low cluster's own
         * calibration temperature rather than leaving out[] undefined;
         * this makes bias_slope's contribution 0 for this one poll
         * cycle (same as pre-Stage-A2 behavior), not a hard failure of
         * the whole velocity estimate over one bad I2C transaction. */
        temp_c = state->temp_cluster_low_c;
        log_warn(TAG, "poll: temperature read failed, using calibration temperature as fallback");
    }

    for (int axis = 0; axis < 3; axis++) {
        out_bias[axis] = state->bias_intercept[axis] + state->bias_slope[axis] * temp_c;
    }
}


void imu_velocity_init(imu_velocity_state_t *state)
{
    memset(state, 0, sizeof(*state));
    state->forward_axis  = IMU_VELOCITY_AXIS_FORWARD_ASSUMED;
    state->scale_factor  = 1.0f;
    _bias_filters_init();
}

/* ===== Stage B: reference axes ===== */

void imu_velocity_axis_calib_sample(imu_velocity_state_t *state, bno055_t *imu)
{
    bno055_vec3_t g;
    if (bno055_get_gravity(imu, &g) != BNO055_OK) {
        log_warn(TAG, "axis calib: gravity read failed");
        return;
    }

    float gx = (float)g.x / IMU_VELOCITY_LSB_PER_MS2;
    float gy = (float)g.y / IMU_VELOCITY_LSB_PER_MS2;
    float gz = (float)g.z / IMU_VELOCITY_LSB_PER_MS2;
    float mag = sqrtf(gx * gx + gy * gy + gz * gz);

    if (mag < 0.1f) {
        /* Should be ~9.8 m/s^2 in stationary NDOF fusion mode -- a near-
         * zero reading means the fusion hasn't converged yet (e.g. just
         * woke from suspend) rather than a real "no gravity" state.
         * Skip rather than divide-by-near-zero below. */
        log_warn(TAG, "axis calib: gravity magnitude too small (%.3f), fusion not settled yet", mag);
        return;
    }

    state->gravity_axis_down[0] = gx / mag;
    state->gravity_axis_down[1] = gy / mag;
    state->gravity_axis_down[2] = gz / mag;

    log_info(TAG, "Stage B gravity axis sampled: down=(%.3f, %.3f, %.3f) (forward axis still %s -- see Stage C)",
              state->gravity_axis_down[0], state->gravity_axis_down[1], state->gravity_axis_down[2],
              state->forward_axis_calibrated ? "calibrated" : "ASSUMED, not measured");
}

/* ===== Stage C: scale factor (framework only) ===== */

void imu_velocity_scale_calib_update(imu_velocity_state_t *state, float gps_speed_kph)
{
    /* NOT YET EXERCISED WITH REAL DATA -- see this function's doc-comment
     * in imu_velocity.h. Framework kept intentionally minimal (a single
     * running-average update) until real drive-test data shows what
     * filtering this actually needs; do not over-engineer this stage
     * ahead of having real numbers to tune against. */
    if (state->velocity_kph <= 0.5f) {
        /* Avoid a divide-by-near-zero scale factor when the IMU's own
         * integrated estimate is ~0 but GPS says otherwise (e.g. right
         * at the start of accelerating) -- wait for a more meaningful
         * IMU speed before trusting the ratio. */
        return;
    }

    float instantaneous_ratio = gps_speed_kph / state->velocity_kph;

    if (!state->scale_calibrated) {
        state->scale_factor    = instantaneous_ratio;
        state->scale_calibrated = true;
    } else {
        /* Simple exponential running average -- same smoothing shape as
         * embfilt's iir_ema (see components/third_party/embfilt/README.md
         * for why only ma_filt/median_filt are vendored so far; pull
         * ema_filt.* from upstream if this stage needs a tunable cutoff
         * frequency instead of a fixed 0.05 blend once real data is
         * available to tune against). */
        const float alpha = 0.05f;
        state->scale_factor = state->scale_factor * (1.0f - alpha) + instantaneous_ratio * alpha;
    }

    log_info(TAG, "Stage C scale update: gps=%.1f kph imu_raw=%.1f kph ratio=%.3f -> scale_factor=%.3f",
              gps_speed_kph, state->velocity_kph, instantaneous_ratio, state->scale_factor);
}

/* ===== Runtime velocity estimate ===== */

void imu_velocity_poll(imu_velocity_state_t *state, bno055_t *imu, uint32_t delta_ms)
{
    if (!state->bias_calibrated) {
        /* Refuse to integrate against an uncalibrated (garbage) bias --
         * see this function's doc-comment in imu_velocity.h. */
        return;
    }

    bno055_vec3_t v;
    if (bno055_get_linear_accel(imu, &v) != BNO055_OK) {
        log_warn(TAG, "poll: linear accel read failed");
        return;
    }

    float raw[3] = {
        (float)v.x / IMU_VELOCITY_LSB_PER_MS2,
        (float)v.y / IMU_VELOCITY_LSB_PER_MS2,
        (float)v.z / IMU_VELOCITY_LSB_PER_MS2,
    };

    float bias_now[3];
    _bias_at_current_temp(state, imu, bias_now);

    float corrected[3];
    for (int axis = 0; axis < 3; axis++) {
        corrected[axis] = raw[axis] - bias_now[axis];
    }

    /* Project onto the horizontal plane (perpendicular to gravity_axis_down,
     * see Stage B) rather than just reading state->forward_axis's single
     * component directly -- this way, a bias-corrected acceleration that
     * has any component along "down" (e.g. slight residual mounting
     * error, or the vehicle on a grade) gets that component removed
     * rather than leaking into the horizontal magnitude the same way a
     * naive single-axis read would. Falls back to
     * IMU_VELOCITY_AXIS_FORWARD_ASSUMED's plain magnitude-of-all-3-axes
     * if Stage B hasn't sampled gravity_axis_down yet (all zero vector)
     * -- see the zero-vector guard below. */
    float down[3] = { state->gravity_axis_down[0], state->gravity_axis_down[1], state->gravity_axis_down[2] };
    float down_mag = sqrtf(down[0]*down[0] + down[1]*down[1] + down[2]*down[2]);

    float a_horizontal;
    if (down_mag < 0.1f) {
        /* Stage B never sampled (gravity_axis_down still zero-initialized)
         * -- fall back to total corrected magnitude. Less accurate on a
         * grade/tilt, but a reasonable degrade-gracefully default rather
         * than refusing to integrate at all. */
        a_horizontal = sqrtf(corrected[0]*corrected[0] + corrected[1]*corrected[1] + corrected[2]*corrected[2]);
    } else {
        float down_unit[3] = { down[0]/down_mag, down[1]/down_mag, down[2]/down_mag };
        float dot = corrected[0]*down_unit[0] + corrected[1]*down_unit[1] + corrected[2]*down_unit[2];
        float horiz[3] = {
            corrected[0] - dot * down_unit[0],
            corrected[1] - dot * down_unit[1],
            corrected[2] - dot * down_unit[2],
        };
        a_horizontal = sqrtf(horiz[0]*horiz[0] + horiz[1]*horiz[1] + horiz[2]*horiz[2]);
    }

    float dt_s = (float)delta_ms / 1000.0f;
    float v_mps = (state->velocity_kph / IMU_VELOCITY_KPH_PER_MPS) + a_horizontal * dt_s;
    if (v_mps < 0.0f) {
        v_mps = 0.0f;   /* speed can't be negative -- a horizontal-magnitude
                          * integration only ever adds, but guard anyway in
                          * case of future signed-projection changes here */
    }

    state->velocity_kph = v_mps * IMU_VELOCITY_KPH_PER_MPS * state->scale_factor;
}

void imu_velocity_zero_velocity_update(imu_velocity_state_t *state)
{
    state->velocity_kph = 0.0f;
}

/* ===== Stage A/A2 persistence (flash) ===== */

/* On-flash layout. Deliberately its own struct (not
 * imu_velocity_state_t written verbatim) so the fields NOT persisted
 * (gravity_axis_down, forward_axis*, scale_factor*, velocity_kph,
 * last_gps_fix_tick_ms -- see this struct's rationale in
 * imu_velocity.h's Stage A/A2 persistence section) can never
 * accidentally leak into or out of the saved file, and so this file's
 * on-disk layout doesn't silently change size/shape if
 * imu_velocity_state_t grows a new runtime-only field later. Bump a
 * version field here rather than changing this struct in place if its
 * layout ever needs to change -- same "don't reinterpret stale bytes"
 * posture network_config.c takes with NETWORK_CONFIG_VERSION, just not
 * needed yet since this is the struct's first version. */
typedef struct {
    float bias_intercept[3];
    float bias_slope[3];
    float temp_cluster_low_c;
    float temp_cluster_high_c;
    bool  bias_calibrated;
    bool  bias_slope_calibrated;
} imu_velocity_flash_bias_t;

/* The cluster temperatures actually written to flash on the last
 * successful save this boot (or after a successful load) -- what
 * imu_velocity_bias_calib_save_if_needed() compares state's current
 * RAM-side clusters against to decide whether drift has exceeded
 * IMU_VELOCITY_FLASH_SAVE_MIN_DRIFT_C. NOT the same thing as state's
 * own temp_cluster_low_c/high_c, which keep moving every stationary
 * period per Stage A3 regardless of whether a save happened -- this is
 * specifically "what's on flash right now", deliberately kept separate
 * so the drift comparison has a stable reference point between saves. */
static float s_flash_temp_cluster_low_c;
static float s_flash_temp_cluster_high_c;
static bool  s_flash_has_saved_this_boot;

bool imu_velocity_bias_calib_load(imu_velocity_state_t *state)
{
    int32_t size = sx_storage_size(FILE_SAVE_IMU_CALIBRATION);
    if (size <= 0) {
        log_info(TAG, "No saved bias calibration file - Stage A will "
                       "calibrate from scratch");
        return false;
    }

    imu_velocity_flash_bias_t saved;
    if ((uint32_t)size != sizeof(saved)) {
        log_warn(TAG, "Saved bias calibration file size mismatch (got %ld, "
                       "expected %u) - ignoring, Stage A will calibrate "
                       "from scratch", (long)size, (unsigned)sizeof(saved));
        return false;
    }

    sx_storage_err_t err = sx_storage_read(FILE_SAVE_IMU_CALIBRATION, &saved, sizeof(saved));
    if (err != SX_STORAGE_OK) {
        log_warn(TAG, "Failed to read saved bias calibration (err=%d) - "
                       "Stage A will calibrate from scratch", err);
        return false;
    }

    memcpy(state->bias_intercept, saved.bias_intercept, sizeof(state->bias_intercept));
    memcpy(state->bias_slope,     saved.bias_slope,     sizeof(state->bias_slope));
    state->temp_cluster_low_c    = saved.temp_cluster_low_c;
    state->temp_cluster_high_c   = saved.temp_cluster_high_c;
    state->bias_calibrated       = saved.bias_calibrated;
    state->bias_slope_calibrated = saved.bias_slope_calibrated;

    s_flash_temp_cluster_low_c  = saved.temp_cluster_low_c;
    s_flash_temp_cluster_high_c = saved.temp_cluster_high_c;
    /* Deliberately NOT set true here -- loading a file is not the same
     * event as this boot having written one. See this flag's other use
     * in imu_velocity_bias_calib_save_if_needed()'s "always save the
     * first time bias_calibrated/bias_slope_calibrated flips true"
     * rule, which must still be free to fire this boot even though a
     * file already existed, if e.g. only bias_calibrated (not
     * bias_slope_calibrated) was true in the saved file and this boot
     * is the first to also reach bias_slope_calibrated. */
    s_flash_has_saved_this_boot = false;

    log_info(TAG, "Loaded bias calibration from flash (intercept=(%.4f,%.4f,%.4f) "
                   "slope=(%.5f,%.5f,%.5f) temp_low=%.1f temp_high=%.1f "
                   "slope_calibrated=%s)",
              saved.bias_intercept[0], saved.bias_intercept[1], saved.bias_intercept[2],
              saved.bias_slope[0], saved.bias_slope[1], saved.bias_slope[2],
              saved.temp_cluster_low_c, saved.temp_cluster_high_c,
              saved.bias_slope_calibrated ? "yes" : "no");
    return true;
}

bool imu_velocity_bias_calib_save_if_needed(const imu_velocity_state_t *state)
{
    /* Nothing calibrated yet at all -- nothing to save. */
    if (!state->bias_calibrated) return false;

    /* Track "first time this boot bias_calibrated/bias_slope_calibrated
     * became true" locally rather than relying solely on
     * s_flash_has_saved_this_boot, since a save can also be triggered
     * later purely by drift (case below) without either flag having
     * just flipped -- these two static locals only answer "have we
     * already unconditionally-saved once for reaching bias_calibrated"
     * and "...for bias_slope_calibrated" specifically. */
    static bool s_saved_for_bias_calibrated       = false;
    static bool s_saved_for_bias_slope_calibrated = false;

    bool should_save = false;
    const char *reason = "";

    if (!s_saved_for_bias_calibrated) {
        should_save = true;
        reason = "bias_calibrated reached for the first time this boot";
    } else if (state->bias_slope_calibrated && !s_saved_for_bias_slope_calibrated) {
        should_save = true;
        reason = "bias_slope_calibrated reached for the first time this boot";
    } else if (s_flash_has_saved_this_boot || s_flash_temp_cluster_low_c != 0.0f
               || s_flash_temp_cluster_high_c != 0.0f) {
        /* Only meaningful once something has actually been saved at
         * least once (this boot, via the two branches above, or a
         * prior boot via imu_velocity_bias_calib_load() populating
         * s_flash_temp_cluster_*) -- compare current RAM clusters
         * against what's on flash, per IMU_VELOCITY_FLASH_SAVE_MIN_
         * DRIFT_C (see imu_velocity.h's Stage A/A2 persistence
         * section). */
        float drift_low  = fabsf(state->temp_cluster_low_c  - s_flash_temp_cluster_low_c);
        float drift_high = fabsf(state->temp_cluster_high_c - s_flash_temp_cluster_high_c);
        if (drift_low > IMU_VELOCITY_FLASH_SAVE_MIN_DRIFT_C
            || drift_high > IMU_VELOCITY_FLASH_SAVE_MIN_DRIFT_C) {
            should_save = true;
            reason = "temperature cluster drifted from last saved value";
        }
    }

    if (!should_save) return false;

    imu_velocity_flash_bias_t out;
    memcpy(out.bias_intercept, state->bias_intercept, sizeof(out.bias_intercept));
    memcpy(out.bias_slope,     state->bias_slope,     sizeof(out.bias_slope));
    out.temp_cluster_low_c    = state->temp_cluster_low_c;
    out.temp_cluster_high_c   = state->temp_cluster_high_c;
    out.bias_calibrated       = state->bias_calibrated;
    out.bias_slope_calibrated = state->bias_slope_calibrated;

    sx_storage_err_t err = sx_storage_write(FILE_SAVE_IMU_CALIBRATION, &out, sizeof(out));
    if (err != SX_STORAGE_OK) {
        log_error(TAG, "Failed to save bias calibration to flash (err=%d, "
                        "reason: %s) - will retry next call", err, reason);
        return false;
    }

    s_saved_for_bias_calibrated       = true;
    if (state->bias_slope_calibrated) s_saved_for_bias_slope_calibrated = true;
    s_flash_temp_cluster_low_c  = state->temp_cluster_low_c;
    s_flash_temp_cluster_high_c = state->temp_cluster_high_c;
    s_flash_has_saved_this_boot = true;

    log_info(TAG, "Saved bias calibration to flash (reason: %s) - "
                   "intercept=(%.4f,%.4f,%.4f) slope=(%.5f,%.5f,%.5f) "
                   "temp_low=%.1f temp_high=%.1f",
              reason,
              out.bias_intercept[0], out.bias_intercept[1], out.bias_intercept[2],
              out.bias_slope[0], out.bias_slope[1], out.bias_slope[2],
              out.temp_cluster_low_c, out.temp_cluster_high_c);
    return true;
}