#ifndef ALGO_CORE_H
#define ALGO_CORE_H

#include <stdint.h>
#include <math.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ATT_HISTORY_SIZE 300

typedef float real_t;

typedef struct {
    real_t n[5];
    real_t npos[5];
    real_t q[5];
    int    count;
    int    initialized;
} P2Quantile_t;

typedef struct {
    real_t history[ATT_HISTORY_SIZE];
    int    idx;
    int    count;
    P2Quantile_t p2;
} AttnHistory_t;

typedef struct {
    real_t blink_rate;
    real_t avg_duration;
    real_t slow_blink_ratio;
    uint32_t total_blinks;
    uint32_t recent_count;
} BlinkStatistics_t;

typedef struct {
    real_t attention_relax_baseline;
    real_t attention_focus_baseline;
    real_t relaxation_relax_baseline;
    real_t relaxation_focus_baseline;
    real_t calibration_time;
    real_t quality_score;
    int    has_value;
} CalibrationConfig_t;

typedef struct {
    real_t smoothed_attention;
    real_t smoothed_relaxation;
    real_t smoothed_blink;
    real_t smooth_alpha;
    real_t last_valid_attention;
    real_t last_valid_relaxation;
    real_t last_valid_blink;
    int consecutive_bad_windows;
    AttnHistory_t attn_history;
    AttnHistory_t relax_history;
    AttnHistory_t blink_history;
    CalibrationConfig_t calibration;
} AttentionEngine_t;

typedef struct {
    real_t timestamp;
    real_t attention_score;
    real_t relaxation_score;
    real_t blink_score;
    real_t attention_confidence;
    real_t relaxation_confidence;
    int    quality;
    real_t delta_power;
    real_t theta_power;
    real_t alpha_power;
    real_t beta_power;
    real_t artifact_ratio;
    real_t blink_rate;
    real_t avg_blink_duration;
    real_t slow_blink_ratio;
    uint32_t total_blink_count;
    uint32_t recent_blink_count;
} AttentionOutput_t;

typedef struct {
    real_t delta_power;
    real_t theta_power;
    real_t alpha_power;
    real_t beta_power;
} BandPowers_t;

BandPowers_t compute_band_powers(const real_t *mags, int fft_size,
                                 int delta_lo, int delta_hi,
                                 int theta_lo, int theta_hi,
                                 int alpha_lo, int alpha_hi,
                                 int beta_lo,  int beta_hi,
                                 real_t norm_factor, real_t fs);

real_t compute_attention_feature(real_t delta, real_t theta, real_t alpha, real_t beta);
real_t compute_relaxation_feature(real_t theta, real_t alpha, real_t beta);
real_t compute_blink_feature(real_t blink_rate, real_t slow_blink_ratio);

real_t normalize_attention(real_t feature, AttnHistory_t *hist);
real_t normalize_feature_calibrated(real_t feature,
                                    real_t relax_baseline,
                                    real_t focus_baseline);

real_t ema_smooth(real_t value, real_t smoothed, real_t alpha);
real_t clampf(real_t v, real_t lo, real_t hi);

real_t fast_logf(real_t x);

void attention_engine_init(AttentionEngine_t *eng,
                           const CalibrationConfig_t *calib);
void attention_engine_reset(AttentionEngine_t *eng);
void attention_engine_set_calibration(AttentionEngine_t *eng,
                                      const CalibrationConfig_t *calib);

real_t attention_engine_compute_artifact_ratio(const real_t *raw_frame, int frame_len,
                                               real_t threshold_uv);

AttentionOutput_t attention_engine_process(AttentionEngine_t *eng,
                                           const BandPowers_t *powers,
                                           real_t artifact_ratio,
                                           const BlinkStatistics_t *blink_stats,
                                           real_t bad_window_threshold,
                                           int bad_window_limit,
                                           real_t timestamp);

#ifdef __cplusplus
}
#endif

#endif /* ALGO_CORE_H */
