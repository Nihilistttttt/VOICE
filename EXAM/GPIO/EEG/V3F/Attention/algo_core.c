#include "algo_core.h"
#include <string.h>
#include <math.h>

#define P2_NUM_MARKERS      5
#define P2_DESIRED_P10      0
#define P2_DESIRED_P50      2
#define P2_DESIRED_P90      4

static void p2_init(P2Quantile_t *s) __attribute__((unused));
static void p2_init(P2Quantile_t *s)
{
    memset(s, 0, sizeof(*s));
}

static void p2_update(P2Quantile_t *s, real_t x)
{
    if (!s->initialized) {
        if (s->count < 5) {
            s->q[s->count] = x;
            s->count++;
            if (s->count == 5) {
                int i, j;
                for (i = 0; i < 4; i++) {
                    for (j = i + 1; j < 5; j++) {
                        if (s->q[i] > s->q[j]) {
                            real_t t = s->q[i];
                            s->q[i] = s->q[j];
                            s->q[j] = t;
                        }
                    }
                }
                s->n[0] = 1.0f; s->n[1] = 2.0f; s->n[2] = 3.0f; s->n[3] = 4.0f; s->n[4] = 5.0f;
                s->npos[0] = 1.0f;
                s->npos[1] = 1.0f + 4.0f * 0.25f;
                s->npos[2] = 1.0f + 4.0f * 0.50f;
                s->npos[3] = 1.0f + 4.0f * 0.75f;
                s->npos[4] = 5.0f;
                s->initialized = 1;
            }
        }
        return;
    }

    s->count++;

    int k;
    if (x < s->q[0]) {
        s->q[0] = x;
        k = 0;
    } else if (x < s->q[1]) {
        k = 0;
    } else if (x < s->q[2]) {
        k = 1;
    } else if (x < s->q[3]) {
        k = 2;
    } else if (x < s->q[4]) {
        k = 3;
    } else {
        s->q[4] = x;
        k = 3;
    }

    int i;
    for (i = 0; i < 5; i++) s->n[i] += 1.0f;
    for (i = k + 1; i < 5; i++) s->npos[i] += 1.0f;

    real_t desired[5];
    desired[0] = 1.0f;
    desired[1] = 1.0f + (real_t)(s->count - 1) * 0.25f;
    desired[2] = 1.0f + (real_t)(s->count - 1) * 0.50f;
    desired[3] = 1.0f + (real_t)(s->count - 1) * 0.75f;
    desired[4] = (real_t)s->count;

    for (i = 0; i < 5; i++) s->npos[i] = desired[i];

    for (i = 1; i < 4; i++) {
        real_t d = s->npos[i] - s->n[i];
        if ((d > 1.0f && (s->n[i + 1] - s->n[i]) > 1.0f) ||
            (d < -1.0f && (s->n[i - 1] - s->n[i]) < -1.0f)) {
            int sign = (d > 0.0f) ? 1 : -1;
            real_t q_prev = s->q[i - 1];
            real_t q_curr = s->q[i];
            real_t q_next = s->q[i + 1];
            real_t n_prev = s->n[i - 1];
            real_t n_curr = s->n[i];
            real_t n_next = s->n[i + 1];

            real_t dd = sign * (n_next - n_prev);
            if (dd == 0.0f) continue;

            real_t numerator = (real_t)sign * (s->npos[i] - n_curr) / dd;
            real_t term1 = (n_next - n_curr) * (q_prev - q_curr) / (n_curr - n_prev);
            real_t term2 = (n_curr - n_prev) * (q_next - q_curr) / (n_next - n_curr);
            real_t q_new = q_curr + numerator * (term1 + term2);

            if (q_new > q_prev && q_new < q_next) {
                s->q[i] = q_new;
            } else if (sign > 0) {
                real_t step = (q_next - q_curr) / (n_next - n_curr);
                s->q[i] += step;
            } else {
                real_t step = (q_curr - q_prev) / (n_curr - n_prev);
                s->q[i] -= step;
            }

            s->n[i] += (real_t)sign;
        }
    }
}

static real_t p2_get_p10(const P2Quantile_t *s)
{
    if (!s->initialized) return 0.0f;
    return s->q[P2_DESIRED_P10];
}

static real_t p2_get_p50(const P2Quantile_t *s) __attribute__((unused));
static real_t p2_get_p50(const P2Quantile_t *s)
{
    if (!s->initialized) return 0.0f;
    return s->q[P2_DESIRED_P50];
}

static real_t p2_get_p90(const P2Quantile_t *s)
{
    if (!s->initialized) return 0.0f;
    return s->q[P2_DESIRED_P90];
}

static const real_t kSmoothAlpha   = 0.25f;
static const real_t kJumpAlpha     = 0.1f;
static const real_t kJumpThreshold = 40.0f;
static const real_t kEpsilon       = 1e-10f;

#define FAST_LOG_TABLE_SIZE 256
#define FAST_LOG_TABLE_SCALE ((float)(FAST_LOG_TABLE_SIZE - 1))

static const float s_fast_log_table[FAST_LOG_TABLE_SIZE] = {
    0.000000f, 0.003914f, 0.007813f, 0.011696f, 0.015565f, 0.019418f, 0.023257f, 0.027081f,
    0.030890f, 0.034686f, 0.038466f, 0.042233f, 0.045985f, 0.049723f, 0.053448f, 0.057158f,
    0.060855f, 0.064539f, 0.068208f, 0.071865f, 0.075508f, 0.079137f, 0.082754f, 0.086358f,
    0.089948f, 0.093526f, 0.097091f, 0.100644f, 0.104183f, 0.107711f, 0.111226f, 0.114728f,
    0.118219f, 0.121697f, 0.125163f, 0.128617f, 0.132060f, 0.135490f, 0.138909f, 0.142316f,
    0.145712f, 0.149096f, 0.152469f, 0.155830f, 0.159180f, 0.162519f, 0.165847f, 0.169163f,
    0.172469f, 0.175764f, 0.179048f, 0.182322f, 0.185584f, 0.188836f, 0.192078f, 0.195309f,
    0.198529f, 0.201740f, 0.204940f, 0.208129f, 0.211309f, 0.214479f, 0.217638f, 0.220788f,
    0.223928f, 0.227057f, 0.230178f, 0.233288f, 0.236389f, 0.239480f, 0.242562f, 0.245634f,
    0.248697f, 0.251750f, 0.254794f, 0.257829f, 0.260855f, 0.263871f, 0.266879f, 0.269877f,
    0.272867f, 0.275848f, 0.278819f, 0.281782f, 0.284737f, 0.287682f, 0.290619f, 0.293547f,
    0.296467f, 0.299378f, 0.302281f, 0.305175f, 0.308061f, 0.310939f, 0.313808f, 0.316670f,
    0.319523f, 0.322368f, 0.325205f, 0.328033f, 0.330854f, 0.333667f, 0.336472f, 0.339269f,
    0.342059f, 0.344840f, 0.347614f, 0.350381f, 0.353139f, 0.355890f, 0.358634f, 0.361370f,
    0.364098f, 0.366819f, 0.369533f, 0.372239f, 0.374939f, 0.377630f, 0.380315f, 0.382992f,
    0.385662f, 0.388326f, 0.390982f, 0.393631f, 0.396273f, 0.398908f, 0.401536f, 0.404157f,
    0.406771f, 0.409379f, 0.411980f, 0.414574f, 0.417161f, 0.419742f, 0.422316f, 0.424883f,
    0.427444f, 0.429998f, 0.432546f, 0.435087f, 0.437622f, 0.440151f, 0.442673f, 0.445188f,
    0.447698f, 0.450201f, 0.452698f, 0.455189f, 0.457673f, 0.460151f, 0.462624f, 0.465090f,
    0.467550f, 0.470004f, 0.472452f, 0.474894f, 0.477330f, 0.479760f, 0.482184f, 0.484602f,
    0.487015f, 0.489422f, 0.491823f, 0.494218f, 0.496607f, 0.498991f, 0.501369f, 0.503742f,
    0.506109f, 0.508470f, 0.510826f, 0.513176f, 0.515520f, 0.517860f, 0.520193f, 0.522522f,
    0.524845f, 0.527162f, 0.529474f, 0.531781f, 0.534082f, 0.536379f, 0.538670f, 0.540955f,
    0.543236f, 0.545511f, 0.547781f, 0.550046f, 0.552306f, 0.554561f, 0.556811f, 0.559055f,
    0.561295f, 0.563530f, 0.565759f, 0.567984f, 0.570204f, 0.572419f, 0.574629f, 0.576834f,
    0.579034f, 0.581229f, 0.583420f, 0.585606f, 0.587787f, 0.589963f, 0.592134f, 0.594301f,
    0.596464f, 0.598621f, 0.600774f, 0.602922f, 0.605066f, 0.607205f, 0.609339f, 0.611469f,
    0.613595f, 0.615715f, 0.617832f, 0.619944f, 0.622051f, 0.624154f, 0.626253f, 0.628347f,
    0.630437f, 0.632523f, 0.634604f, 0.636681f, 0.638753f, 0.640821f, 0.642885f, 0.644945f,
    0.647001f, 0.649052f, 0.651099f, 0.653142f, 0.655181f, 0.657215f, 0.659246f, 0.661272f,
    0.663294f, 0.665312f, 0.667326f, 0.669337f, 0.671343f, 0.673345f, 0.675343f, 0.677337f,
    0.679327f, 0.681313f, 0.683295f, 0.685273f, 0.687247f, 0.689218f, 0.691184f, 0.693147f,
};

real_t fast_logf(real_t x)
{
    if (x <= 0.0f) return -30.0f;

    union { float f; int32_t i; } u;
    u.f = x;
    int32_t raw = u.i;
    int32_t exponent = (raw >> 23) - 127;
    int32_t mantissa = raw & 0x007FFFFF;

    float m = (float)mantissa / 8388608.0f;

    float t = m * FAST_LOG_TABLE_SCALE;
    int32_t idx = (int32_t)t;
    if (idx < 0) idx = 0;
    if (idx >= FAST_LOG_TABLE_SIZE - 1) idx = FAST_LOG_TABLE_SIZE - 2;
    float frac = t - (float)idx;

    float log_m = s_fast_log_table[idx] + frac * (s_fast_log_table[idx + 1] - s_fast_log_table[idx]);

    return log_m + (float)exponent * 0.6931471805599453f;
}

static real_t integrate_band(const real_t *mags, int lo, int hi, real_t df, real_t norm)
{
    real_t sum = 0.0f;
    int i;
    for (i = lo; i < hi; i++) {
        real_t psd_i   = (mags[i] * mags[i]) / norm;
        real_t psd_ip1 = (mags[i + 1] * mags[i + 1]) / norm;
        sum += (psd_i + psd_ip1) * 0.5f * df;
    }
    return sum;
}

BandPowers_t compute_band_powers(const real_t *mags, int fft_size,
                                 int delta_lo, int delta_hi,
                                 int theta_lo, int theta_hi,
                                 int alpha_lo, int alpha_hi,
                                 int beta_lo,  int beta_hi,
                                 real_t norm_factor, real_t fs)
{
    BandPowers_t bp = {0, 0, 0, 0};
    if (delta_lo >= delta_hi || theta_lo >= theta_hi || alpha_lo >= alpha_hi || beta_lo >= beta_hi)
        return bp;

    real_t df = fs / (real_t)fft_size;
    bp.delta_power = integrate_band(mags, delta_lo, delta_hi, df, norm_factor);
    bp.theta_power = integrate_band(mags, theta_lo, theta_hi, df, norm_factor);
    bp.alpha_power = integrate_band(mags, alpha_lo, alpha_hi, df, norm_factor);
    bp.beta_power  = integrate_band(mags, beta_lo,  beta_hi,  df, norm_factor);
    return bp;
}

real_t compute_attention_feature(real_t delta, real_t theta, real_t alpha, real_t beta)
{
    real_t slow = alpha + theta;
    if (slow < kEpsilon) slow = kEpsilon;
    real_t ratio1 = beta / slow;
    if (ratio1 < kEpsilon) ratio1 = kEpsilon;

    if (alpha < kEpsilon) alpha = kEpsilon;
    real_t ratio2 = beta / alpha;
    if (ratio2 < kEpsilon) ratio2 = kEpsilon;

    if (theta < kEpsilon) theta = kEpsilon;
    real_t ratio3 = delta / theta;
    if (ratio3 < kEpsilon) ratio3 = kEpsilon;

    return 0.45f * fast_logf(ratio1) + 0.35f * fast_logf(ratio2) + 0.20f * fast_logf(ratio3);
}

real_t compute_relaxation_feature(real_t theta, real_t alpha, real_t beta)
{
    if (theta < kEpsilon) theta = kEpsilon;
    real_t ratio = alpha / theta;
    if (ratio < kEpsilon) ratio = kEpsilon;
    return fast_logf(ratio);
}

real_t compute_blink_feature(real_t blink_rate, real_t slow_blink_ratio)
{
    const real_t baseline_rate = 17.5f;
    real_t rate_feature = fast_logf((blink_rate + kEpsilon) / baseline_rate);
    return rate_feature + slow_blink_ratio * 2.0f;
}

real_t clampf(real_t v, real_t lo, real_t hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

real_t normalize_attention(real_t feature, AttnHistory_t *hist)
{
    hist->history[hist->idx] = feature;
    hist->idx = (hist->idx + 1) % ATT_HISTORY_SIZE;
    if (hist->count < ATT_HISTORY_SIZE) hist->count++;

    p2_update(&hist->p2, feature);

    if (hist->count < 10) return 50.0f;

    real_t low  = p2_get_p10(&hist->p2);
    real_t high = p2_get_p90(&hist->p2);
    if (high - low < 0.1f) return 50.0f;

    real_t score = (feature - low) / (high - low) * 100.0f;
    return clampf(score, 0.0f, 100.0f);
}

real_t normalize_feature_calibrated(real_t feature,
                                    real_t relax_baseline,
                                    real_t focus_baseline)
{
    real_t low  = relax_baseline;
    real_t high = focus_baseline;
    if (high - low < 0.1f) return 50.0f;
    real_t score = (feature - low) / (high - low) * 100.0f;
    return clampf(score, 0.0f, 100.0f);
}

real_t ema_smooth(real_t value, real_t smoothed, real_t alpha)
{
    return alpha * value + (1.0f - alpha) * smoothed;
}

static real_t smooth_with_jump(real_t new_val, real_t *smoothed_ptr)
{
    real_t diff = fabsf(new_val - *smoothed_ptr);
    real_t alpha = (diff > kJumpThreshold) ? kJumpAlpha : kSmoothAlpha;
    *smoothed_ptr = ema_smooth(new_val, *smoothed_ptr, alpha);
    return *smoothed_ptr;
}

void attention_engine_init(AttentionEngine_t *eng, const CalibrationConfig_t *calib)
{
    memset(eng, 0, sizeof(AttentionEngine_t));
    eng->smooth_alpha = kSmoothAlpha;
    eng->smoothed_attention = 50.0f;
    eng->smoothed_relaxation = 50.0f;
    eng->smoothed_blink = 50.0f;
    eng->last_valid_attention = 50.0f;
    eng->last_valid_relaxation = 50.0f;
    eng->last_valid_blink = 50.0f;
    if (calib) {
        eng->calibration = *calib;
    }
}

void attention_engine_reset(AttentionEngine_t *eng)
{
    CalibrationConfig_t calib = eng->calibration;
    attention_engine_init(eng, &calib);
}

void attention_engine_set_calibration(AttentionEngine_t *eng,
                                      const CalibrationConfig_t *calib)
{
    if (calib) {
        eng->calibration = *calib;
    } else {
        eng->calibration.has_value = 0;
    }
}

real_t attention_engine_compute_artifact_ratio(const real_t *raw_frame, int frame_len,
                                               real_t threshold_uv)
{
    int artifact_count = 0;
    int i;
    for (i = 0; i < frame_len; i++) {
        real_t v = fabsf(raw_frame[i]);
        if (v > threshold_uv) artifact_count++;
    }
    return (real_t)artifact_count / (real_t)frame_len;
}

AttentionOutput_t attention_engine_process(AttentionEngine_t *eng,
                                           const BandPowers_t *powers,
                                           real_t artifact_ratio,
                                           const BlinkStatistics_t *blink_stats,
                                           real_t bad_window_threshold,
                                           int bad_window_limit,
                                           real_t timestamp)
{
    AttentionOutput_t out;
    memset(&out, 0, sizeof(out));
    out.timestamp = timestamp;
    out.artifact_ratio = artifact_ratio;

    if (blink_stats) {
        out.blink_rate         = blink_stats->blink_rate;
        out.avg_blink_duration = blink_stats->avg_duration;
        out.slow_blink_ratio   = blink_stats->slow_blink_ratio;
        out.total_blink_count  = blink_stats->total_blinks;
        out.recent_blink_count = blink_stats->recent_count;
    }

    out.delta_power  = powers->delta_power;
    out.theta_power = powers->theta_power;
    out.alpha_power = powers->alpha_power;
    out.beta_power  = powers->beta_power;

    if (artifact_ratio > bad_window_threshold) {
        eng->consecutive_bad_windows++;
        if (eng->consecutive_bad_windows >= bad_window_limit) {
            out.quality = 2;
            out.attention_score = 50.0f;
            out.relaxation_score = 50.0f;
            out.blink_score = 50.0f;
            return out;
        }
        out.quality = 1;
        out.attention_score  = eng->last_valid_attention;
        out.relaxation_score = eng->last_valid_relaxation;
        out.blink_score      = eng->last_valid_blink;
        return out;
    }
    eng->consecutive_bad_windows = 0;

    real_t attn_feat  = compute_attention_feature(powers->delta_power,
                                                    powers->theta_power,
                                                    powers->alpha_power,
                                                    powers->beta_power);
    real_t relax_feat = compute_relaxation_feature(powers->theta_power,
                                                    powers->alpha_power,
                                                    powers->beta_power);

    real_t blink_feat = 0.0f;
    if (blink_stats) {
        blink_feat = compute_blink_feature(blink_stats->blink_rate,
                                           blink_stats->slow_blink_ratio);
    }

    real_t attn_score, relax_score, blink_score;

    if (eng->calibration.has_value) {
        attn_score = normalize_feature_calibrated(attn_feat,
                                                   eng->calibration.attention_relax_baseline,
                                                   eng->calibration.attention_focus_baseline);
        relax_score = normalize_feature_calibrated(relax_feat,
                                                    eng->calibration.relaxation_relax_baseline,
                                                    eng->calibration.relaxation_focus_baseline);
        blink_score = normalize_attention(blink_feat, &eng->blink_history);
        blink_score = 100.0f - blink_score;
    } else {
        attn_score  = normalize_attention(attn_feat, &eng->attn_history);
        relax_score = normalize_attention(relax_feat, &eng->relax_history);
        blink_score = normalize_attention(blink_feat, &eng->blink_history);
        blink_score = 100.0f - blink_score;
    }

    attn_score  = smooth_with_jump(attn_score, &eng->smoothed_attention);
    relax_score = smooth_with_jump(relax_score, &eng->smoothed_relaxation);
    blink_score = smooth_with_jump(blink_score, &eng->smoothed_blink);

    eng->last_valid_attention  = attn_score;
    eng->last_valid_relaxation = relax_score;
    eng->last_valid_blink      = blink_score;

    out.quality = 0;
    out.attention_score  = attn_score;
    out.relaxation_score = relax_score;
    out.blink_score      = blink_score;

    out.attention_confidence  = fabsf(attn_score - 50.0f) / 50.0f;
    out.relaxation_confidence = fabsf(relax_score - 50.0f) / 50.0f;
    if (out.attention_confidence  > 1.0f) out.attention_confidence  = 1.0f;
    if (out.relaxation_confidence > 1.0f) out.relaxation_confidence = 1.0f;

    return out;
}
