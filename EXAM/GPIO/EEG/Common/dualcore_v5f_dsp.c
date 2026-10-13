#include "dualcore_v5f_dsp.h"
#include <math.h>
#include <string.h>

#if defined(Core_V5F)
#include "DualCore_Direction_Model_4ch_Weights.h"
#include "DualCore_CSP_Model_4ch_Weights.h"

volatile uint32_t g_ipc_v5f_fft_count = 0;
volatile uint8_t  g_ipc_v5f_feature_valid = 0;
volatile int32_t  g_ipc_v5f_feature_q[DUALCORE_V5F_FEATURE_DIM];
volatile uint8_t  g_ipc_v5f_infer_valid = 0;
volatile uint8_t  g_ipc_v5f_pred = DUALCORE_V5F_PRED_UNKNOWN;
volatile uint8_t  g_ipc_v5f_model_trained = 0;
volatile uint8_t  g_ipc_v5f_model_used = DUALCORE_V5F_MODEL_FFT24;
volatile uint8_t  g_ipc_v5f_model_select = DUALCORE_V5F_MODEL_SELECT_AUTO;
volatile int32_t  g_ipc_v5f_score_left = 0;
volatile int32_t  g_ipc_v5f_score_right = 0;
volatile int32_t  g_ipc_v5f_confidence = 0;
volatile uint32_t g_ipc_v5f_infer_count = 0;

#if DUALCORE_V5F_RESULT_FUSION_ENABLE
static int32_t  g_v5f_fusion_right_hist[DUALCORE_V5F_FUSION_ROWS];
static uint8_t  g_v5f_fusion_write_idx = 0;
static uint8_t  g_v5f_fusion_count = 0;
static uint8_t  g_v5f_fusion_ema_ready = 0;
static float    g_v5f_fusion_ema_right = 0.5f;
static uint8_t  g_v5f_fusion_last_pred = DUALCORE_V5F_PRED_UNKNOWN;
static int32_t  g_v5f_fft_prev_right_raw = 5000;
static uint8_t  g_v5f_fft_prev_ready = 0;
#endif

float g_v5f_ring[DUALCORE_ADS1299_ACTIVE_CH_NUM][DUALCORE_V5F_FFT_SIZE];
/* CSP is band-limited separately so the existing FFT24 fallback keeps the
 * preprocessing distribution used by its current trained weights. */
float g_v5f_csp_ring[DUALCORE_ADS1299_ACTIVE_CH_NUM][DUALCORE_V5F_FFT_SIZE];
static float g_v5f_hann[DUALCORE_V5F_FFT_SIZE];
static float g_v5f_goertzel_coeff[DUALCORE_V5F_MAX_BIN + 1u];
uint16_t g_v5f_ring_write_idx = 0;
uint16_t g_v5f_ring_valid_count = 0;
uint16_t g_v5f_step_count = 0;
static uint8_t  g_v5f_fft_cfg_ready = 0;

DualCore_DriftRemove_t g_v5f_drift[DUALCORE_ADS1299_ACTIVE_CH_NUM];
DualCore_IIR_SOS_Coeff_t g_v5f_notch_coeff = {
    .Sec = {
        {.b0 = 0.9794827610f, .b1 = -0.6053536377f, .b2 = 0.9794827610f, .a1 = -0.6053536377f, .a2 = 0.9589655220f},
        {.b0 = 0.9794827610f, .b1 = -0.6053536377f, .b2 = 0.9794827610f, .a1 = -0.6053536377f, .a2 = 0.9589655220f},
    },
    .NumSections = 2
};

DualCore_IIR_SOS_Coeff_t g_v5f_bandpass_coeff = {
    .Sec = {
        {.b0 = 0.93637848f, .b1 = -1.87275696f, .b2 = 0.93637848f, .a1 = -1.95979169f, .a2 = 0.96227012f},
        {.b0 = 1.0f, .b1 = -2.0f, .b2 = 1.0f, .a1 = -1.90886498f, .a2 = 0.91127901f},
    },
    .NumSections = 2
};

DualCore_IIR_SOS_State_t g_v5f_notch_state[DUALCORE_ADS1299_ACTIVE_CH_NUM] = {0};
DualCore_IIR_SOS_State_t g_v5f_bandpass_state[DUALCORE_ADS1299_ACTIVE_CH_NUM] = {0};
DualCore_IIR_SOS_Coeff_t g_v5f_csp_bandpass_coeff = {
    .Sec = {
        {.b0 = 0.0193775563f, .b1 = 0.0387551126f, .b2 = 0.0193775563f, .a1 = -0.6832148491f, .a2 = 0.1547199697f},
        {.b0 = 1.0f,          .b1 = 2.0f,          .b2 = 1.0f,          .a1 = -0.8394389387f, .a2 = 0.5430653452f},
        {.b0 = 1.0f,          .b1 = -2.0f,         .b2 = 1.0f,          .a1 = -1.9028100905f, .a2 = 0.9056453513f},
        {.b0 = 1.0f,          .b1 = -2.0f,         .b2 = 1.0f,          .a1 = -1.9622176956f, .a2 = 0.9647572069f},
    },
    .NumSections = 4
};
DualCore_IIR_SOS_State_t g_v5f_csp_bandpass_state[DUALCORE_ADS1299_ACTIVE_CH_NUM] = {0};

void DualCore_V5F_UpdateDirWeight(const volatile DualCore_IPC_DirWeight_t *src)
{
    uint8_t i;
    if (src == 0) {
        return;
    }
    DUALCORE_FENCE();
    for (i = 0; i < DUALCORE_DIR_WEIGHT_DIM; i++) {
        dir4ch_mean[i] = src->mean[i];
        dir4ch_scale[i] = src->scale[i];
        dir4ch_svm_weight[i] = src->weight[i];
    }
    dir4ch_svm_bias = src->bias;
    DUALCORE_FENCE();
    g_ipc_v5f_model_trained = 1u;
}

static float DualCore_IIR_Step(float input, const DualCore_IIR_Coeff_t *coeff, DualCore_IIR_State_t *state)
{
    float wn = input - coeff->a1 * state->w1 - coeff->a2 * state->w2;
    float out = coeff->b0 * wn + coeff->b1 * state->w1 + coeff->b2 * state->w2;

    state->w2 = state->w1;
    state->w1 = wn;

    return out;
}

float DualCore_IIR_SOS_Step(float input, const DualCore_IIR_SOS_Coeff_t *coeff, DualCore_IIR_SOS_State_t *state)
{
    float tmp = input;
    uint8_t i;

    for (i = 0; i < coeff->NumSections; i++) {
        tmp = DualCore_IIR_Step(tmp, &coeff->Sec[i], &state->Sec[i]);
    }

    return tmp;
}

float DualCore_RemoveRealtimeDrift(float x, DualCore_DriftRemove_t *st)
{
    float y;

    if (!st->init) {
        st->drift_base = x;
        st->init = 1;
    }

    y = x - st->drift_base;
    st->drift_base = DUALCORE_AB_DRIFT_K * st->drift_base + (1.0f - DUALCORE_AB_DRIFT_K) * x;

    return y;
}

void DualCore_InitOneNotch(DualCore_IIR_SOS_Coeff_t *c, DualCore_IIR_SOS_State_t *s)
{
    c->Sec[0].b0 = 0.9794827610f; c->Sec[0].b1 = -0.6053536377f; c->Sec[0].b2 = 0.9794827610f;
    c->Sec[0].a1 = -0.6053536377f; c->Sec[0].a2 = 0.9589655220f;
    c->Sec[1].b0 = 0.9794827610f; c->Sec[1].b1 = -0.6053536377f; c->Sec[1].b2 = 0.9794827610f;
    c->Sec[1].a1 = -0.6053536377f; c->Sec[1].a2 = 0.9589655220f;
    c->NumSections = 2;
    memset(s, 0, sizeof(*s));
}

void DualCore_InitOneBandpass(DualCore_IIR_SOS_Coeff_t *c, DualCore_IIR_SOS_State_t *s)
{
    c->Sec[0].b0 = 0.93637848f; c->Sec[0].b1 = -1.87275696f; c->Sec[0].b2 = 0.93637848f;
    c->Sec[0].a1 = -1.95979169f; c->Sec[0].a2 = 0.96227012f;
    c->Sec[1].b0 = 1.0f;        c->Sec[1].b1 = -2.0f;        c->Sec[1].b2 = 1.0f;
    c->Sec[1].a1 = -1.90886498f; c->Sec[1].a2 = 0.91127901f;
    c->NumSections = 2;
    memset(s, 0, sizeof(*s));
}

static void DualCore_InitOneCSPBandpass(DualCore_IIR_SOS_State_t *s)
{
    memset(s, 0, sizeof(*s));
}

static float DualCore_V5F_SafePower(float p)
{
    return (p < DUALCORE_V5F_POWER_EPS) ? DUALCORE_V5F_POWER_EPS : p;
}

#define V5F_FAST_LOG_TABLE_SIZE 256
#define V5F_FAST_LOG_TABLE_SCALE ((float)(V5F_FAST_LOG_TABLE_SIZE - 1))

static const float s_v5f_log_table[V5F_FAST_LOG_TABLE_SIZE] = {
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

static float DualCore_V5F_FastLogf(float x)
{
    if (x <= 0.0f) return -30.0f;

    union { float f; int32_t i; } u;
    u.f = x;
    int32_t raw = u.i;
    int32_t exponent = (raw >> 23) - 127;
    int32_t mantissa = raw & 0x007FFFFF;

    float m = (float)mantissa / 8388608.0f;

    float t = m * V5F_FAST_LOG_TABLE_SCALE;
    int32_t idx = (int32_t)t;
    if (idx < 0) idx = 0;
    if (idx >= V5F_FAST_LOG_TABLE_SIZE - 1) idx = V5F_FAST_LOG_TABLE_SIZE - 2;
    float frac = t - (float)idx;

    float log_m = s_v5f_log_table[idx] + frac * (s_v5f_log_table[idx + 1] - s_v5f_log_table[idx]);

    return log_m + (float)exponent * 0.6931471805599453f;
}

static float DualCore_V5F_ClampFloat(float x, float lo, float hi)
{
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

static float DualCore_V5F_LogRatio(float a, float b)
{
    float v = DualCore_V5F_FastLogf(DualCore_V5F_SafePower(a) / DualCore_V5F_SafePower(b));
    return DualCore_V5F_ClampFloat(v, -DUALCORE_V5F_LOG_CLAMP, DUALCORE_V5F_LOG_CLAMP);
}

static int32_t DualCore_V5F_FeatureToQ(float x)
{
    if (x >= 0.0f) return (int32_t)(x + 0.5f);
    return (int32_t)(x - 0.5f);
}

static float DualCore_V5F_Sigmoid(float x)
{
    if (x > 30.0f) return 1.0f;
    if (x < -30.0f) return 0.0f;
    return 1.0f / (1.0f + expf(-x));
}

static float DualCore_V5F_StdFeature(const volatile int32_t feature_q[DUALCORE_V5F_FEATURE_DIM], uint8_t i)
{
    float scale = dir4ch_scale[i];
    if ((scale < 1.0e-6f) && (scale > -1.0e-6f)) {
        scale = 1.0f;
    }
    return (((float)feature_q[i]) - dir4ch_mean[i]) / scale;
}

static float DualCore_V5F_FFTClassifierRightProbability(const volatile int32_t feature_q[DUALCORE_V5F_FEATURE_DIM])
{
    float margin = dir4ch_svm_bias;
    uint8_t i;

    for (i = 0; i < DUALCORE_V5F_FEATURE_DIM; i++) {
        margin += dir4ch_svm_weight[i] * DualCore_V5F_StdFeature(feature_q, i);
    }

    return DualCore_V5F_Sigmoid(margin * DUALCORE_V5F_CLASSIFIER_GAIN);
}

#if DUALCORE_V5F_CSP_ENABLE
static float DualCore_V5F_CSPStdFeature(float x, uint8_t i)
{
    float scale = csp4ch_scale[i];
    if ((scale < 1.0e-6f) && (scale > -1.0e-6f)) {
        scale = 1.0f;
    }
    return (x - csp4ch_mean[i]) / scale;
}

static float DualCore_V5F_CSPRightProbability(void)
{
    float sum[CSP4CH_FEATURE_DIM] = {0.0f, 0.0f, 0.0f, 0.0f};
    float sumsq[CSP4CH_FEATURE_DIM] = {0.0f, 0.0f, 0.0f, 0.0f};
    float var[CSP4CH_FEATURE_DIM];
    float feat[CSP4CH_FEATURE_DIM];
    float total_var = 0.0f;
    float margin = csp4ch_svm_bias;
    uint16_t start = g_v5f_ring_write_idx;
    uint16_t j;
    uint8_t k;
    uint8_t ch;

    for (j = 0; j < DUALCORE_V5F_FFT_SIZE; j++) {
        uint16_t idx = (uint16_t)((start + j) & (DUALCORE_V5F_FFT_SIZE - 1u));
        float x[DUALCORE_ADS1299_ACTIVE_CH_NUM];
        float y[CSP4CH_FEATURE_DIM];

        for (ch = 0; ch < DUALCORE_ADS1299_ACTIVE_CH_NUM; ch++) {
            x[ch] = g_v5f_csp_ring[ch][idx];
        }

        for (k = 0; k < CSP4CH_FEATURE_DIM; k++) {
            y[k] = 0.0f;
            for (ch = 0; ch < DUALCORE_ADS1299_ACTIVE_CH_NUM; ch++) {
                y[k] += csp4ch_matrix[k][ch] * x[ch];
            }
            sum[k] += y[k];
            sumsq[k] += y[k] * y[k];
        }
    }

    for (k = 0; k < CSP4CH_FEATURE_DIM; k++) {
        float mean = sum[k] / (float)DUALCORE_V5F_FFT_SIZE;
        var[k] = sumsq[k] / (float)DUALCORE_V5F_FFT_SIZE - mean * mean;
        if (var[k] < DUALCORE_V5F_POWER_EPS) {
            var[k] = DUALCORE_V5F_POWER_EPS;
        }
        total_var += var[k];
    }
    if (total_var < DUALCORE_V5F_POWER_EPS) {
        total_var = DUALCORE_V5F_POWER_EPS;
    }

    for (k = 0; k < CSP4CH_FEATURE_DIM; k++) {
        feat[k] = DualCore_V5F_FastLogf(var[k] / total_var);
        margin += csp4ch_svm_weight[k] * DualCore_V5F_CSPStdFeature(feat[k], k);
    }

    return DualCore_V5F_Sigmoid(margin);
}
#endif

static void DualCore_V5F_RunClassifier(const volatile int32_t feature_q[DUALCORE_V5F_FEATURE_DIM])
{
    float p_right;
    int32_t sr;
    int32_t sl;
    int32_t conf;

#if DUALCORE_V5F_CSP_ENABLE && DUALCORE_V5F_USE_CSP_WHEN_TRAINED
    if ((g_ipc_v5f_model_select != DUALCORE_V5F_MODEL_SELECT_FFT24) &&
        CSP4CH_WEIGHTS_TRAINED) {
        p_right = DualCore_V5F_CSPRightProbability();
        g_ipc_v5f_model_used = DUALCORE_V5F_MODEL_CSP;
    } else {
        p_right = DualCore_V5F_FFTClassifierRightProbability(feature_q);
        g_ipc_v5f_model_used = DUALCORE_V5F_MODEL_FFT24;
    }
#else
    p_right = DualCore_V5F_FFTClassifierRightProbability(feature_q);
    g_ipc_v5f_model_used = DUALCORE_V5F_MODEL_FFT24;
#endif

#if DUALCORE_V5F_RESULT_FUSION_ENABLE
    {
        int32_t right_raw;
        float probability = p_right;

        if (probability < 0.0f) probability = 0.0f;
        if (probability > 1.0f) probability = 1.0f;
        right_raw = (int32_t)(probability * 10000.0f + 0.5f);
        if (right_raw < 0) right_raw = 0;
        if (right_raw > 10000) right_raw = 10000;

        if (g_ipc_v5f_model_used == DUALCORE_V5F_MODEL_FFT24) {
            /*
             * FFT分支不重置任务、仍每125点更新；仅平均当前窗和上一窗。
             * 这样只保留约1秒证据，避免原EMA+4窗把上一方向拖入下一段。
             */
            if (g_v5f_fft_prev_ready) {
                sr = (right_raw + g_v5f_fft_prev_right_raw) / (int32_t)DUALCORE_V5F_FFT_FUSION_ROWS;
            } else {
                sr = right_raw;
                g_v5f_fft_prev_ready = 1u;
            }
            g_v5f_fft_prev_right_raw = right_raw;
        } else {
            float ema_right;
            int32_t right_sum = 0;
            uint8_t n;

            /* CSP分支完整保留原EMA 0.60 + 4窗历史。 */
            if (!g_v5f_fusion_ema_ready) {
                g_v5f_fusion_ema_right = p_right;
                g_v5f_fusion_ema_ready = 1u;
            } else {
                g_v5f_fusion_ema_right = DUALCORE_V5F_FUSION_EMA_ALPHA * g_v5f_fusion_ema_right +
                                         (1.0f - DUALCORE_V5F_FUSION_EMA_ALPHA) * p_right;
            }

            ema_right = g_v5f_fusion_ema_right;
            if (ema_right < 0.0f) ema_right = 0.0f;
            if (ema_right > 1.0f) ema_right = 1.0f;
            right_raw = (int32_t)(ema_right * 10000.0f + 0.5f);
            if (right_raw < 0) right_raw = 0;
            if (right_raw > 10000) right_raw = 10000;

            g_v5f_fusion_right_hist[g_v5f_fusion_write_idx] = right_raw;
            g_v5f_fusion_write_idx = (uint8_t)((g_v5f_fusion_write_idx + 1u) % DUALCORE_V5F_FUSION_ROWS);
            if (g_v5f_fusion_count < DUALCORE_V5F_FUSION_ROWS) {
                g_v5f_fusion_count++;
            }

            for (n = 0; n < g_v5f_fusion_count; n++) {
                right_sum += g_v5f_fusion_right_hist[n];
            }
            sr = (g_v5f_fusion_count > 0u) ? (right_sum / (int32_t)g_v5f_fusion_count) : right_raw;
        }
    }
#else
    sr = (int32_t)(p_right * 10000.0f + 0.5f);
#endif

    if (sr < 0) sr = 0;
    if (sr > 10000) sr = 10000;
    sl = 10000 - sr;
    conf = sl - sr;
    if (conf < 0) conf = -conf;

    if (conf < DUALCORE_V5F_UNKNOWN_CONF) {
        g_ipc_v5f_pred = DUALCORE_V5F_PRED_UNKNOWN;
    } else {
        g_ipc_v5f_pred = (sr >= sl) ? DUALCORE_V5F_PRED_RIGHT : DUALCORE_V5F_PRED_LEFT;
#if DUALCORE_V5F_RESULT_FUSION_ENABLE
        g_v5f_fusion_last_pred = g_ipc_v5f_pred;
#endif
    }

    g_ipc_v5f_score_left = sl;
    g_ipc_v5f_score_right = sr;
    g_ipc_v5f_confidence = conf;
#if DUALCORE_V5F_CSP_ENABLE && DUALCORE_V5F_USE_CSP_WHEN_TRAINED
    if (g_ipc_v5f_model_used == DUALCORE_V5F_MODEL_CSP) {
        g_ipc_v5f_model_trained = CSP4CH_WEIGHTS_TRAINED ? 1u : 0u;
    } else {
#ifdef DIR4CH_WEIGHTS_TRAINED
        g_ipc_v5f_model_trained = DIR4CH_WEIGHTS_TRAINED ? 1u : 0u;
#else
        g_ipc_v5f_model_trained = 1u;
#endif
    }
#else
#ifdef DIR4CH_WEIGHTS_TRAINED
    g_ipc_v5f_model_trained = DIR4CH_WEIGHTS_TRAINED ? 1u : 0u;
#else
    g_ipc_v5f_model_trained = 1u;
#endif
#endif
    g_ipc_v5f_infer_count++;
    g_ipc_v5f_infer_valid = 1u;
}

static void DualCore_V5F_InitFFTConfig(void)
{
    uint16_t i;
    uint16_t k;

    for (i = 0; i < DUALCORE_V5F_FFT_SIZE; i++) {
        float phase = (2.0f * DUALCORE_V5F_PI * (float)i) / ((float)DUALCORE_V5F_FFT_SIZE - 1.0f);
        float w = 0.5f - 0.5f * cosf(phase);
        g_v5f_hann[i] = w;
    }

    for (k = 0; k <= DUALCORE_V5F_MAX_BIN; k++) {
        float omega = (2.0f * DUALCORE_V5F_PI * (float)k) / (float)DUALCORE_V5F_FFT_SIZE;
        g_v5f_goertzel_coeff[k] = 2.0f * cosf(omega);
    }

    g_v5f_fft_cfg_ready = 1;
}

static void DualCore_V5F_CalcMeanSlope(const float *src, uint16_t start,
                                       float *out_mean, float *out_slope)
{
    float sum_y = 0.0f;
    float sum_xy = 0.0f;
    const float center = ((float)DUALCORE_V5F_FFT_SIZE - 1.0f) * 0.5f;
    const float sum_x2 = ((float)DUALCORE_V5F_FFT_SIZE *
                         ((float)DUALCORE_V5F_FFT_SIZE * (float)DUALCORE_V5F_FFT_SIZE - 1.0f)) / 12.0f;
    uint16_t j;

    for (j = 0; j < DUALCORE_V5F_FFT_SIZE; j++) {
        uint16_t idx = (uint16_t)((start + j) & (DUALCORE_V5F_FFT_SIZE - 1u));
        float y = src[idx];
        float x = (float)j - center;
        sum_y += y;
        sum_xy += x * y;
    }

    *out_mean = sum_y / (float)DUALCORE_V5F_FFT_SIZE;
    *out_slope = sum_xy / sum_x2;
}

static float DualCore_V5F_GoertzelPower(const float *src, uint16_t start,
                                        uint8_t bin, float mean, float slope)
{
    float q0 = 0.0f;
    float q1 = 0.0f;
    float q2 = 0.0f;
    float coeff = g_v5f_goertzel_coeff[bin];
    const float center = ((float)DUALCORE_V5F_FFT_SIZE - 1.0f) * 0.5f;
    uint16_t j;
    float power;

    for (j = 0; j < DUALCORE_V5F_FFT_SIZE; j++) {
        uint16_t idx = (uint16_t)((start + j) & (DUALCORE_V5F_FFT_SIZE - 1u));
        float x = (float)j - center;
        float sample = src[idx] - (mean + slope * x);
        sample *= g_v5f_hann[j];
        q0 = sample + coeff * q1 - q2;
        q2 = q1;
        q1 = q0;
    }

    power = q1 * q1 + q2 * q2 - coeff * q1 * q2;
    if (power < 0.0f) {
        power = 0.0f;
    }
    return power;
}

static float DualCore_V5F_GoertzelDiffPower(const float *src_a, const float *src_b,
                                            uint16_t start, uint8_t bin,
                                            float mean_a, float slope_a,
                                            float mean_b, float slope_b)
{
    float q0 = 0.0f;
    float q1 = 0.0f;
    float q2 = 0.0f;
    float coeff = g_v5f_goertzel_coeff[bin];
    const float center = ((float)DUALCORE_V5F_FFT_SIZE - 1.0f) * 0.5f;
    uint16_t j;
    float power;

    for (j = 0; j < DUALCORE_V5F_FFT_SIZE; j++) {
        uint16_t idx = (uint16_t)((start + j) & (DUALCORE_V5F_FFT_SIZE - 1u));
        float x = (float)j - center;
        float a = src_a[idx] - (mean_a + slope_a * x);
        float b = src_b[idx] - (mean_b + slope_b * x);
        float sample = (a - b) * g_v5f_hann[j];
        q0 = sample + coeff * q1 - q2;
        q2 = q1;
        q1 = q0;
    }

    power = q1 * q1 + q2 * q2 - coeff * q1 * q2;
    return (power > 0.0f) ? power : 0.0f;
}

static uint8_t DualCore_V5F_NarrowBandIndex(uint8_t bin)
{
    if (bin < 10u) return 0u; /* 8-10 Hz */
    if (bin < 12u) return 1u; /* 10-12 Hz */
    if (bin < 14u) return 2u; /* 12-14 Hz */
    if (bin < 18u) return 3u; /* 14-18 Hz */
    if (bin < 22u) return 4u; /* 18-22 Hz */
    if (bin < 26u) return 5u; /* 22-26 Hz */
    return 6u;                /* 26-31 Hz */
}

static void DualCore_V5F_BuildSpatialNarrowFeature(const float lap_power[2][7],
                                                   const float hemi_power[2][7],
                                      volatile int32_t feature_q[DUALCORE_V5F_FEATURE_DIM])
{
    float feature[DUALCORE_V5F_FEATURE_DIM];
    uint8_t i;

    for (i = 0; i < 7u; i++) {
        /* feat0..6: (CP3-C3)/(CP4-C4)；feat7..13: (CP3+C3)/(CP4+C4)。 */
        feature[i] = DualCore_V5F_LogRatio(lap_power[0][i], lap_power[1][i]) *
                     DUALCORE_V5F_FEATURE_SCALE;
        feature[7u + i] = DualCore_V5F_LogRatio(hemi_power[0][i], hemi_power[1][i]) *
                          DUALCORE_V5F_FEATURE_SCALE;
    }

    /* 协议继续携带24列；后10列固定为0且训练器明确忽略。 */
    for (i = DUALCORE_V5F_MODEL_FEATURE_DIM; i < DUALCORE_V5F_FEATURE_DIM; i++) {
        feature[i] = 0.0f;
    }

    for (i = 0; i < DUALCORE_V5F_FEATURE_DIM; i++) {
        feature_q[i] = DualCore_V5F_FeatureToQ(feature[i]);
    }
}

void DualCore_V5F_ComputeFFTFeature(void)
{
    uint8_t ch;
    uint8_t bin;
    uint8_t band;
    uint16_t start = g_v5f_ring_write_idx;
    float mean[DUALCORE_ADS1299_ACTIVE_CH_NUM];
    float slope[DUALCORE_ADS1299_ACTIVE_CH_NUM];
    float lap_power[2][7] = {{0.0f}};
    float hemi_power[2][7] = {{0.0f}};

    if (!g_v5f_fft_cfg_ready) {
        DualCore_V5F_InitFFTConfig();
    }

    for (ch = 0; ch < DUALCORE_ADS1299_ACTIVE_CH_NUM; ch++) {
        DualCore_V5F_CalcMeanSlope(g_v5f_ring[ch], start, &mean[ch], &slope[ch]);
    }

    for (bin = 8u; bin < 31u; bin++) {
        float channel_power[DUALCORE_ADS1299_ACTIVE_CH_NUM];
        float lap_left = DualCore_V5F_GoertzelDiffPower(
            g_v5f_ring[0], g_v5f_ring[2], start, bin,
            mean[0], slope[0], mean[2], slope[2]);
        float lap_right = DualCore_V5F_GoertzelDiffPower(
            g_v5f_ring[1], g_v5f_ring[3], start, bin,
            mean[1], slope[1], mean[3], slope[3]);
        float hemi_left;
        float hemi_right;

        for (ch = 0; ch < DUALCORE_ADS1299_ACTIVE_CH_NUM; ch++) {
            channel_power[ch] = DualCore_V5F_GoertzelPower(
                g_v5f_ring[ch], start, bin, mean[ch], slope[ch]);
        }
        hemi_left = 2.0f * (channel_power[0] + channel_power[2]) - lap_left;
        hemi_right = 2.0f * (channel_power[1] + channel_power[3]) - lap_right;

        if (hemi_left < 0.0f) hemi_left = 0.0f;
        if (hemi_right < 0.0f) hemi_right = 0.0f;
        band = DualCore_V5F_NarrowBandIndex(bin);
        lap_power[0][band] += lap_left;
        lap_power[1][band] += lap_right;
        hemi_power[0][band] += hemi_left;
        hemi_power[1][band] += hemi_right;
    }

    DualCore_V5F_BuildSpatialNarrowFeature(lap_power, hemi_power, g_ipc_v5f_feature_q);
    DualCore_V5F_RunClassifier(g_ipc_v5f_feature_q);

    g_ipc_v5f_fft_count++;
    g_ipc_v5f_feature_valid = 1;
}

void DualCore_V5F_DSP_Init(void)
{
    DualCore_V5F_InitFFTConfig();
}

void DualCore_V5F_DSP_Reset(void)
{
    uint8_t c;
    uint16_t i;

    if (!g_v5f_fft_cfg_ready) {
        DualCore_V5F_InitFFTConfig();
    }

    for (c = 0; c < DUALCORE_ADS1299_ACTIVE_CH_NUM; c++) {
        g_v5f_drift[c].drift_base = 0.0f;
        g_v5f_drift[c].init = 0;
        DualCore_InitOneNotch(&g_v5f_notch_coeff, &g_v5f_notch_state[c]);
        DualCore_InitOneBandpass(&g_v5f_bandpass_coeff, &g_v5f_bandpass_state[c]);
        DualCore_InitOneCSPBandpass(&g_v5f_csp_bandpass_state[c]);
        for (i = 0; i < DUALCORE_V5F_FFT_SIZE; i++) {
            g_v5f_ring[c][i] = 0.0f;
            g_v5f_csp_ring[c][i] = 0.0f;
        }
    }

    for (i = 0; i < DUALCORE_V5F_FEATURE_DIM; i++) {
        g_ipc_v5f_feature_q[i] = 0;
    }

    g_v5f_ring_write_idx = 0;
    g_v5f_ring_valid_count = 0;
    g_v5f_step_count = 0;
    g_ipc_v5f_fft_count = 0;
    g_ipc_v5f_feature_valid = 0;
    g_ipc_v5f_infer_valid = 0;
    g_ipc_v5f_pred = DUALCORE_V5F_PRED_UNKNOWN;
    g_ipc_v5f_model_trained = 0;
    g_ipc_v5f_model_used = DUALCORE_V5F_MODEL_FFT24;
    g_ipc_v5f_model_select = DUALCORE_V5F_MODEL_SELECT_AUTO;
    g_ipc_v5f_score_left = 0;
    g_ipc_v5f_score_right = 0;
    g_ipc_v5f_confidence = 0;
    g_ipc_v5f_infer_count = 0;

#if DUALCORE_V5F_RESULT_FUSION_ENABLE
    for (i = 0; i < DUALCORE_V5F_FUSION_ROWS; i++) {
        g_v5f_fusion_right_hist[i] = 5000;
    }
    g_v5f_fusion_write_idx = 0;
    g_v5f_fusion_count = 0;
    g_v5f_fusion_ema_ready = 0;
    g_v5f_fusion_ema_right = 0.5f;
    g_v5f_fusion_last_pred = DUALCORE_V5F_PRED_UNKNOWN;
    g_v5f_fft_prev_right_raw = 5000;
    g_v5f_fft_prev_ready = 0;
#endif
}

#endif
