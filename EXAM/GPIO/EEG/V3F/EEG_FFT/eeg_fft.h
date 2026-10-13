#ifndef __EEG_FFT_H
#define __EEG_FFT_H

#include <stdint.h>
#include "eeg_filter.h"
#include "eeg_direction_feature.h"
#include "algo_core.h"

#define FFT_SIZE 256
#define SAMPLE_RATE 250.0f
#define STEP_SIZE 125
#ifndef NUM_CHANNELS
#define NUM_CHANNELS 8
#endif

#define SPECTRUM_TOTAL_FRAGS 32
#define SPECTRUM_FRAG_SIZE (128 / SPECTRUM_TOTAL_FRAGS)
#define SPECTRUM_FRAG_BYTES (SPECTRUM_FRAG_SIZE * sizeof(float))

#define STATE_RELAX 0
#define STATE_NEUTRAL 1
#define STATE_FOCUS 2

#define WAVE_FIXED_MAX 0.5f
#define SPECTRUM_Y_MAX 8.0f
#define TREND_WINDOW_SIZE 10

typedef struct {
    float CH0[FFT_SIZE];
    float CH1[FFT_SIZE];
    float CH2[FFT_SIZE];
    float CH3[FFT_SIZE];
    float CH4[FFT_SIZE];  // ���� CP3
    float CH5[FFT_SIZE];  // ���� CP4
    float CH6[FFT_SIZE];  // ���� C3
    float CH7[FFT_SIZE];  // ���� C4
    int WriteIdx;
} RingBuffer_t;

typedef struct {
    float Value;
    uint32_t Count;
} AttnEma_t;

typedef struct {
    float CH0[TREND_WINDOW_SIZE];
    float CH1[TREND_WINDOW_SIZE];
    int Idx;
    int Count;
} TrendWindow_t;

typedef struct {
    float CH0Mags[FFT_SIZE / 2 + 1];
    float CH1Mags[FFT_SIZE / 2 + 1];
    float CH2Mags[FFT_SIZE / 2 + 1];
    float CH3Mags[FFT_SIZE / 2 + 1];
    float CH4Mags[FFT_SIZE / 2 + 1];  // ����
    float CH5Mags[FFT_SIZE / 2 + 1];  // ����
    float CH6Mags[FFT_SIZE / 2 + 1];  // ����
    float CH7Mags[FFT_SIZE / 2 + 1];  // ����
    uint8_t SpectrumHeights[FFT_SIZE / 2 + 1];
} FFT_Data_t;

typedef struct {
    int DeltaStart, DeltaEnd;
    int ThetaStart, ThetaEnd;
    int AlphaStart, AlphaEnd;
    int BetaStart, BetaEnd;
} BandIndices_t;

typedef enum {
    FFT_STEP_START = 0,
    FFT_STEP_EXTRACT_RAW_FRAME,
    FFT_STEP_SEND_RAW_SPECTRUM,
    FFT_STEP_SPECTRUM_RAW,
    FFT_STEP_FREQ_FILTER,
    FFT_STEP_SEND_FREQ_SPECTRUM,
    FFT_STEP_BAND_POWER,
    FFT_STEP_ATTENTION,
    FFT_STEP_UPDATE_EMA,
    FFT_STEP_NORMALIZE,
    FFT_STEP_UPDATE_TREND,
    FFT_STEP_JUDGE_STATE,
    FFT_STEP_EXTRACT_FILT_FRAME,
    FFT_STEP_SPECTRUM_FILT,
    FFT_STEP_SEND_FILT_SPECTRUM,
    FFT_STEP_SEND_BUFFER,
    FFT_STEP_SEND_FOCUS,
    FFT_STEP_FFT_COPY_INPUT,
    FFT_STEP_FFT_TRANSFORM,
    FFT_STEP_FFT_MAGS,
    FFT_STEP_FINISH
} FFT_ProcState;

void EEG_FFT_Init(void);
uint8_t Process_FFT_Step(void);
void Process_FFT(void);
void Update_Waveform(float new_val);
void EEG_FFT_ResetInferState(void);
void EEG_FFT_ResetSendState(void);
void EEG_MI_ResultPoll(void);
void EEG_SSVEP_ResultPoll(void);
void EEG_Game_ResultPoll(void);

float *RingBuffer_GetChannel(RingBuffer_t *rb, uint8_t ch);
float *FFT_Data_GetMags(FFT_Data_t *data, uint8_t ch);

extern RingBuffer_t RingBuf;
extern RingBuffer_t RingBufFiltered;
extern FFT_Data_t FFT_Data;
extern FFT_Data_t FFT_DataFiltered;
extern BandIndices_t BandIdx;
extern float delta_pow_ch0, theta_pow_ch0, alpha_pow_ch0, beta_pow_ch0;
extern float delta_pow_ch1, theta_pow_ch1, alpha_pow_ch1, beta_pow_ch1;
extern float delta_pow_ch2, theta_pow_ch2, alpha_pow_ch2, beta_pow_ch2;
extern float delta_pow_ch3, theta_pow_ch3, alpha_pow_ch3, beta_pow_ch3;
extern float delta_pow_ch4, theta_pow_ch4, alpha_pow_ch4, beta_pow_ch4;
extern float delta_pow_ch5, theta_pow_ch5, alpha_pow_ch5, beta_pow_ch5;
extern float delta_pow_ch6, theta_pow_ch6, alpha_pow_ch6, beta_pow_ch6;
extern float delta_pow_ch7, theta_pow_ch7, alpha_pow_ch7, beta_pow_ch7;
extern float norm_factor;
extern AttnEma_t attn_ema[2];
extern AttnEma_t relax_ema[2];

extern uint8_t instant_state, trend_state;
extern int New_Samples_Count;

#endif