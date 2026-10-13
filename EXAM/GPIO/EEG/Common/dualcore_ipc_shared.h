#ifndef __DUALCORE_IPC_SHARED_H
#define __DUALCORE_IPC_SHARED_H

#include <stdint.h>

#define DUALCORE_IPC_RUNTIME_ENABLE     1
#define DUALCORE_IPC_FRAME_LEN          27u
#define DUALCORE_IPC_FRAME_SLOT_NUM     128u
#define DUALCORE_IPC_TX_HISTORY_SIZE    256u

#define DUALCORE_ADS1299_STATUS_BYTES   3u
#define DUALCORE_ADS1299_CHANNEL_NUM    8u
#define DUALCORE_ADS1299_ACTIVE_CH_NUM  4u

/*
 * V5F MI channel mapping: V5F ring[0..3] → ADS1299 ch_data index
 * ring[0] = CP3 (ch_data[5]), ring[1] = CP4 (ch_data[2])
 * ring[2] = C3  (ch_data[6]), ring[3] = C4  (ch_data[0])
 * Laplacian: left=CP3-C3, right=CP4-C4
 * Note: F4/C4 physical channels swapped (C4=ch1, F4=ch0)
 */
#define V5F_CH_CP3   5u
#define V5F_CH_CP4   2u
#define V5F_CH_C3    6u
#define V5F_CH_C4   1u
static const uint8_t g_v5f_ch_map[DUALCORE_ADS1299_ACTIVE_CH_NUM] = {
    V5F_CH_CP3, V5F_CH_CP4, V5F_CH_C3, V5F_CH_C4
};
#define DUALCORE_ADS1299_CH_BYTES       3u
#define DUALCORE_ADS1299_FULL_SCALE     8388608LL
#define DUALCORE_ADS1299_VREF_UV        4500000LL
#define DUALCORE_ADS1299_GAIN           24LL

#define DUALCORE_V5F_WINDOW_SIZE        125u
#define DUALCORE_V5F_FFT_SIZE           256u
#define DUALCORE_V5F_STEP_SIZE          125u
#define DUALCORE_V5F_FS                 250.0f
#define DUALCORE_V5F_MAX_BIN            31u
#define DUALCORE_V5F_FEATURE_DIM        24u
#define DUALCORE_V5F_MODEL_FEATURE_DIM  14u
#define DUALCORE_V5F_CLASS_NUM          2u
#define DUALCORE_V5F_PRED_LEFT          0u
#define DUALCORE_V5F_PRED_RIGHT         1u
#define DUALCORE_V5F_PRED_UNKNOWN       2u
#define DUALCORE_V5F_MODEL_FFT24        0u
#define DUALCORE_V5F_MODEL_CSP          1u
#define DUALCORE_V5F_MODEL_SELECT_AUTO  0u
#define DUALCORE_V5F_MODEL_SELECT_FFT24 1u
#define DUALCORE_V5F_MODEL_SELECT_CSP   2u
#define DUALCORE_V5F_CLASSIFIER_GAIN    1.0f
#define DUALCORE_V5F_CSP_ENABLE          1u
#define DUALCORE_V5F_USE_CSP_WHEN_TRAINED 1u
#define DUALCORE_V5F_UNKNOWN_CONF        300
#define DUALCORE_V5F_RESULT_FUSION_ENABLE 1u
/* FFT24协议中仅前14维参与LDA；FFT分支连续两窗直接平均。 */
#define DUALCORE_V5F_FFT_FUSION_ROWS      2u
/* CSP分支保持原有EMA 0.60 + 4窗历史，不改变其既有行为。 */
#define DUALCORE_V5F_FUSION_ROWS          4u
#define DUALCORE_V5F_FUSION_EMA_ALPHA     0.60f
#define DUALCORE_V5F_KEEP_LAST_CONF       650
#define DUALCORE_V5F_FEATURE_SCALE      10000.0f
#define DUALCORE_V5F_LOG_CLAMP          6.0f
#define DUALCORE_V5F_POWER_EPS          1.0e-18f
#define DUALCORE_V5F_PI                 3.14159265358979323846f
#define DUALCORE_AB_DRIFT_K             0.996f

#define DUALCORE_FENCE()                __asm volatile ("fence iorw, iorw" ::: "memory")

#define DUALCORE_IPC_CTRL_RESET_DSP     0x01u
#define DUALCORE_IPC_CTRL_SSVEP_ENABLE  0x02u
#define DUALCORE_IPC_CTRL_RESET_SSVEP   0x04u
#define DUALCORE_IPC_CTRL_SSVEP_SELFTEST 0x08u
#define DUALCORE_IPC_CTRL_DIR_WEIGHT    0x10u

typedef struct
{
    float b0, b1, b2, a1, a2;
} DualCore_IIR_Coeff_t;

typedef struct
{
    float w1, w2;
} DualCore_IIR_State_t;

typedef struct
{
    DualCore_IIR_Coeff_t Sec[4];
    uint8_t NumSections;
} DualCore_IIR_SOS_Coeff_t;

typedef struct
{
    DualCore_IIR_State_t Sec[4];
} DualCore_IIR_SOS_State_t;

typedef struct
{
    float drift_base;
    uint8_t init;
} DualCore_DriftRemove_t;

typedef struct
{
    volatile uint32_t seq;
    volatile uint16_t len;
    volatile uint16_t checksum;
    volatile uint8_t  model_select;
    volatile uint8_t  control_reserved[3];
    volatile uint8_t  frame[DUALCORE_IPC_FRAME_LEN];

    volatile uint8_t  v5f_parse_valid;
    volatile uint32_t v5f_status;
    volatile uint32_t v5f_sample_count;
    volatile uint32_t v5f_window_count;
    volatile int32_t  v5f_ch_code[DUALCORE_ADS1299_ACTIVE_CH_NUM];
    volatile int32_t  v5f_uv_x1000[DUALCORE_ADS1299_ACTIVE_CH_NUM];
    volatile int32_t  v5f_pre_x1000[DUALCORE_ADS1299_ACTIVE_CH_NUM];
    volatile int32_t  v5f_filt_x1000[DUALCORE_ADS1299_ACTIVE_CH_NUM];
    volatile uint8_t  v5f_feature_valid;
    volatile uint32_t v5f_fft_count;
    volatile int32_t  v5f_feature_q[DUALCORE_V5F_FEATURE_DIM];
    volatile uint8_t  v5f_infer_valid;
    volatile uint8_t  v5f_pred;
    volatile uint8_t  v5f_model_trained;
    volatile uint8_t  v5f_model_used;
    volatile int32_t  v5f_score_left;
    volatile int32_t  v5f_score_right;
    volatile int32_t  v5f_confidence;
    volatile uint32_t v5f_infer_count;

    volatile uint8_t  ssvep_valid;
    volatile int8_t   ssvep_raw_index;

    volatile int32_t  ssvep_ratio_q10000;
    volatile int32_t  ssvep_best_score_q10000;
    volatile int32_t  ssvep_margin_q10000;
    volatile int32_t  ssvep_scores_q10000[4];
    volatile uint32_t ssvep_sequence;
    volatile int32_t  ssvep_o1_uv_x1000;
    volatile int32_t  ssvep_oz_uv_x1000;
} DualCore_IPC_FrameSlot_t;

#define DUALCORE_DIR_WEIGHT_DIM  24u
typedef struct {
    volatile uint8_t update_flag;
    volatile float   mean[DUALCORE_DIR_WEIGHT_DIM];
    volatile float   scale[DUALCORE_DIR_WEIGHT_DIM];
    volatile float   weight[DUALCORE_DIR_WEIGHT_DIM];
    volatile float   bias;
} DualCore_IPC_DirWeight_t;

uint16_t DualCore_IPC_Checksum16(const volatile uint8_t *buf, uint16_t len);
int32_t DualCore_ADS1299_SignExtend24(uint32_t raw24);
void DualCore_ADS1299_ParseRawFrame(const volatile uint8_t *frame,
                                     uint32_t *status,
                                     int32_t ch_data[DUALCORE_ADS1299_CHANNEL_NUM]);
int32_t DualCore_ADS1299_CodeToMicroVoltX1000(int32_t code);

#endif
