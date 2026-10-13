#include "posture_detect.h"
#include <math.h>
#include <string.h>

#ifdef HAS_ICM42605

static PostureConfig_t s_cfg;
static float s_lpf_x;
static float s_lpf_y;
static float s_lpf_z;
static uint8_t s_lpf_init;
static PostureResult_t s_result;
static Posture_t s_candidate;
static uint32_t s_candidate_ms;

static const char *s_posture_names[POSTURE_COUNT] = {
    "UNKNOWN", "SUPINE", "PRONE", "LEFT", "RIGHT", "SITTING"
};

const char *Posture_ToString(Posture_t p)
{
    if (p >= POSTURE_COUNT) p = POSTURE_UNKNOWN;
    return s_posture_names[p];
}

void Posture_Init(const PostureConfig_t *cfg)
{
    if (cfg) {
        s_cfg = *cfg;
    } else {
        s_cfg.lpf_alpha           = 0.05f;
        s_cfg.supine_z_min        = 0.7f;
        s_cfg.prone_z_max         = -0.7f;
        s_cfg.side_x_abs_min      = 0.7f;
        s_cfg.sitting_y_min       = 0.5f;
        s_cfg.confidence_threshold= 0.5f;
        s_cfg.stable_confirm_ms   = 1500u;
    }

    s_lpf_x = 0.0f;
    s_lpf_y = 0.0f;
    s_lpf_z = 0.0f;
    s_lpf_init = 0u;

    memset(&s_result, 0, sizeof(s_result));
    s_result.posture = POSTURE_UNKNOWN;
    s_candidate = POSTURE_UNKNOWN;
    s_candidate_ms = 0u;
}

static float Posture_Absf(float x)
{
    return (x >= 0.0f) ? x : -x;
}

static Posture_t Posture_Classify(float gx, float gy, float gz, float *conf)
{
    float ax = Posture_Absf(gx);
    float ay = Posture_Absf(gy);
    float az = Posture_Absf(gz);

    *conf = 0.0f;

    if (gz >= s_cfg.supine_z_min) {
        *conf = gz;
        return POSTURE_SUPINE;
    }

    if (gz <= s_cfg.prone_z_max) {
        *conf = -gz;
        return POSTURE_PRONE;
    }

    if (gx >= s_cfg.side_x_abs_min) {
        *conf = gx;
        return POSTURE_LEFT_SIDE;
    }

    if (gx <= -s_cfg.side_x_abs_min) {
        *conf = -gx;
        return POSTURE_RIGHT_SIDE;
    }

    if (gy >= s_cfg.sitting_y_min) {
        *conf = gy;
        return POSTURE_SITTING;
    }

    if (az >= ax && az >= ay) {
        *conf = az;
        return (gz >= 0.0f) ? POSTURE_SUPINE : POSTURE_PRONE;
    }

    if (ax >= ay) {
        *conf = ax;
        return (gx >= 0.0f) ? POSTURE_LEFT_SIDE : POSTURE_RIGHT_SIDE;
    }

    *conf = ay;
    return POSTURE_SITTING;
}

void Posture_FeedAccel(float ax_g, float ay_g, float az_g, uint32_t dt_ms)
{
    float alpha = s_cfg.lpf_alpha;

    if (!s_lpf_init) {
        s_lpf_x = ax_g;
        s_lpf_y = ay_g;
        s_lpf_z = az_g;
        s_lpf_init = 1u;
    } else {
        s_lpf_x += alpha * (ax_g - s_lpf_x);
        s_lpf_y += alpha * (ay_g - s_lpf_y);
        s_lpf_z += alpha * (az_g - s_lpf_z);
    }

    s_result.gravity_x = s_lpf_x;
    s_result.gravity_y = s_lpf_y;
    s_result.gravity_z = s_lpf_z;

    float conf = 0.0f;
    Posture_t detected = Posture_Classify(s_lpf_x, s_lpf_y, s_lpf_z, &conf);
    s_result.confidence = conf;

    /* Motion or an invalid mounting vector must not be forced into a posture. */
    if (conf < s_cfg.confidence_threshold) {
        detected = POSTURE_UNKNOWN;
    }

    if (detected == s_candidate) {
        s_candidate_ms += dt_ms;
    } else {
        s_candidate = detected;
        s_candidate_ms = dt_ms;
    }

    if (s_candidate_ms >= s_cfg.stable_confirm_ms) {
        if (s_result.posture != s_candidate) {
            s_result.posture = s_candidate;
        }
        s_result.stable_ms = s_candidate_ms;
    } else {
        s_result.stable_ms = s_candidate_ms;
    }
}

const PostureResult_t *Posture_GetResult(void)
{
    return &s_result;
}


#endif /* HAS_ICM42605 */