#ifndef __IPC_LOG_H
#define __IPC_LOG_H

#include <stdint.h>
#include "ch32h417.h"

#define IPC_LOG_BUF_SIZE   512
#define IPC_LOG_SHARED_ADDR  0x2017F000UL

#define IPC_CMD_NONE         0
#define IPC_CMD_SSVEP        1
#define IPC_CMD_ARROW        2
#define IPC_CMD_ARROW_TRAIN  3
#define IPC_CMD_SOLID        4
#define IPC_CMD_MI_INFER     5
#define IPC_CMD_MI_RESULT    6
#define IPC_CMD_RESET        7
#define IPC_CMD_SSVEP_CTRL   8
#define IPC_CMD_COLLECT      9
#define IPC_CMD_GAME         10
#define IPC_CMD_FOCUS        11

#define IPC_CTRL_RESET_DSP      0x01u
#define IPC_CTRL_SSVEP_ENABLE   0x02u
#define IPC_CTRL_SSVEP_SELFTEST 0x08u

#define IPC_PRED_LEFT    0
#define IPC_PRED_RIGHT   1
#define IPC_PRED_UNKNOWN 2

#define IPC_FEATURE_DIM  24

typedef struct {
    volatile uint32_t write_pos;
    volatile uint32_t read_pos;
    volatile uint32_t v5f_status;
    volatile uint8_t  buf[IPC_LOG_BUF_SIZE];

    volatile uint32_t cmd;
    volatile uint32_t cmd_param;
    volatile uint32_t cmd_seq;
    volatile uint32_t cmd_ack;

    volatile uint32_t ctrl_flags;
    volatile uint8_t  model_select;
    volatile uint8_t  ssvep_selftest_idx;
    volatile uint8_t  v5f_pred;
    volatile uint8_t  v5f_model_trained;
    volatile int32_t  v5f_score_left;
    volatile int32_t  v5f_score_right;
    volatile int32_t  v5f_confidence;
    volatile uint32_t v5f_infer_count;

    volatile int32_t v3f_attn_q100;
    volatile int32_t v3f_pitch_deg10;
    volatile int32_t ssvep_scores_q10000[4];
    volatile int8_t   ssvep_raw_index;

    volatile int32_t  game_distance;
    volatile int32_t  game_coins;
    volatile int32_t  game_score;
    volatile uint32_t game_over_seq;

    volatile uint8_t  feature_valid;
    volatile int32_t  feature_q[IPC_FEATURE_DIM];
    volatile uint32_t feature_seq;
    volatile uint32_t sample_count;

    volatile uint32_t ssvep_send_us;
    volatile uint32_t ssvep_late_us;
    volatile uint32_t ssvep_frame_cnt;
    volatile uint8_t  ssvep_mode_active;
    volatile uint32_t ssvep_rp_us;
    volatile uint32_t ssvep_rp_count;
} IPC_Log_Shared_t;

#define IPC_LOG_SHARED  ((volatile IPC_Log_Shared_t *)IPC_LOG_SHARED_ADDR)

#define IPC_LOG_STATUS_IDLE       0
#define IPC_LOG_STATUS_USB_INIT   1
#define IPC_LOG_STATUS_USB_WAIT   2
#define IPC_LOG_STATUS_USB_READY  3
#define IPC_LOG_STATUS_SSVEP_RUN  4
#define IPC_LOG_STATUS_ARROW_RUN  5
#define IPC_LOG_STATUS_TRAIN_RUN  6
#define IPC_LOG_STATUS_ERROR      0xFF

#if defined(Core_V3F)
void IPC_Log_Init_V3F(void);
void IPC_Log_Poll_V3F(void);
void IPC_Cmd_Send_V3F(uint32_t cmd, uint32_t param);
void IPC_Ctrl_SetFlags_V3F(uint32_t flags);
uint8_t IPC_GetV5FPred_V3F(void);
int32_t IPC_GetV5FConfidence_V3F(void);
int32_t IPC_GetV5FScoreLeft_V3F(void);
int32_t IPC_GetV5FScoreRight_V3F(void);
int8_t IPC_GetSSVEPIndex_V3F(void);
uint8_t IPC_GetFeatureValid_V3F(void);
const volatile int32_t *IPC_GetFeatureV3F(void);
uint32_t IPC_GetFeatureSeq_V3F(void);
uint32_t IPC_GetSampleCount_V3F(void);
#endif

#if defined(Core_V5F)
void IPC_Log_Init_V5F(void);
void IPC_Log_Printf_V5F(const char *fmt, ...);
void IPC_Log_SetStatus_V5F(uint32_t status);
uint32_t IPC_Cmd_Recv_V5F(uint32_t *param);
uint32_t IPC_Ctrl_GetFlags_V5F(void);
void IPC_Ctrl_SetFlags_V5F(uint32_t flags);
void IPC_SetMIResult_V5F(uint8_t pred, int32_t sl, int32_t sr, int32_t conf);
void IPC_SetSSVEPResult_V5F(int8_t idx, const int32_t scores_q10000[4]);
void IPC_SetFeature_V5F(const int32_t feature_q[IPC_FEATURE_DIM]);
void IPC_IncSample_V5F(uint32_t n);
#endif

#endif
