#include "eeg_filter.h"
#include <math.h>

#define AB_REALTIME_DRIFT_REMOVE_ENABLE  1
#define AB_DRIFT_K                       0.996f
#define AB_FFT_DETREND_ENABLE            1

#define WAVE_DISPLAY_REMOVE_BASELINE     1
#define WAVE_DISPLAY_BASELINE_ALPHA      0.005f

IIR_SOS_Coeff_t g_notch_coeff = {
    .sec = {
        {.b0 = 0.9794827610f, .b1 = -0.6053536377f, .b2 = 0.9794827610f, .a1 = -0.6053536377f, .a2 = 0.9589655220f},
        {.b0 = 0.9794827610f, .b1 = -0.6053536377f, .b2 = 0.9794827610f, .a1 = -0.6053536377f, .a2 = 0.9589655220f},
    },
    .num_sections = 2
};

IIR_SOS_Coeff_t g_bandpass_coeff = {
    .sec = {
        /* Butterworth 2-40 Hz band-pass, Fs=250 Hz (SciPy SOS convention). */
        {.b0 = 0.0193775563f, .b1 = 0.0387551126f, .b2 = 0.0193775563f, .a1 = -0.6832148491f, .a2 = 0.1547199697f},
        {.b0 = 1.0f,          .b1 = 2.0f,          .b2 = 1.0f,          .a1 = -0.8394389387f, .a2 = 0.5430653452f},
        {.b0 = 1.0f,          .b1 = -2.0f,         .b2 = 1.0f,          .a1 = -1.9028100905f, .a2 = 0.9056453513f},
        {.b0 = 1.0f,          .b1 = -2.0f,         .b2 = 1.0f,          .a1 = -1.9622176956f, .a2 = 0.9647572069f},
    },
    .num_sections = 4
};

IIR_SOS_State_t g_notch_state[NUM_CHANNELS] = {0};
IIR_SOS_State_t g_bandpass_state[NUM_CHANNELS] = {0};

EEG_DriftRemove_t AB_Drift_CH0 = {0.0f, 0};
EEG_DriftRemove_t AB_Drift_CH1 = {0.0f, 0};
EEG_DriftRemove_t AB_Drift_CH2 = {0.0f, 0};
EEG_DriftRemove_t AB_Drift_CH3 = {0.0f, 0};
EEG_DriftRemove_t AB_Drift_CH4 = {0.0f, 0};
EEG_DriftRemove_t AB_Drift_CH5 = {0.0f, 0};
EEG_DriftRemove_t AB_Drift_CH6 = {0.0f, 0};
EEG_DriftRemove_t AB_Drift_CH7 = {0.0f, 0};

static float wave_display_base_ch0 = 0.0f;
static float wave_display_base_ch1 = 0.0f;
static uint8_t wave_display_base_ready = 0;

static float wave_display_base_8ch[NUM_CHANNELS] = {0};
static uint8_t wave_display_base_8ch_ready = 0;

float IIR_Step(float input, const IIR_Coeff_t *coeff, IIR_State_t *state)
{
    float wn = input - coeff->a1 * state->w1 - coeff->a2 * state->w2;
    float out = coeff->b0 * wn + coeff->b1 * state->w1 + coeff->b2 * state->w2;
    state->w2 = state->w1;
    state->w1 = wn;
    return out;
}

float IIR_SOS_Step(float input, const IIR_SOS_Coeff_t *coeff, IIR_SOS_State_t *state)
{
    float tmp = input;
    uint8_t i;
#pragma GCC unroll 4
    for (i = 0; i < coeff->num_sections; i++) {
        tmp = IIR_Step(tmp, &coeff->sec[i], &state->sec[i]);
    }
    return tmp;
}

float EEG_RemoveRealtimeDrift(float x, EEG_DriftRemove_t *st)
{
#if AB_REALTIME_DRIFT_REMOVE_ENABLE
    float y;

    if (!st->init) {
        st->drift_base = x;
        st->init = 1;
    }

    y = x - st->drift_base;
    st->drift_base = AB_DRIFT_K * st->drift_base + (1.0f - AB_DRIFT_K) * x;

    return y;
#else
    (void)st;
    return x;
#endif
}

float EEG_RemoveMeanAndLinearTrend(float sample, float mean, float slope, int n)
{
#if AB_FFT_DETREND_ENABLE
    const float center = ((float)EEG_FILTER_FFT_SIZE - 1.0f) * 0.5f;
    float x = (float)n - center;
    return sample - (mean + slope * x);
#else
    (void)mean;
    (void)slope;
    (void)n;
    return sample;
#endif
}

void EEG_CalcMeanAndSlope(float *src, uint16_t start, float *out_mean, float *out_slope)
{
#if AB_FFT_DETREND_ENABLE
    float sum_y = 0.0f;
    float sum_xy = 0.0f;
    const float center = ((float)EEG_FILTER_FFT_SIZE - 1.0f) * 0.5f;
    const float sum_x2 = ((float)EEG_FILTER_FFT_SIZE * ((float)EEG_FILTER_FFT_SIZE * (float)EEG_FILTER_FFT_SIZE - 1.0f)) / 12.0f;
    int j;

    for (j = 0; j < EEG_FILTER_FFT_SIZE; j++) {
        uint16_t idx = (start + j) & (EEG_FILTER_FFT_SIZE - 1);
        float y = src[idx];
        float x = (float)j - center;
        sum_y += y;
        sum_xy += x * y;
    }

    *out_mean = sum_y / (float)EEG_FILTER_FFT_SIZE;
    *out_slope = sum_xy / sum_x2;
#else
    (void)src;
    (void)start;
    *out_mean = 0.0f;
    *out_slope = 0.0f;
#endif
}

void Waveform_RemoveDisplayBaseline(float in_ch0, float in_ch1, float *out_ch0, float *out_ch1)
{
#if WAVE_DISPLAY_REMOVE_BASELINE
    if (!wave_display_base_ready) {
        wave_display_base_ch0 = in_ch0;
        wave_display_base_ch1 = in_ch1;
        wave_display_base_ready = 1;
    }

    wave_display_base_ch0 += WAVE_DISPLAY_BASELINE_ALPHA * (in_ch0 - wave_display_base_ch0);
    wave_display_base_ch1 += WAVE_DISPLAY_BASELINE_ALPHA * (in_ch1 - wave_display_base_ch1);

    *out_ch0 = in_ch0 - wave_display_base_ch0;
    *out_ch1 = in_ch1 - wave_display_base_ch1;
#else
    *out_ch0 = in_ch0;
    *out_ch1 = in_ch1;
#endif
}

void Waveform_RemoveDisplayBaseline_8CH(const float *in_arr, float *out_arr)
{
#if WAVE_DISPLAY_REMOVE_BASELINE
    int i;
    if (!wave_display_base_8ch_ready) {
        for (i = 0; i < NUM_CHANNELS; i++) {
            wave_display_base_8ch[i] = in_arr[i];
        }
        wave_display_base_8ch_ready = 1;
    }
    for (i = 0; i < NUM_CHANNELS; i++) {
        wave_display_base_8ch[i] += WAVE_DISPLAY_BASELINE_ALPHA * (in_arr[i] - wave_display_base_8ch[i]);
        out_arr[i] = in_arr[i] - wave_display_base_8ch[i];
    }
#else
    int i;
    for (i = 0; i < NUM_CHANNELS; i++) {
        out_arr[i] = in_arr[i];
    }
#endif
}
