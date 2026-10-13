#include "max98357a.h"
#include "Config.h"
#include "hal_gpio.h"
#include "ch32h417.h"
#include "debug.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define MAX98357A_SHDN_LOW()     Hal_GPIO_Write(MAX98357A_SHDN_PIN_ENC, 0)
#define MAX98357A_SHDN_HIGH()    Hal_GPIO_Write(MAX98357A_SHDN_PIN_ENC, 1)
#define MAX98357A_GAIN_LOW()     Hal_GPIO_Write(MAX98357A_GAIN_PIN_ENC, 0)
#define MAX98357A_GAIN_HIGH()    Hal_GPIO_Write(MAX98357A_GAIN_PIN_ENC, 1)

static int16_t g_tone_buf[MAX98357A_TONE_BUF_SIZE];
static volatile int  g_playing = 0;
static uint32_t      g_loop_len = 0;
static max98357a_dma_cb_t g_stream_cb = 0;

static void max98357a_gpio_init(void) {
    AFIO_ClockEnable();
    GPIO_ClockEnable(MAX98357A_WS_PORT);
    GPIO_ClockEnable(MAX98357A_CK_PORT);
    GPIO_ClockEnable(MAX98357A_DIN_PORT);
    GPIO_ClockEnable(MAX98357A_GAIN_PORT);
    GPIO_ClockEnable(MAX98357A_SHDN_PORT);

    Hal_GPIO_Init(MAX98357A_WS_PIN_ENC,  HAL_GPIO_MODE_AF_PP,    HAL_GPIO_SPEED_VERY_HIGH, MAX98357A_WS_AF);
    Hal_GPIO_Init(MAX98357A_CK_PIN_ENC,  HAL_GPIO_MODE_AF_PP,    HAL_GPIO_SPEED_VERY_HIGH, MAX98357A_CK_AF);
    Hal_GPIO_Init(MAX98357A_DIN_PIN_ENC, HAL_GPIO_MODE_AF_PP,    HAL_GPIO_SPEED_VERY_HIGH, MAX98357A_DIN_AF);
    Hal_GPIO_Init(MAX98357A_GAIN_PIN_ENC, HAL_GPIO_MODE_OUTPUT_PP, HAL_GPIO_SPEED_LOW, 0);
    Hal_GPIO_Init(MAX98357A_SHDN_PIN_ENC, HAL_GPIO_MODE_OUTPUT_PP, HAL_GPIO_SPEED_LOW, 0);
    MAX98357A_SHDN_LOW();
}

static void max98357a_i2s_init(void) {
    SPI_ClockEnable(MAX98357A_I2S_INSTANCE);
    SPI_I2S_DeInit(MAX98357A_I2S_INSTANCE);

    I2S_InitTypeDef I2S_InitStructure = {0};
    I2S_InitStructure.I2S_Mode       = I2S_Mode_MasterTx;
    I2S_InitStructure.I2S_Standard   = I2S_Standard_Phillips;
    I2S_InitStructure.I2S_DataFormat = I2S_DataFormat_16b;
    I2S_InitStructure.I2S_MCLKOutput = I2S_MCLKOutput_Disable;
    I2S_InitStructure.I2S_AudioFreq  = I2S_AudioFreq_48k;
    I2S_InitStructure.I2S_CPOL       = I2S_CPOL_High;
    I2S_Init(MAX98357A_I2S_INSTANCE, &I2S_InitStructure);

    SPI_I2S_DMACmd(MAX98357A_I2S_INSTANCE, SPI_I2S_DMAReq_Tx, ENABLE);
}

static void max98357a_dma_init(const int16_t *buf, uint32_t len, uint32_t loop) {
    DMA_ClockEnable(MAX98357A_DMA_INSTANCE);
    DMA_DeInit(MAX98357A_TX_DMA_CHANNEL);

    DMA_InitTypeDef DMA_InitStructure = {0};
    DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)&(MAX98357A_I2S_INSTANCE->DATAR);
    DMA_InitStructure.DMA_Memory0BaseAddr    = (uint32_t)buf;
    DMA_InitStructure.DMA_DIR                = DMA_DIR_PeripheralDST;
    DMA_InitStructure.DMA_BufferSize         = len;
    DMA_InitStructure.DMA_PeripheralInc      = DMA_PeripheralInc_Disable;
    DMA_InitStructure.DMA_MemoryInc          = DMA_MemoryInc_Enable;
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
    DMA_InitStructure.DMA_MemoryDataSize     = DMA_MemoryDataSize_HalfWord;
    DMA_InitStructure.DMA_Mode              = (loop ? DMA_Mode_Circular : DMA_Mode_Normal);
    DMA_InitStructure.DMA_Priority          = DMA_Priority_High;
    DMA_InitStructure.DMA_M2M               = DMA_M2M_Disable;
    DMA_InitStructure.DMA_BufferMode        = DMA_SingleBufferMode;
    DMA_InitStructure.DMA_DoubleBuffer_StartMemory = DMA_DoubleBufferMode_Memory_0;
    DMA_Init(MAX98357A_TX_DMA_CHANNEL, &DMA_InitStructure);
    DMA_MuxChannelConfig(MAX98357A_TX_DMAMUX_CHANNEL, MAX98357A_TX_DMA_REQUEST);
}

int max98357a_init(max98357a_gain_t gain) {
    max98357a_gpio_init();
    max98357a_i2s_init();

    if (gain == MAX98357A_GAIN_15DB_RIGHT) {
        MAX98357A_GAIN_HIGH();
    } else {
        MAX98357A_GAIN_LOW();
    }
    Delay_Ms(1);
    MAX98357A_SHDN_HIGH();
    Delay_Ms(2);

    g_playing = 0;
    g_loop_len = 0;
    return 0;
}

void max98357a_shutdown(void) {
    max98357a_stop();
    MAX98357A_SHDN_LOW();
    g_playing = 0;
}

void max98357a_wakeup(max98357a_gain_t gain) {
    MAX98357A_SHDN_LOW();
    Delay_Ms(1);
    if (gain == MAX98357A_GAIN_15DB_RIGHT) {
        MAX98357A_GAIN_HIGH();
    } else {
        MAX98357A_GAIN_LOW();
    }
    Delay_Ms(1);
    MAX98357A_SHDN_HIGH();
    Delay_Ms(2);
}

void max98357a_stop(void) {
    DMA_ITConfig(MAX98357A_TX_DMA_CHANNEL, DMA_IT_TC | DMA_IT_HT, DISABLE);
    NVIC_DisableIRQ(MAX98357A_TX_DMA_IRQn);
    DMA_Cmd(MAX98357A_TX_DMA_CHANNEL, DISABLE);
    I2S_Cmd(MAX98357A_I2S_INSTANCE, DISABLE);
    g_playing = 0;
    g_stream_cb = 0;
}

int max98357a_play_tone(uint32_t freq_hz, uint32_t duration_ms, uint16_t amplitude) {
    if (freq_hz == 0 || duration_ms == 0 || amplitude == 0) {
        return -1;
    }

    uint32_t samples_per_cycle = MAX98357A_SAMPLE_RATE / freq_hz;
    if (samples_per_cycle == 0) samples_per_cycle = 1;
    if (samples_per_cycle > MAX98357A_TONE_BUF_SIZE) {
        return -2;
    }

    uint32_t buf_len = MAX98357A_TONE_BUF_SIZE;
    while (buf_len % samples_per_cycle != 0) {
        buf_len--;
    }
    if (buf_len < samples_per_cycle) buf_len = samples_per_cycle;

    for (uint32_t i = 0; i < buf_len; i++) {
        float phase = 2.0f * (float)M_PI * (float)freq_hz * (float)i / (float)MAX98357A_SAMPLE_RATE;
        int32_t v = (int32_t)((float)amplitude * sinf(phase));
        if (v > 32767) v = 32767;
        if (v < -32768) v = -32768;
        g_tone_buf[i] = (int16_t)v;
    }

    max98357a_dma_init(g_tone_buf, buf_len, 1);
    I2S_Cmd(MAX98357A_I2S_INSTANCE, ENABLE);
    DMA_Cmd(MAX98357A_TX_DMA_CHANNEL, ENABLE);
    g_playing = 1;
    g_loop_len = buf_len;

    Delay_Ms(duration_ms);

    max98357a_stop();
    return 0;
}

int max98357a_play_buffer(const int16_t *buf, uint32_t len, uint32_t loop) {
    if (buf == 0 || len == 0) {
        return -1;
    }
    max98357a_dma_init(buf, len, loop);
    I2S_Cmd(MAX98357A_I2S_INSTANCE, ENABLE);
    DMA_Cmd(MAX98357A_TX_DMA_CHANNEL, ENABLE);
    g_playing = 1;
    g_loop_len = len;
    return 0;
}

int max98357a_is_playing(void) {
    return g_playing;
}

int max98357a_play_stream(int16_t *buf, uint32_t total_samples, max98357a_dma_cb_t cb) {
    if (buf == 0 || total_samples == 0 || cb == 0) {
        return -1;
    }

    g_stream_cb = cb;

    DMA_ClockEnable(MAX98357A_DMA_INSTANCE);
    DMA_DeInit(MAX98357A_TX_DMA_CHANNEL);

    DMA_InitTypeDef DMA_InitStructure = {0};
    DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)&(MAX98357A_I2S_INSTANCE->DATAR);
    DMA_InitStructure.DMA_Memory0BaseAddr    = (uint32_t)buf;
    DMA_InitStructure.DMA_DIR                = DMA_DIR_PeripheralDST;
    DMA_InitStructure.DMA_BufferSize         = total_samples;
    DMA_InitStructure.DMA_PeripheralInc      = DMA_PeripheralInc_Disable;
    DMA_InitStructure.DMA_MemoryInc          = DMA_MemoryInc_Enable;
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
    DMA_InitStructure.DMA_MemoryDataSize     = DMA_MemoryDataSize_HalfWord;
    DMA_InitStructure.DMA_Mode              = DMA_Mode_Circular;
    DMA_InitStructure.DMA_Priority          = DMA_Priority_High;
    DMA_InitStructure.DMA_M2M               = DMA_M2M_Disable;
    DMA_InitStructure.DMA_BufferMode        = DMA_SingleBufferMode;
    DMA_InitStructure.DMA_DoubleBuffer_StartMemory = DMA_DoubleBufferMode_Memory_0;
    DMA_Init(MAX98357A_TX_DMA_CHANNEL, &DMA_InitStructure);
    DMA_MuxChannelConfig(MAX98357A_TX_DMAMUX_CHANNEL, MAX98357A_TX_DMA_REQUEST);

    DMA_ClearITPendingBit(MAX98357A_DMA_INSTANCE, MAX98357A_TX_DMA_TC_FLAG);
    DMA_ClearITPendingBit(MAX98357A_DMA_INSTANCE, DMA1_IT_HT3);
    DMA_ITConfig(MAX98357A_TX_DMA_CHANNEL, DMA_IT_TC | DMA_IT_HT, ENABLE);

    NVIC_SetPriority(MAX98357A_TX_DMA_IRQn, (1 << 7) | (1 << 4));
    NVIC_EnableIRQ(MAX98357A_TX_DMA_IRQn);

    I2S_Cmd(MAX98357A_I2S_INSTANCE, ENABLE);
    DMA_Cmd(MAX98357A_TX_DMA_CHANNEL, ENABLE);
    g_playing = 1;
    g_loop_len = total_samples;
    return 0;
}

void DMA1_Channel3_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));

void DMA1_Channel3_IRQHandler(void) {
    if (DMA_GetITStatus(MAX98357A_DMA_INSTANCE, DMA1_IT_HT3) != RESET) {
        DMA_ClearITPendingBit(MAX98357A_DMA_INSTANCE, DMA1_IT_HT3);
        if (g_stream_cb) g_stream_cb(0);
    }
    if (DMA_GetITStatus(MAX98357A_DMA_INSTANCE, MAX98357A_TX_DMA_TC_FLAG) != RESET) {
        DMA_ClearITPendingBit(MAX98357A_DMA_INSTANCE, MAX98357A_TX_DMA_TC_FLAG);
        if (g_stream_cb) g_stream_cb(1);
    }
}

int max98357a_selftest(void) {
    max98357a_init(MAX98357A_GAIN_15DB_I2S);
    max98357a_play_tone(1000, 1000, 30000);
    max98357a_shutdown();
    return 0;
}