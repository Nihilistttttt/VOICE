#include "dualcore_ipc_diag.h"
#include "dualcore_ipc_shared.h"
#include "ch32h417.h"

#if defined(Core_V3F)

extern volatile uint32_t g_ipc_v3f_notify_count;
extern volatile uint32_t g_ipc_v3f_ack_count;
extern volatile uint32_t g_ipc_v3f_tx_checksum;
extern volatile uint32_t g_ipc_v3f_ack_checksum;
extern volatile uint32_t g_ipc_v3f_ack_tx_checksum;
extern volatile uint32_t g_ipc_v3f_checksum_ok;
extern volatile uint32_t g_ipc_v3f_checksum_bad;
extern volatile uint32_t g_ipc_v3f_parse_ok;
extern volatile uint32_t g_ipc_v3f_parse_bad;

extern volatile int32_t  g_ipc_v3f_last_v3f_ch0;
extern volatile int32_t  g_ipc_v3f_last_v5f_ch0;
extern volatile int32_t  g_ipc_v3f_last_v5f_uv0_x1000;
extern volatile int32_t  g_ipc_v3f_last_v5f_pre0_x1000;
extern volatile int32_t  g_ipc_v3f_last_v5f_filt0_x1000;
extern volatile uint32_t g_ipc_v3f_last_v5f_sample_count;
extern volatile uint32_t g_ipc_v3f_last_v5f_window_count;
extern volatile uint32_t g_ipc_v3f_last_v5f_fft_count;
extern volatile uint32_t g_ipc_v3f_last_v5f_feature_valid;
extern volatile int32_t  g_ipc_v3f_last_v5f_feature_q[DUALCORE_V5F_FEATURE_DIM];
extern volatile uint32_t g_ipc_v3f_last_v5f_infer_valid;
extern volatile uint32_t g_ipc_v3f_last_v5f_pred;
extern volatile uint32_t g_ipc_v3f_last_v5f_model_trained;
extern volatile uint32_t g_ipc_v3f_last_v5f_model_used;
extern volatile int32_t  g_ipc_v3f_last_v5f_score_left;
extern volatile int32_t  g_ipc_v3f_last_v5f_score_right;
extern volatile int32_t  g_ipc_v3f_last_v5f_confidence;
extern volatile uint32_t g_ipc_v3f_last_v5f_infer_count;

extern volatile uint8_t  g_ipc_v3f_last_ssvep_valid;
extern volatile int8_t   g_ipc_v3f_last_ssvep_raw_index;

extern volatile int32_t  g_ipc_v3f_last_ssvep_ratio_q10000;
extern volatile int32_t  g_ipc_v3f_last_ssvep_best_score_q10000;
extern volatile int32_t  g_ipc_v3f_last_ssvep_margin_q10000;
extern volatile int32_t  g_ipc_v3f_last_ssvep_scores_q10000[4];
extern volatile uint32_t g_ipc_v3f_last_ssvep_sequence;

uint32_t DualCore_IPC_GetNotifyCount(void)       { return g_ipc_v3f_notify_count; }
uint32_t DualCore_IPC_GetAckCount(void)          { return g_ipc_v3f_ack_count; }
uint32_t DualCore_IPC_GetTxChecksum(void)        { return g_ipc_v3f_tx_checksum; }
uint32_t DualCore_IPC_GetAckChecksum(void)       { return g_ipc_v3f_ack_checksum; }
uint32_t DualCore_IPC_GetAckTxChecksum(void)     { return g_ipc_v3f_ack_tx_checksum; }
uint32_t DualCore_IPC_GetChecksumOKCount(void)   { return g_ipc_v3f_checksum_ok; }
uint32_t DualCore_IPC_GetChecksumBadCount(void)  { return g_ipc_v3f_checksum_bad; }
uint32_t DualCore_IPC_GetParseOKCount(void)      { return g_ipc_v3f_parse_ok; }
uint32_t DualCore_IPC_GetParseBadCount(void)     { return g_ipc_v3f_parse_bad; }

int32_t  DualCore_IPC_GetLastV3FCh0(void)        { return g_ipc_v3f_last_v3f_ch0; }
int32_t  DualCore_IPC_GetLastV5FCh0(void)        { return g_ipc_v3f_last_v5f_ch0; }
int32_t  DualCore_IPC_GetLastV5FCh0uVX1000(void) { return g_ipc_v3f_last_v5f_uv0_x1000; }
int32_t  DualCore_IPC_GetLastV5FPre0uVX1000(void){ return g_ipc_v3f_last_v5f_pre0_x1000; }
int32_t  DualCore_IPC_GetLastV5FFilt0uVX1000(void){ return g_ipc_v3f_last_v5f_filt0_x1000; }
uint32_t DualCore_IPC_GetLastV5FSampleCount(void){ return g_ipc_v3f_last_v5f_sample_count; }
uint32_t DualCore_IPC_GetLastV5FWindowCount(void){ return g_ipc_v3f_last_v5f_window_count; }
uint32_t DualCore_IPC_GetLastV5FFFTCount(void)   { return g_ipc_v3f_last_v5f_fft_count; }
uint32_t DualCore_IPC_GetLastV5FFeatureValid(void){ return g_ipc_v3f_last_v5f_feature_valid; }
int32_t  DualCore_IPC_GetLastV5FFeature(uint8_t idx)
{
    if (idx >= DUALCORE_V5F_FEATURE_DIM) {
        return 0;
    }
    return g_ipc_v3f_last_v5f_feature_q[idx];
}

uint8_t DualCore_IPC_CopyLastV5FFeature(int32_t *out_feature,
                                        uint8_t feature_count,
                                        uint32_t *fft_count)
{
    uint8_t i;

    if ((out_feature == 0) ||
        (feature_count < DUALCORE_V5F_FEATURE_DIM)) {
        return 0;
    }

    /*
     * V5F 的特征由 IPC 中断复制到 V3F 全局缓存。
     * 这里只在复制 24 个整数的短时间内屏蔽 IPC_CH0，
     * 防止复制过程中缓存被下一窗口更新。
     */
    NVIC_DisableIRQ(IPC_CH0_IRQn);

    if (g_ipc_v3f_last_v5f_feature_valid == 0u) {
        NVIC_EnableIRQ(IPC_CH0_IRQn);
        return 0;
    }

    for (i = 0; i < DUALCORE_V5F_FEATURE_DIM; i++) {
        out_feature[i] = g_ipc_v3f_last_v5f_feature_q[i];
    }
    if (fft_count != 0) {
        *fft_count = g_ipc_v3f_last_v5f_fft_count;
    }

    NVIC_EnableIRQ(IPC_CH0_IRQn);
    return 1;
}
uint32_t DualCore_IPC_GetLastV5FInferValid(void){ return g_ipc_v3f_last_v5f_infer_valid; }
uint32_t DualCore_IPC_GetLastV5FPred(void){ return g_ipc_v3f_last_v5f_pred; }
uint32_t DualCore_IPC_GetLastV5FModelTrained(void){ return g_ipc_v3f_last_v5f_model_trained; }
uint32_t DualCore_IPC_GetLastV5FModelUsed(void){ return g_ipc_v3f_last_v5f_model_used; }
int32_t  DualCore_IPC_GetLastV5FScoreLeft(void){ return g_ipc_v3f_last_v5f_score_left; }
int32_t  DualCore_IPC_GetLastV5FScoreRight(void){ return g_ipc_v3f_last_v5f_score_right; }
int32_t  DualCore_IPC_GetLastV5FConfidence(void){ return g_ipc_v3f_last_v5f_confidence; }
uint32_t DualCore_IPC_GetLastV5FInferCount(void){ return g_ipc_v3f_last_v5f_infer_count; }

uint32_t DualCore_IPC_GetV5FHandlerCount(void)
{
    return IPC_ReadMSG(IPC_MSG2);
}

uint8_t  DualCore_IPC_GetLastSsvepValid(void)          { return g_ipc_v3f_last_ssvep_valid; }
int8_t   DualCore_IPC_GetLastSsvepRawIndex(void)       { return g_ipc_v3f_last_ssvep_raw_index; }

int32_t  DualCore_IPC_GetLastSsvepRatioQ10000(void)    { return g_ipc_v3f_last_ssvep_ratio_q10000; }
int32_t  DualCore_IPC_GetLastSsvepBestScoreQ10000(void){ return g_ipc_v3f_last_ssvep_best_score_q10000; }
int32_t  DualCore_IPC_GetLastSsvepMarginQ10000(void)   { return g_ipc_v3f_last_ssvep_margin_q10000; }
int32_t  DualCore_IPC_GetLastSsvepScoreQ10000(uint8_t idx)
{
    if (idx >= 4) return 0;
    return g_ipc_v3f_last_ssvep_scores_q10000[idx];
}
uint32_t DualCore_IPC_GetLastSsvepSequence(void)       { return g_ipc_v3f_last_ssvep_sequence; }

#endif