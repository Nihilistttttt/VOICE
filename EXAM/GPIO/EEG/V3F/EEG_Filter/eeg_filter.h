#ifndef __EEG_FILTER_H
#define __EEG_FILTER_H

#include <stdint.h>

#define MAX_SOS_SECTIONS 8
#define EEG_FILTER_FFT_SIZE 256
#define NUM_CHANNELS 8

typedef struct {
    float b0, b1, b2, a1, a2;
} IIR_Coeff_t;

typedef struct {
    float w1, w2;
} IIR_State_t;

typedef struct {
    IIR_Coeff_t sec[MAX_SOS_SECTIONS];
    uint8_t num_sections;
} IIR_SOS_Coeff_t;

typedef struct {
    IIR_State_t sec[MAX_SOS_SECTIONS];
} IIR_SOS_State_t;

typedef struct {
    float drift_base;
    uint8_t init;
} EEG_DriftRemove_t;

float IIR_Step(float input, const IIR_Coeff_t *coeff, IIR_State_t *state);
float IIR_SOS_Step(float input, const IIR_SOS_Coeff_t *coeff, IIR_SOS_State_t *state);

float EEG_RemoveRealtimeDrift(float x, EEG_DriftRemove_t *st);
float EEG_RemoveMeanAndLinearTrend(float sample, float mean, float slope, int n);
void EEG_CalcMeanAndSlope(float *src, uint16_t start, float *out_mean, float *out_slope);

void Waveform_RemoveDisplayBaseline(float in_ch0, float in_ch1, float *out_ch0, float *out_ch1);
void Waveform_RemoveDisplayBaseline_8CH(const float *in_arr, float *out_arr);

extern IIR_SOS_Coeff_t g_notch_coeff;
extern IIR_SOS_Coeff_t g_bandpass_coeff;
extern IIR_SOS_State_t g_notch_state[NUM_CHANNELS];
extern IIR_SOS_State_t g_bandpass_state[NUM_CHANNELS];
extern EEG_DriftRemove_t AB_Drift_CH0, AB_Drift_CH1, AB_Drift_CH2, AB_Drift_CH3, AB_Drift_CH4, AB_Drift_CH5, AB_Drift_CH6, AB_Drift_CH7;

#endif
