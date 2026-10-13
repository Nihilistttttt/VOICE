#ifndef __DUALCORE_V5F_SSVEP_H
#define __DUALCORE_V5F_SSVEP_H

#include "dualcore_ipc_shared.h"

#if defined(Core_V5F)

#define SSVEP_NUM_CH                2u
#define SSVEP_WINDOW_SIZE           1250u
#define SSVEP_STEP_SIZE             500u
#define SSVEP_FS                    250.0f
#define SSVEP_HARMONICS             2u
#define SSVEP_NUM_TARGETS           4u
#define SSVEP_NUM_BANKS             4u

#define SSVEP_RATIO_THRESHOLD       1.25f
#define SSVEP_MARGIN_THRESHOLD      0.01f
#define SSVEP_MIN_SCORE             0.0f
#define SSVEP_REG                   1e-6f
#define SSVEP_NOTCH_FREQ            50.0f
#define SSVEP_NOTCH_RHO             0.98f
#define SSVEP_TUKEY_ALPHA           0.25f
#define SSVEP_CH_O1                 1u
#define SSVEP_CH_OZ                 0u
#define SSVEP_PI                    3.14159265358979323846f
#define SSVEP_CHANNEL_MIN_STD_MV    1e-7f
#define SSVEP_CHANNEL_MAX_STD_MV    100.0f
#define SSVEP_CHANNEL_MAX_PTP_MV    300.0f

void DualCore_V5F_SSVEP_Init(void);
void DualCore_V5F_SSVEP_Reset(void);
void DualCore_V5F_SSVEP_PushSample(float o1_mv, float oz_mv);
void DualCore_V5F_SSVEP_RunPending(void);

extern volatile uint8_t  g_ssvep_analysis_pending;

extern volatile uint8_t  g_ipc_ssvep_valid;
extern volatile int8_t   g_ipc_ssvep_raw_index;

extern volatile int32_t  g_ipc_ssvep_ratio_q10000;
extern volatile int32_t  g_ipc_ssvep_best_score_q10000;
extern volatile int32_t  g_ipc_ssvep_margin_q10000;
extern volatile int32_t  g_ipc_ssvep_scores_q10000[SSVEP_NUM_TARGETS];
extern volatile uint32_t g_ipc_ssvep_sequence;

#endif
#endif