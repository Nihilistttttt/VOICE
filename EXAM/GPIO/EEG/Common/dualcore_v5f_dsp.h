#ifndef __DUALCORE_V5F_DSP_H
#define __DUALCORE_V5F_DSP_H

#include "dualcore_ipc_shared.h"

#if defined(Core_V5F)

void DualCore_V5F_DSP_Init(void);
void DualCore_V5F_DSP_Reset(void);
void DualCore_V5F_UpdateDirWeight(const volatile DualCore_IPC_DirWeight_t *src);

float DualCore_IIR_SOS_Step(float input, const DualCore_IIR_SOS_Coeff_t *coeff, DualCore_IIR_SOS_State_t *state);
float DualCore_RemoveRealtimeDrift(float x, DualCore_DriftRemove_t *st);
void DualCore_V5F_ComputeFFTFeature(void);

extern DualCore_DriftRemove_t g_v5f_drift[DUALCORE_ADS1299_ACTIVE_CH_NUM];
extern DualCore_IIR_SOS_Coeff_t g_v5f_notch_coeff;
extern DualCore_IIR_SOS_Coeff_t g_v5f_bandpass_coeff;
extern DualCore_IIR_SOS_Coeff_t g_v5f_csp_bandpass_coeff;
extern DualCore_IIR_SOS_State_t g_v5f_notch_state[DUALCORE_ADS1299_ACTIVE_CH_NUM];
extern DualCore_IIR_SOS_State_t g_v5f_bandpass_state[DUALCORE_ADS1299_ACTIVE_CH_NUM];
extern DualCore_IIR_SOS_State_t g_v5f_csp_bandpass_state[DUALCORE_ADS1299_ACTIVE_CH_NUM];

extern float    g_v5f_ring[DUALCORE_ADS1299_ACTIVE_CH_NUM][DUALCORE_V5F_FFT_SIZE];
extern float    g_v5f_csp_ring[DUALCORE_ADS1299_ACTIVE_CH_NUM][DUALCORE_V5F_FFT_SIZE];
extern uint16_t g_v5f_ring_write_idx;
extern uint16_t g_v5f_ring_valid_count;
extern uint16_t g_v5f_step_count;

extern volatile uint32_t g_ipc_v5f_fft_count;
extern volatile uint8_t  g_ipc_v5f_feature_valid;
extern volatile int32_t  g_ipc_v5f_feature_q[DUALCORE_V5F_FEATURE_DIM];
extern volatile uint8_t  g_ipc_v5f_infer_valid;
extern volatile uint8_t  g_ipc_v5f_pred;
extern volatile uint8_t  g_ipc_v5f_model_trained;
extern volatile uint8_t  g_ipc_v5f_model_used;
extern volatile uint8_t  g_ipc_v5f_model_select;
extern volatile int32_t  g_ipc_v5f_score_left;
extern volatile int32_t  g_ipc_v5f_score_right;
extern volatile int32_t  g_ipc_v5f_confidence;
extern volatile uint32_t g_ipc_v5f_infer_count;

#endif

#endif
