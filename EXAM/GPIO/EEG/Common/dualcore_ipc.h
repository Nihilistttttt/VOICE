#ifndef __DUALCORE_IPC_H
#define __DUALCORE_IPC_H

#ifdef __cplusplus
 extern "C" {
#endif

#include <stdint.h>
#include "ch32h417.h"

#include "debug.h"
#include "dualcore_ipc_shared.h"
#include "dualcore_ipc_diag.h"

void IPC_Config(IPC_Channel_TypeDef IPC_CHx, IPC_TxCID_TypeDef IPC_TxCIDx, IPC_RxCID_TypeDef IPC_RxCIDx);

void DualCore_IPC_Init_V3F(void);
void DualCore_IPC_Init_V5F(void);
void DualCore_IPC_NotifyFromV3F(void);
void DualCore_IPC_SendFrameFromV3F(const uint8_t *frame, uint16_t len);
void DualCore_IPC_SetModelSelect(uint8_t model_select);
uint8_t DualCore_IPC_GetModelSelect(void);
void DualCore_IPC_RequestV5FReset(void);
void DualCore_IPC_RequestSsvepReset(void);
void DualCore_IPC_SetSsvepEnable(uint8_t enable);
uint8_t DualCore_IPC_GetSsvepEnable(void);
void DualCore_IPC_SetSsvepSelftest(uint8_t enable, uint8_t freq_index);
uint8_t DualCore_IPC_GetSsvepSelftest(void);
uint32_t DualCore_IPC_GetLastV5FWfiWake(void);
void DualCore_IPC_UpdateDirWeight(const uint8_t *payload, uint16_t len);
void DualCore_V5F_MainLoopProcess(void);

#ifdef __cplusplus
 }
#endif

#endif
