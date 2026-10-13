#ifndef WATCH_MESSAGE_PARSER_H
#define WATCH_MESSAGE_PARSER_H

#include "ch32h417_usb.h"
#include "Serial.h"
#include "eeg_protocol.h"
#include <string.h>

#define TX_MESSAGE_BUF_SIZE    (2 * 1032 + 64)

#define DISPLAY_NUM_CH       8
#define DISPLAY_MAX_CH       8

typedef enum {
    WAVE_TYPE_RAW      = 0,
    WAVE_TYPE_FILT     = 1,
    WAVE_TYPE_BASELINE = 2,
    WAVE_TYPE_NONE     = 0xFF
} WaveType_t;

typedef enum {
    SPEC_TYPE_RAW          = 0,
    SPEC_TYPE_FREQ_FILTER  = 1,
    SPEC_TYPE_TIME_FILTER  = 2,
    SPEC_TYPE_NONE         = 0xFF
} SpecType_t;

typedef struct {
    uint8_t wave_ch[DISPLAY_NUM_CH];
    uint8_t wave_type[DISPLAY_NUM_CH];
    uint8_t spec_type[DISPLAY_NUM_CH];
} DisplayConfig_t;

void DisplayConfig_SetDefaults(DisplayConfig_t *cfg);
uint8_t DisplayConfig_GetWaveCmd(uint8_t wave_type);
uint8_t DisplayConfig_GetSpectrumCmd(uint8_t spec_type, uint8_t ch);
uint8_t DisplayConfig_NeedsBandpass(uint8_t ch);
uint8_t DisplayConfig_NeedsRawFFT(uint8_t ch);
uint8_t DisplayConfig_NeedsFiltFFT(uint8_t ch);
uint8_t DisplayConfig_IsAllNone(void);

extern DisplayConfig_t g_display_config;

void Parse_Serial_Data(Serial_Port port);

void Pack_Frame(Serial_Port port, uint8_t cmd, const uint8_t *payload, uint16_t len);
void Send_WaveformBatch(uint8_t wave_type, const float vals[DISPLAY_MAX_CH]);
void Send_Spectrum(uint8_t ch, uint8_t spectrum_type, float *mag, uint8_t frag_idx);
void Send_Focus(float attn0, float attn1, float ema0, float ema1,
                uint8_t trend_state, uint8_t instant_state);
void Send_RespCommand(uint8_t cmd, const uint8_t *payload, uint16_t len);

#endif
