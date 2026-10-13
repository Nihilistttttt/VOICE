#include "eeg_protocol.h"

uint16_t Proto_Checksum16(const uint8_t *data, uint16_t len)
{
    uint16_t sum = 0;
    uint16_t i;
    for (i = 0; i < len; i++) {
        sum = (uint16_t)(sum + data[i]);
    }
    return sum;
}

uint32_t Proto_GetTimestampMs(void)
{
    extern volatile uint32_t g_icm42605_ms_tick;
    return g_icm42605_ms_tick;
}