#include <string.h>
#include "ch32h417.h"
#include "OLED_Font.h"
#include "OLED.h"
#include "debug.h"
#include "Config.h"
/* 全屏缓冲区 (128 列 × 8 页 = 1024 字节) */
static uint8_t OLED_GRAM[8][128];
/* ========== 引脚定义 ========== */

#define SPI_HARDWARE 0
#define SPI_SOFTWARE 1
#define SPI_TYPE SPI_SOFTWARE

/* ========== 软件 SPI 引脚控制宏 ========== */
#define OLED_W_CS(x) Hal_GPIO_Write (OLED_CS_PIN_ENC, (x))
#define OLED_W_RES(x) Hal_GPIO_Write (OLED_RES_PIN_ENC, (x))
#define OLED_W_DC(x) Hal_GPIO_Write (OLED_DC_PIN_ENC, (x))
#define OLED_W_SCK(x) Hal_GPIO_Write (OLED_SCK_PIN_ENC, (x))
#define OLED_W_MOSI(x) Hal_GPIO_Write (OLED_MOSI_PIN_ENC, (x))

/* ========== I2C 初始化 ========== */
void OLED_I2C_Init (void) {
    AFIO_ClockEnable();
    I2C_ClockEnable (OLED_I2C_INSTANCE);
    GPIO_ClockEnable (OLED_SCL_PORT);
    GPIO_ClockEnable (OLED_SDA_PORT);

    Hal_GPIO_Init (OLED_SCL_PIN_ENC, HAL_GPIO_MODE_AF_OD, HAL_GPIO_SPEED_VERY_HIGH, OLED_SCL_AF);
    Hal_GPIO_Init (OLED_SDA_PIN_ENC, HAL_GPIO_MODE_AF_OD, HAL_GPIO_SPEED_VERY_HIGH, OLED_SDA_AF);

    I2C_InitTypeDef I2C_InitStructure;
    I2C_InitStructure.I2C_ClockSpeed = 1000000;
    I2C_InitStructure.I2C_Mode = I2C_Mode_I2C;
    I2C_InitStructure.I2C_DutyCycle = I2C_DutyCycle_2;
    I2C_InitStructure.I2C_OwnAddress1 = 0x00;
    I2C_InitStructure.I2C_Ack = I2C_Ack_Enable;
    I2C_InitStructure.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit;
    I2C_Init (OLED_I2C_INSTANCE, &I2C_InitStructure);
    I2C_Cmd (OLED_I2C_INSTANCE, ENABLE);
}

/* ========== SPI 引脚初始化（全部作为普通推挽输出） ========== */
void OLED_SPI_Init (void) {
    AFIO_ClockEnable();
    GPIO_ClockEnable (OLED_CS_PORT);
    GPIO_ClockEnable (OLED_RES_PORT);
    GPIO_ClockEnable (OLED_DC_PORT);
    GPIO_ClockEnable (OLED_SCK_PORT);
    GPIO_ClockEnable (OLED_MOSI_PORT);
#if (SPI_TYPE == SPI_SOFTWARE)
    Hal_GPIO_Init (OLED_CS_PIN_ENC, HAL_GPIO_MODE_OUTPUT_PP, HAL_GPIO_SPEED_VERY_HIGH, 0);
    Hal_GPIO_Init (OLED_RES_PIN_ENC, HAL_GPIO_MODE_OUTPUT_PP, HAL_GPIO_SPEED_VERY_HIGH, 0);
    Hal_GPIO_Init (OLED_DC_PIN_ENC, HAL_GPIO_MODE_OUTPUT_PP, HAL_GPIO_SPEED_VERY_HIGH, 0);
    Hal_GPIO_Init (OLED_SCK_PIN_ENC, HAL_GPIO_MODE_OUTPUT_PP, HAL_GPIO_SPEED_VERY_HIGH, 0);
    Hal_GPIO_Init (OLED_MOSI_PIN_ENC, HAL_GPIO_MODE_OUTPUT_PP, HAL_GPIO_SPEED_VERY_HIGH, 0);
    OLED_W_SCK (1); /* 空闲时 SCK 为高（CPOL=1） */
#else
    SPI_ClockEnable (OLED_SPI_INSTANCE);
    /* SCK 和 MOSI 复用推挽 */
    Hal_GPIO_Init (OLED_SCK_PIN_ENC, HAL_GPIO_MODE_AF_PP, HAL_GPIO_SPEED_VERY_HIGH, OLED_SCK_AF);
    Hal_GPIO_Init (OLED_MOSI_PIN_ENC, HAL_GPIO_MODE_AF_PP, HAL_GPIO_SPEED_VERY_HIGH, OLED_MOSI_AF);
    /* CS、RES、DC 为普通推挽输出 */
    Hal_GPIO_Init (OLED_CS_PIN_ENC, HAL_GPIO_MODE_OUTPUT_PP, HAL_GPIO_SPEED_VERY_HIGH, 0);
    Hal_GPIO_Init (OLED_RES_PIN_ENC, HAL_GPIO_MODE_OUTPUT_PP, HAL_GPIO_SPEED_VERY_HIGH, 0);
    Hal_GPIO_Init (OLED_DC_PIN_ENC, HAL_GPIO_MODE_OUTPUT_PP, HAL_GPIO_SPEED_VERY_HIGH, 0);

    /* 配置 SPI 外设 */
    SPI_InitTypeDef SPI_InitStructure;
    SPI_InitStructure.SPI_Direction = SPI_Direction_1Line_Tx;
    SPI_InitStructure.SPI_Mode = SPI_Mode_Master;
    SPI_InitStructure.SPI_DataSize = SPI_DataSize_8b;
    SPI_InitStructure.SPI_CPOL = SPI_CPOL_High;
    SPI_InitStructure.SPI_CPHA = SPI_CPHA_2Edge;
    SPI_InitStructure.SPI_NSS = SPI_NSS_Soft;
    SPI_InitStructure.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_Mode4;
    SPI_InitStructure.SPI_FirstBit = SPI_FirstBit_MSB;
    SPI_InitStructure.SPI_CRCPolynomial = 7;
    SPI_Init (OLED_SPI_INSTANCE, &SPI_InitStructure);
    SPI_Cmd (OLED_SPI_INSTANCE, ENABLE);

#endif
    OLED_W_CS (1);
    OLED_W_DC (1);
}

/* ========== I2C 多字节发送 ========== */
void Hardware_I2C_Write (uint8_t addr, uint8_t *data, uint8_t len) {
    while (I2C_GetFlagStatus (OLED_I2C_INSTANCE, I2C_FLAG_BUSY));
    I2C_GenerateSTART (OLED_I2C_INSTANCE, ENABLE);
    while (!I2C_CheckEvent (OLED_I2C_INSTANCE, I2C_EVENT_MASTER_MODE_SELECT));
    I2C_Send7bitAddress (OLED_I2C_INSTANCE, addr, I2C_Direction_Transmitter);
    while (!I2C_CheckEvent (OLED_I2C_INSTANCE, I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED));
    for (uint8_t i = 0; i < len; i++) {
        I2C_SendData (OLED_I2C_INSTANCE, data[i]);
        while (!I2C_CheckEvent (OLED_I2C_INSTANCE, I2C_EVENT_MASTER_BYTE_TRANSMITTED));
    }
    I2C_GenerateSTOP (OLED_I2C_INSTANCE, ENABLE);
}

/* ========== 软件模拟 SPI 发送一个字节（CPOL=1, CPHA=1 模式3） ========== */
static void SPI_SendByte (uint8_t byte) {
#if (SPI_TYPE == SPI_SOFTWARE)
    for (uint8_t i = 0; i < 8; i++) {
        OLED_W_MOSI ((byte & 0x80) ? 1 : 0);
        byte <<= 1;
        OLED_W_SCK (0);
        OLED_W_SCK (1);
    }
#else
    while (SPI_I2S_GetFlagStatus (OLED_SPI_INSTANCE, SPI_I2S_FLAG_TXE) == RESET);
    SPI_I2S_SendData (OLED_SPI_INSTANCE, byte);
    while (SPI_I2S_GetFlagStatus (OLED_SPI_INSTANCE, SPI_I2S_FLAG_BSY) == SET);
#endif
}

/* ========== 写命令 ========== */
static void OLED_WriteCommand (OLED_Type Type, uint8_t Command) {
    if (Type == I2C) {
        uint8_t buf[2] = {0x00, Command};
        Hardware_I2C_Write (OLED_I2C_ADDR, buf, 2);
    } else {
        OLED_W_DC(0);
        OLED_W_CS(0);
        SPI_SendByte(Command);
        OLED_W_CS(1);
    }
}

/* ========== 写数据 ========== */
static void OLED_WriteData (OLED_Type Type, uint8_t Data) {
    if (Type == I2C) {
        uint8_t buf[2] = {0x40, Data};
        Hardware_I2C_Write (OLED_I2C_ADDR, buf, 2);
    } else {
        OLED_W_DC(1);
        OLED_W_CS(0);
        SPI_SendByte(Data);
        OLED_W_CS(1);
    }
}

/* ========== 设置光标位置 ========== */
static void OLED_SetCursor (OLED_Type Type, uint8_t Y, uint8_t X) {
    OLED_WriteCommand (Type, 0xB0 | Y);
    OLED_WriteCommand (Type, 0x10 | ((X & 0xF0) >> 4));
    OLED_WriteCommand (Type, 0x00 | (X & 0x0F));
}

/* ========== 清屏 ========== */
void OLED_Clear (OLED_Type Type) {
    for (uint8_t j = 0; j < 8; j++) {
        OLED_SetCursor (Type, j, 0);
        for (uint8_t i = 0; i < 128; i++) {
            OLED_WriteData (Type, 0x00);
        }
    }
}

void OLED_Clear_All (void) {
    OLED_Clear (SPI);
    //OLED_Clear (I2C);
}

/* 一次性将缓冲区发送到 SPI 屏幕（页寻址模式） */
void OLED_UpdateFull (OLED_Type Type) {
    if (Type == I2C)
        return;
    OLED_W_CS (0);
    for (uint8_t page = 0; page < 8; page++) {
        OLED_W_DC (0);
        SPI_SendByte (0xB0 | page);
        OLED_W_DC (0);
        SPI_SendByte (0x00);
        OLED_W_DC (0);
        SPI_SendByte (0x10);
        OLED_W_DC (1);
        for (uint8_t col = 0; col < 128; col++) {
            SPI_SendByte (OLED_GRAM[page][col]);
        }
    }
    OLED_W_CS (1);
}

/* ========== 显示一个字符 (8x16) ========== */
void OLED_ShowChar (OLED_Type Type, uint8_t Line, uint8_t Column, char Char) {
    const uint8_t (*font)[16] = OLED_F8x16;
    uint8_t idx = Char - ' ';
    OLED_SetCursor (Type, Line * 2, Column * 8);
    for (uint8_t i = 0; i < 8; i++) {
        OLED_WriteData (Type, font[idx][i]);
    }
    OLED_SetCursor (Type, Line * 2 + 1, Column * 8);
    for (uint8_t i = 0; i < 8; i++) {
        OLED_WriteData (Type, font[idx][i + 8]);
    }
}

/* ========== 右对齐字符串 ========== */
void OLED_ShowStringRight (OLED_Type Type, uint8_t Line, char *String) {
    uint8_t len = strlen (String);
    uint8_t startColumn = (len >= 16) ? 1 : (16 - len + 1);
    for (uint8_t i = 0; String[i] != '\0'; i++) {
        OLED_ShowChar (Type, Line, startColumn + i, String[i]);
    }
}

/* ========== 普通显示字符串 ========== */
void OLED_ShowString (OLED_Type Type, uint8_t Line, uint8_t Column, char *String) {
    for (uint8_t i = 0; String[i] != '\0'; i++) {
        OLED_ShowChar (Type, Line, Column + i, String[i]);
    }
}

/* ========== 循环滚动字符串 ========== */
uint8_t OLED_ShowString_Scroll (OLED_Type Type, uint8_t Line, uint8_t Column,
                                const char *str, uint8_t offset, uint8_t display_width) {
    uint8_t len = strlen (str);
    uint8_t next_offset = 0;
    if (len <= display_width) {
        for (uint8_t i = 0; i < display_width; i++) {
            char ch = (i < len) ? str[i] : ' ';
            OLED_ShowChar (Type, Line, Column + i, ch);
        }
        next_offset = 0;
    } else {
        if (offset > len - display_width)
            offset = 0;
        for (uint8_t i = 0; i < display_width; i++) {
            OLED_ShowChar (Type, Line, Column + i, str[offset + i]);
        }
        next_offset = offset + 1;
        if (next_offset > len - display_width)
            next_offset = 0;
    }
    return next_offset;
}

/* ========== 整数幂函数 ========== */
uint32_t OLED_Pow (uint32_t X, uint32_t Y) {
    uint32_t Result = 1;
    while (Y--) Result *= X;
    return Result;
}

/* ========== 显示无符号整数 ========== */
void OLED_ShowNum (OLED_Type Type, uint8_t Line, uint8_t Column,
                   uint32_t Number, uint8_t Length) {
    for (uint8_t i = 0; i < Length; i++) {
        OLED_ShowChar (Type, Line, Column + i,
                       Number / OLED_Pow (10, Length - i - 1) % 10 + '0');
    }
}

/* ========== 显示有符号整数 ========== */
void OLED_ShowSignedNum (OLED_Type Type, uint8_t Line, uint8_t Column,
                         int32_t Number, uint8_t Length) {
    uint32_t Number1;
    if (Number >= 0) {
        OLED_ShowChar (Type, Line, Column, '+');
        Number1 = Number;
    } else {
        OLED_ShowChar (Type, Line, Column, '-');
        Number1 = -Number;
    }
    for (uint8_t i = 0; i < Length; i++) {
        OLED_ShowChar (Type, Line, Column + i + 1,
                       Number1 / OLED_Pow (10, Length - i - 1) % 10 + '0');
    }
}

/* ========== 显示十六进制 ========== */
void OLED_ShowHexNum (OLED_Type Type, uint8_t Line, uint8_t Column,
                      uint32_t Number, uint8_t Length) {
    for (uint8_t i = 0; i < Length; i++) {
        uint8_t SingleNumber = Number / OLED_Pow (16, Length - i - 1) % 16;
        if (SingleNumber < 10)
            OLED_ShowChar (Type, Line, Column + i, SingleNumber + '0');
        else
            OLED_ShowChar (Type, Line, Column + i, SingleNumber - 10 + 'A');
    }
}

/* ========== 显示二进制 ========== */
void OLED_ShowBinNum (OLED_Type Type, uint8_t Line, uint8_t Column,
                      uint32_t Number, uint8_t Length) {
    for (uint8_t i = 0; i < Length; i++) {
        OLED_ShowChar (Type, Line, Column + i,
                       Number / OLED_Pow (2, Length - i - 1) % 2 + '0');
    }
}

/* ========== 设置列地址 ========== */
void OLED_SetCol (OLED_Type Type, uint8_t col) {
    OLED_WriteCommand (Type, 0x10 | ((col & 0xF0) >> 4));
    OLED_WriteCommand (Type, 0x00 | (col & 0x0F));
}

/* ========== 清空指定页区域 ========== */
void OLED_ClearArea (OLED_Type Type, uint8_t start_page, uint8_t end_page) {
    for (uint8_t page = start_page; page <= end_page; page++) {
        OLED_SetCursor (Type, page, 0);
        for (uint8_t i = 0; i < 128; i++) {
            OLED_WriteData (Type, 0x00);
        }
    }
}

/* ========== 绘制频谱柱状图 ========== */
void OLED_DrawSpectrum (OLED_Type Type, uint8_t x_start, uint8_t *heights,
                        uint8_t num_bins, uint8_t max_height) {
    if (max_height > 64)
        max_height = 64;
    for (uint8_t i = 0; i < num_bins && i < 128; i++) {
        uint8_t x = x_start + i;
        uint8_t h = heights[i];
        if (h > max_height)
            h = max_height;
        int remaining = h;
        for (int8_t page = 7; page >= 0; page--) {
            OLED_SetCursor (Type, page, x);
            uint8_t dat = 0;
            if (remaining > 0) {
                if (remaining >= 8) {
                    dat = 0xFF;
                    remaining -= 8;
                } else {
                    dat = 0xFF << (8 - remaining);
                    remaining = 0;
                }
            }
            OLED_WriteData (Type, dat);
        }
    }
}

/* ========== 绘制波形（128点折线） ========== */
void OLED_DrawWaveformFast (OLED_Type Type, uint8_t *y_values) {
    if (Type == I2C)
        return;

    memset (OLED_GRAM, 0, sizeof (OLED_GRAM));

    for (uint8_t x = 0; x < 128; x++) {
        uint8_t y_curr = y_values[x];
        if (y_curr > 63)
            y_curr = 63;

        if (x == 0) {
            uint8_t page = y_curr / 8;
            uint8_t bit = y_curr % 8;
            OLED_GRAM[page][x] |= (1 << bit);
        } else {
            uint8_t y_prev = y_values[x - 1];
            if (y_prev > 63)
                y_prev = 63;
            uint8_t y_min = (y_prev < y_curr) ? y_prev : y_curr;
            uint8_t y_max = (y_prev > y_curr) ? y_prev : y_curr;
            for (uint8_t y = y_min; y <= y_max; y++) {
                uint8_t page = y / 8;
                uint8_t bit = y % 8;
                OLED_GRAM[page][x] |= (1 << bit);
            }
        }
    }

    OLED_UpdateFull (Type);
}

/* ========== OLED 总初始化 ========== */
void OLED_Init (void) {
    /* 1. 初始化 I2C 屏幕 */
    // OLED_I2C_Init();
    // OLED_WriteCommand (I2C, 0xAE);
    // OLED_WriteCommand (I2C, 0xD5);
    // OLED_WriteCommand (I2C, 0x80);
    // OLED_WriteCommand (I2C, 0xA8);
    // OLED_WriteCommand (I2C, 0x3F);
    // OLED_WriteCommand (I2C, 0xD3);
    // OLED_WriteCommand (I2C, 0x00);
    // OLED_WriteCommand (I2C, 0x40);
    // OLED_WriteCommand (I2C, 0xA1);
    // OLED_WriteCommand (I2C, 0xC8);
    // OLED_WriteCommand (I2C, 0xDA);
    // OLED_WriteCommand (I2C, 0x12);
    // OLED_WriteCommand (I2C, 0x81);
    // OLED_WriteCommand (I2C, 0xCF);
    // OLED_WriteCommand (I2C, 0xD9);
    // OLED_WriteCommand (I2C, 0xF1);
    // OLED_WriteCommand (I2C, 0xDB);
    // OLED_WriteCommand (I2C, 0x30);
    // OLED_WriteCommand (I2C, 0xA4);
    // OLED_WriteCommand (I2C, 0xA6);
    // OLED_WriteCommand (I2C, 0x8D);
    // OLED_WriteCommand (I2C, 0x14);
    // OLED_WriteCommand (I2C, 0xAF);
    // OLED_Clear (I2C);

    /* 2. 初始化 SPI 屏幕 */
    OLED_SPI_Init();
    OLED_W_RES (0);
    Delay_Ms (200);
    OLED_W_RES (1);
    Delay_Ms (200);

    OLED_WriteCommand (SPI, 0xAE);
    OLED_WriteCommand (SPI, 0xD5);
    OLED_WriteCommand (SPI, 0xA8);
    OLED_WriteCommand (SPI, 0xD3);
    OLED_WriteCommand (SPI, 0x40);
    OLED_WriteCommand (SPI, 0xA1);
    OLED_WriteCommand (SPI, 0xC8);
    OLED_WriteCommand (SPI, 0xDA);
    OLED_WriteCommand (SPI, 0x81);
    OLED_WriteCommand (SPI, 0xD9);
    OLED_WriteCommand (SPI, 0xDB);
    OLED_WriteCommand (SPI, 0x8D);
    OLED_WriteCommand (SPI, 0xA4);
    OLED_WriteCommand (SPI, 0xA6);
    OLED_WriteCommand (SPI, 0x20);
    OLED_Clear (SPI);
    OLED_WriteCommand (SPI, 0xAF);
}