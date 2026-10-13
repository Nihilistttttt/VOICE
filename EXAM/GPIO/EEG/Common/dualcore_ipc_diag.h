#ifndef __DUALCORE_IPC_DIAG_H
#define __DUALCORE_IPC_DIAG_H

#include <stdint.h>

uint32_t DualCore_IPC_GetNotifyCount(void);
uint32_t DualCore_IPC_GetAckCount(void);
uint32_t DualCore_IPC_GetTxChecksum(void);
uint32_t DualCore_IPC_GetAckChecksum(void);
uint32_t DualCore_IPC_GetAckTxChecksum(void);
uint32_t DualCore_IPC_GetChecksumOKCount(void);
uint32_t DualCore_IPC_GetChecksumBadCount(void);
uint32_t DualCore_IPC_GetParseOKCount(void);
uint32_t DualCore_IPC_GetParseBadCount(void);

int32_t  DualCore_IPC_GetLastV3FCh0(void);
int32_t  DualCore_IPC_GetLastV5FCh0(void);
int32_t  DualCore_IPC_GetLastV5FCh0uVX1000(void);
int32_t  DualCore_IPC_GetLastV5FPre0uVX1000(void);
int32_t  DualCore_IPC_GetLastV5FFilt0uVX1000(void);
uint32_t DualCore_IPC_GetLastV5FSampleCount(void);
uint32_t DualCore_IPC_GetLastV5FWindowCount(void);
uint32_t DualCore_IPC_GetLastV5FFFTCount(void);
uint32_t DualCore_IPC_GetLastV5FFeatureValid(void);
int32_t  DualCore_IPC_GetLastV5FFeature(uint8_t idx);

/*
 * 一次性复制 V5F 最近生成的完整 24 维特征。
 * 复制期间只短暂关闭 IPC_CH0 中断，避免同一行数据混入相邻两个窗口。
 * 返回 1 表示复制成功；返回 0 表示 V5F 尚未生成有效特征。
 */
uint8_t DualCore_IPC_CopyLastV5FFeature(int32_t *out_feature,
                                        uint8_t feature_count,
                                        uint32_t *fft_count);

uint32_t DualCore_IPC_GetLastV5FInferValid(void);
uint32_t DualCore_IPC_GetLastV5FPred(void);
uint32_t DualCore_IPC_GetLastV5FModelTrained(void);
uint32_t DualCore_IPC_GetLastV5FModelUsed(void);
int32_t  DualCore_IPC_GetLastV5FScoreLeft(void);
int32_t  DualCore_IPC_GetLastV5FScoreRight(void);
int32_t  DualCore_IPC_GetLastV5FConfidence(void);
uint32_t DualCore_IPC_GetLastV5FInferCount(void);

uint32_t DualCore_IPC_GetV5FHandlerCount(void);

uint8_t  DualCore_IPC_GetLastSsvepValid(void);
int8_t   DualCore_IPC_GetLastSsvepRawIndex(void);
int8_t   DualCore_IPC_GetLastSsvepRawIndex(void);
int32_t  DualCore_IPC_GetLastSsvepRatioQ10000(void);
int32_t  DualCore_IPC_GetLastSsvepBestScoreQ10000(void);
int32_t  DualCore_IPC_GetLastSsvepMarginQ10000(void);
int32_t  DualCore_IPC_GetLastSsvepScoreQ10000(uint8_t idx);
uint32_t DualCore_IPC_GetLastSsvepSequence(void);

#endif