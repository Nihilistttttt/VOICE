#include <string.h>
#include "ch32v30x.h"
#include "Config.h"
#include "hal_gpio.h"
#include "OLED_Font.h"
#include "OLED.h"
#include "debug.h"

#define OLED_W_CS(x)   Hal_GPIO_Write(OLED_CS_PIN_ENC, (x))
#define OLED_W_RES(x)  Hal_GPIO_Write(OLED_RES_PIN_ENC, (x))
#define OLED_W_DC(x)   Hal_GPIO_Write(OLED_DC_PIN_ENC, (x))

static uint8_t oled_gram[8][128];

static void OLED_SPI_Init(void)
{
    AFIO_ClockEnable();
    SPI_ClockEnable(OLED_SPI_INSTANCE);
    GPIO_ClockEnable(HAL_GPIO_PORT(OLED_CS_PIN_ENC));
    GPIO_ClockEnable(HAL_GPIO_PORT(OLED_RES_PIN_ENC));
    GPIO_ClockEnable(HAL_GPIO_PORT(OLED_DC_PIN_ENC));
    GPIO_ClockEnable(HAL_GPIO_PORT(OLED_SCK_PIN_ENC));
    GPIO_ClockEnable(HAL_GPIO_PORT(OLED_MOSI_PIN_ENC));

    Hal_GPIO_Init(OLED_CS_PIN_ENC,  HAL_GPIO_MODE_OUTPUT_PP, HAL_GPIO_SPEED_HIGH, 0);
    Hal_GPIO_Init(OLED_RES_PIN_ENC, HAL_GPIO_MODE_OUTPUT_PP, HAL_GPIO_SPEED_HIGH, 0);
    Hal_GPIO_Init(OLED_DC_PIN_ENC,  HAL_GPIO_MODE_OUTPUT_PP, HAL_GPIO_SPEED_HIGH, 0);
    Hal_GPIO_Init(OLED_SCK_PIN_ENC, HAL_GPIO_MODE_AF_PP,     HAL_GPIO_SPEED_HIGH, 0);
    Hal_GPIO_Init(OLED_MOSI_PIN_ENC,HAL_GPIO_MODE_AF_PP,     HAL_GPIO_SPEED_HIGH, 0);

    SPI_InitTypeDef SPI_InitStructure;
    SPI_InitStructure.SPI_Direction = SPI_Direction_1Line_Tx;
    SPI_InitStructure.SPI_Mode = SPI_Mode_Master;
    SPI_InitStructure.SPI_DataSize = SPI_DataSize_8b;
    SPI_InitStructure.SPI_CPOL = SPI_CPOL_High;
    SPI_InitStructure.SPI_CPHA = SPI_CPHA_2Edge;
    SPI_InitStructure.SPI_NSS = SPI_NSS_Soft;
    SPI_InitStructure.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_16;
    SPI_InitStructure.SPI_FirstBit = SPI_FirstBit_MSB;
    SPI_InitStructure.SPI_CRCPolynomial = 7;
    SPI_Init(OLED_SPI_INSTANCE, &SPI_InitStructure);
    SPI_Cmd(OLED_SPI_INSTANCE, ENABLE);

    OLED_W_CS(1);
    OLED_W_DC(1);
}

static void SPI_SendByte(uint8_t byte)
{
    while (SPI_I2S_GetFlagStatus(OLED_SPI_INSTANCE, SPI_I2S_FLAG_TXE) == RESET);
    SPI_I2S_SendData(OLED_SPI_INSTANCE, byte);
    while (SPI_I2S_GetFlagStatus(OLED_SPI_INSTANCE, SPI_I2S_FLAG_BSY) == SET);
}

static void OLED_WriteCommand(uint8_t Command)
{
    OLED_W_DC(0);
    OLED_W_CS(0);
    SPI_SendByte(Command);
    OLED_W_CS(1);
}


static void OLED_SetCursor(uint8_t Y, uint8_t X)
{
    OLED_WriteCommand(0xB0 | Y);
    OLED_WriteCommand(0x10 | ((X & 0xF0) >> 4));
    OLED_WriteCommand(0x00 | (X & 0x0F));
}

void OLED_Clear(void)
{
    memset(oled_gram, 0, sizeof(oled_gram));
}

void OLED_Refresh(void)
{
    for (uint8_t j = 0; j < 8; j++) {
        OLED_SetCursor(j, 0);
        OLED_W_DC(1);
        OLED_W_CS(0);
        for (uint8_t i = 0; i < 128; i++) {
            SPI_SendByte(oled_gram[j][i]);
        }
        OLED_W_CS(1);
    }
}

void OLED_SetPixel(uint8_t x, uint8_t y, uint8_t on)
{
    if (x >= 128 || y >= 64) return;
    uint8_t page = y >> 3;
    uint8_t bit  = 1 << (y & 7);
    if (on) oled_gram[page][x] |= bit;
    else    oled_gram[page][x] &= ~bit;
}

void OLED_ShowChar(uint8_t Line, uint8_t Column, char Char)
{
    const uint8_t (*font)[16] = OLED_F8x16;
    uint8_t idx = Char - ' ';
    uint8_t x = Column * 8;
    uint8_t page = Line * 2;
    for (uint8_t i = 0; i < 8; i++) {
        oled_gram[page][x + i]     = font[idx][i];
        oled_gram[page + 1][x + i] = font[idx][i + 8];
    }
}

void OLED_ShowString(uint8_t Line, uint8_t Column, const char *String)
{
    for (uint8_t i = 0; String[i] != '\0'; i++) {
        OLED_ShowChar(Line, Column + i, String[i]);
    }
}

static uint32_t OLED_Pow(uint32_t X, uint32_t Y)
{
    uint32_t Result = 1;
    while (Y--) Result *= X;
    return Result;
}

void OLED_ShowNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length)
{
    for (uint8_t i = 0; i < Length; i++) {
        OLED_ShowChar(Line, Column + i, Number / OLED_Pow(10, Length - i - 1) % 10 + '0');
    }
}

void OLED_DrawWaveSplit(const uint16_t *data_a, const uint16_t *data_b,
                        uint8_t head, uint16_t scale,
                        uint8_t y_top, uint8_t height)
{
    uint8_t yv[64];
    uint8_t y_mid = y_top + height / 2;
    int16_t half = height / 2;

    uint32_t sum = 0;
    for (uint8_t x = 0; x < 64; x++) sum += data_a[(head + x) & 127];
    uint16_t dc = sum / 64;
    for (uint8_t x = 0; x < 64; x++) {
        int16_t diff = (int16_t)data_a[(head + x) & 127] - (int16_t)dc;
        int32_t off = (scale == 0) ? 0 : ((int32_t)diff * half) / (int32_t)scale;
        if (off > half) off = half;
        if (off < -half) off = -half;
        yv[x] = (uint8_t)(y_mid - off);
    }
    for (uint8_t x = 0; x < 64; x++) {
        if (x > 0) {
            uint8_t lo = (yv[x-1] < yv[x]) ? yv[x-1] : yv[x];
            uint8_t hi = (yv[x-1] > yv[x]) ? yv[x-1] : yv[x];
            for (uint8_t y = lo; y <= hi; y++) OLED_SetPixel(x, y, 1);
        } else {
            OLED_SetPixel(0, yv[0], 1);
        }
    }

    sum = 0;
    for (uint8_t x = 0; x < 64; x++) sum += data_b[(head + x) & 127];
    dc = sum / 64;
    for (uint8_t x = 0; x < 64; x++) {
        int16_t diff = (int16_t)data_b[(head + x) & 127] - (int16_t)dc;
        int32_t off = (scale == 0) ? 0 : ((int32_t)diff * half) / (int32_t)scale;
        if (off > half) off = half;
        if (off < -half) off = -half;
        yv[x] = (uint8_t)(y_mid - off);
    }
    for (uint8_t x = 0; x < 64; x++) {
        if (x > 0) {
            uint8_t lo = (yv[x-1] < yv[x]) ? yv[x-1] : yv[x];
            uint8_t hi = (yv[x-1] > yv[x]) ? yv[x-1] : yv[x];
            for (uint8_t y = lo; y <= hi; y++) OLED_SetPixel(64 + x, y, 1);
        } else {
            OLED_SetPixel(64, yv[0], 1);
        }
    }
}

void OLED_Init(void)
{
    OLED_SPI_Init();

    OLED_W_RES(0);
    Delay_Ms(200);
    OLED_W_RES(1);
    Delay_Ms(200);

    OLED_WriteCommand(0xAE);
    OLED_WriteCommand(0xD5);
    OLED_WriteCommand(0xA8);
    OLED_WriteCommand(0xD3);
    OLED_WriteCommand(0x40);
    OLED_WriteCommand(0xA1);
    OLED_WriteCommand(0xC8);
    OLED_WriteCommand(0xDA);
    OLED_WriteCommand(0x81);
    OLED_WriteCommand(0xD9);
    OLED_WriteCommand(0xDB);
    OLED_WriteCommand(0x8D);
    OLED_WriteCommand(0xA4);
    OLED_WriteCommand(0xA6);
    OLED_WriteCommand(0x20);
    OLED_Clear();
    OLED_Refresh();
    OLED_WriteCommand(0xAF);
}
