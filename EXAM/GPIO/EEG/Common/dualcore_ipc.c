#include "dualcore_ipc.h"
#include "dualcore_ipc_shared.h"
#include <string.h>

uint16_t DualCore_IPC_Checksum16(const volatile uint8_t *buf, uint16_t len)
{
    uint32_t sum = 0;
    uint16_t i;

    for (i = 0; i < len; i++) {
        sum += buf[i];
    }

    return (uint16_t)(sum & 0xFFFFu);
}

int32_t DualCore_ADS1299_SignExtend24(uint32_t raw24)
{
    raw24 &= 0x00FFFFFFUL;
    if ((raw24 & 0x00800000UL) != 0U) {
        raw24 |= 0xFF000000UL;
    }
    return (int32_t)raw24;
}

void DualCore_ADS1299_ParseRawFrame(const volatile uint8_t *frame,
                                     uint32_t *status,
                                     int32_t ch_data[DUALCORE_ADS1299_CHANNEL_NUM])
{
    uint8_t i;
    uint8_t index;
    uint32_t raw24;

    if ((frame == 0) || (ch_data == 0)) {
        return;
    }

    if (status != 0) {
        *status = ((uint32_t)frame[0] << 16) |
                  ((uint32_t)frame[1] << 8)  |
                  ((uint32_t)frame[2]);
    }

    for (i = 0; i < DUALCORE_ADS1299_CHANNEL_NUM; i++) {
        index = (uint8_t)(DUALCORE_ADS1299_STATUS_BYTES + i * DUALCORE_ADS1299_CH_BYTES);
        raw24 = ((uint32_t)frame[index] << 16) |
                ((uint32_t)frame[index + 1] << 8) |
                ((uint32_t)frame[index + 2]);
        ch_data[i] = DualCore_ADS1299_SignExtend24(raw24);
    }
}

int32_t DualCore_ADS1299_CodeToMicroVoltX1000(int32_t code)
{
    int64_t numerator;
    int64_t denominator;

    numerator = (int64_t)code * DUALCORE_ADS1299_VREF_UV * 1000LL;
    denominator = DUALCORE_ADS1299_GAIN * DUALCORE_ADS1299_FULL_SCALE;

    if (numerator >= 0) {
        numerator += denominator / 2;
    } else {
        numerator -= denominator / 2;
    }

    return (int32_t)(numerator / denominator);
}

void IPC_Config(IPC_Channel_TypeDef IPC_CHx, IPC_TxCID_TypeDef IPC_TxCIDx, IPC_RxCID_TypeDef IPC_RxCIDx)
{
    IPC_InitTypeDef IPC_InitStructure = {0};

    IPC_InitStructure.IPC_CH = IPC_CHx;
    IPC_InitStructure.TxCID  = IPC_TxCIDx;
    IPC_InitStructure.RxCID  = IPC_RxCIDx;
    IPC_InitStructure.TxIER  = ENABLE;
    IPC_InitStructure.RxIER  = ENABLE;
    IPC_InitStructure.AutoEN = ENABLE;

    IPC_Init(&IPC_InitStructure);
}
