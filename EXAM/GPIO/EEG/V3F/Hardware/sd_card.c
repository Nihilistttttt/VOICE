#include "sd_card.h"
#include "Config.h"
#include "hal_gpio.h"
#include "Serial.h"
#include <string.h>

extern volatile uint32_t g_icm42605_ms_tick;

#define SD_CS_LOW()     Hal_GPIO_Write(SD_CS_PIN_ENC, 0)
#define SD_CS_HIGH()    Hal_GPIO_Write(SD_CS_PIN_ENC, 1)

static SD_CardType_t g_sd_type = SD_TYPE_UNKNOWN;

static void SD_GPIO_Init(void) {
    AFIO_ClockEnable();
    GPIO_ClockEnable(SD_SCK_PORT);
    GPIO_ClockEnable(SD_MISO_PORT);
    GPIO_ClockEnable(SD_MOSI_PORT);
    GPIO_ClockEnable(SD_CS_PORT);
    GPIO_ClockEnable(SD_CD_PORT);

    Hal_GPIO_Init(SD_SCK_PIN_ENC,  HAL_GPIO_MODE_AF_PP,   HAL_GPIO_SPEED_VERY_HIGH, SD_SCK_AF);
    Hal_GPIO_Init(SD_MOSI_PIN_ENC, HAL_GPIO_MODE_AF_PP,   HAL_GPIO_SPEED_VERY_HIGH, SD_MOSI_AF);
    Hal_GPIO_Init(SD_MISO_PIN_ENC, HAL_GPIO_MODE_AF_INPUT, HAL_GPIO_SPEED_VERY_HIGH, SD_MISO_AF);
    Hal_GPIO_Init(SD_CS_PIN_ENC,   HAL_GPIO_MODE_OUTPUT_PP, HAL_GPIO_SPEED_VERY_HIGH, 0);
    Hal_GPIO_Init(SD_CD_PIN_ENC,   HAL_GPIO_MODE_INPUT_PU, HAL_GPIO_SPEED_LOW, 0);
    SD_CS_HIGH();
}

static void SD_SPI_Init(uint32_t prescaler) {
    SPI_ClockEnable(SD_SPI_INSTANCE);
    SPI_InitTypeDef cfg = {0};
    cfg.SPI_Direction = SPI_Direction_2Lines_FullDuplex;
    cfg.SPI_Mode      = SPI_Mode_Master;
    cfg.SPI_DataSize  = SPI_DataSize_8b;
    cfg.SPI_CPOL      = SPI_CPOL_High;
    cfg.SPI_CPHA      = SPI_CPHA_2Edge;
    cfg.SPI_NSS       = SPI_NSS_Soft;
    cfg.SPI_BaudRatePrescaler = prescaler;
    cfg.SPI_FirstBit  = SPI_FirstBit_MSB;
    cfg.SPI_CRCPolynomial = 7;
    SPI_Init(SD_SPI_INSTANCE, &cfg);
    SPI_Cmd(SD_SPI_INSTANCE, ENABLE);
}

static uint8_t SD_SPI_Transfer(uint8_t tx) {
    while (SPI_I2S_GetFlagStatus(SD_SPI_INSTANCE, SPI_I2S_FLAG_TXE) == RESET);
    SPI_I2S_SendData(SD_SPI_INSTANCE, tx);
    while (SPI_I2S_GetFlagStatus(SD_SPI_INSTANCE, SPI_I2S_FLAG_RXNE) == RESET);
    return (uint8_t)SPI_I2S_ReceiveData(SD_SPI_INSTANCE);
}

static void SD_SPI_BaudRate(uint32_t prescaler) {
    SPI_Cmd(SD_SPI_INSTANCE, DISABLE);
    SPI_InitTypeDef cfg = {0};
    cfg.SPI_Direction = SPI_Direction_2Lines_FullDuplex;
    cfg.SPI_Mode      = SPI_Mode_Master;
    cfg.SPI_DataSize  = SPI_DataSize_8b;
    cfg.SPI_CPOL      = SPI_CPOL_High;
    cfg.SPI_CPHA      = SPI_CPHA_2Edge;
    cfg.SPI_NSS       = SPI_NSS_Soft;
    cfg.SPI_BaudRatePrescaler = prescaler;
    cfg.SPI_FirstBit  = SPI_FirstBit_MSB;
    cfg.SPI_CRCPolynomial = 7;
    SPI_Init(SD_SPI_INSTANCE, &cfg);
    SPI_Cmd(SD_SPI_INSTANCE, ENABLE);
}

static void SD_MOSI_ToAF(void) {
    Hal_GPIO_Init(SD_MOSI_PIN_ENC, HAL_GPIO_MODE_AF_PP, HAL_GPIO_SPEED_VERY_HIGH, SD_MOSI_AF);
}

static void SD_MOSI_ToIdle(void) {
    Hal_GPIO_Init(SD_MOSI_PIN_ENC, HAL_GPIO_MODE_INPUT_PU, HAL_GPIO_SPEED_LOW, 0);
}

static void SD_SendClocks(uint8_t n) {
    SD_MOSI_ToIdle();
    while (n--) SD_SPI_Transfer(0xFF);
}


static uint8_t SD_SendCmdRaw(uint8_t cmd, uint32_t arg, uint8_t crc) {
    uint8_t r, n;
    SD_MOSI_ToAF();
    SD_SPI_Transfer(0x40 | cmd);
    SD_SPI_Transfer((uint8_t)(arg >> 24));
    SD_SPI_Transfer((uint8_t)(arg >> 16));
    SD_SPI_Transfer((uint8_t)(arg >> 8));
    SD_SPI_Transfer((uint8_t)(arg));
    SD_SPI_Transfer(crc);
    SD_MOSI_ToIdle();
    SD_SPI_Transfer(0xFF);
    n = 10;
    do {
        r = SD_SPI_Transfer(0xFF);
    } while ((r & 0x80) && --n);
    return r;
}

static uint8_t SD_SendCmd(uint8_t cmd, uint32_t arg, uint8_t crc) {
    uint8_t r;
    SD_CS_LOW();
    r = SD_SendCmdRaw(cmd, arg, crc);
    SD_CS_HIGH();
    SD_SendClocks(2);
    return r;
}



static void SD_WaitNotBusy(void)
{
    SD_CS_LOW();
    SD_MOSI_ToIdle();
    uint32_t start = g_icm42605_ms_tick;
    uint8_t r;
    while (1) {
        r = SD_SPI_Transfer(0xFF);
        if (r == 0xFF) break;
        if ((g_icm42605_ms_tick - start) > 3000u) break;
    }
    SD_CS_HIGH();
    SD_SendClocks(1);
}


static int SD_GoIdle(void) {
    uint8_t r;
    int i;
    SD_SPI_BaudRate(SPI_BaudRatePrescaler_Mode7);
    SD_SendClocks(30);
    r = SD_SPI_Transfer(0xFF);
    Serial_Printf(SERIAL_PORT_DEBUG, "[SD] idle read=%02X\r\n", r);
    for (i = 0; i < 5; i++) {
        r = SD_SendCmd(0, 0, 0x95);
        Serial_Printf(SERIAL_PORT_DEBUG, "[SD] CMD0 ret=%02X\r\n", r);
        if (r == 0x01) return 0;
    }
    return SD_INIT_ERROR;
}

int SD_Init(void) {
    uint8_t r, buf[4];
    int i;

    SD_GPIO_Init();
    SD_SPI_Init(SPI_BaudRatePrescaler_Mode7);
    g_sd_type = SD_TYPE_UNKNOWN;

    if (!SD_IsCardPresent()) return SD_INIT_NO_CARD;

    Hal_GPIO_Init(SD_MISO_PIN_ENC, HAL_GPIO_MODE_INPUT_PU, HAL_GPIO_SPEED_LOW, 0);
    Serial_Printf(SERIAL_PORT_DEBUG, "[SD] MISO pullup read=%d\r\n", Hal_GPIO_Read(SD_MISO_PIN_ENC));
    Hal_GPIO_Init(SD_MISO_PIN_ENC, HAL_GPIO_MODE_AF_INPUT, HAL_GPIO_SPEED_VERY_HIGH, SD_MISO_AF);

    if (SD_GoIdle() != 0) return SD_INIT_ERROR;

    SD_CS_LOW();
    r = SD_SendCmdRaw(8, 0x000001AA, 0x87);
    Serial_Printf(SERIAL_PORT_DEBUG, "[SD] CMD8 ret=%02X\r\n", r);
    if (r == 0x01) {
        for (i = 0; i < 4; i++) buf[i] = SD_SPI_Transfer(0xFF);
        Serial_Printf(SERIAL_PORT_DEBUG, "[SD] CMD8 resp=%02X %02X %02X %02X\r\n",
                      buf[0], buf[1], buf[2], buf[3]);
    }
    SD_CS_HIGH();
    SD_SendClocks(2);
    if (r == 0x01 && (buf[2] & 0x01) && (buf[3] == 0xAA)) {
        for (i = 0; i < 10000; i++) {
            uint8_t r55 = SD_SendCmd(55, 0, 0x01);
            uint8_t r41 = 0xFF;
            if (r55 == 0x01) {
                r41 = SD_SendCmd(41, 0x40000000, 0x01);
                if (r41 == 0x00) { r = r41; break; }
            }
            r = r41;
            if (i < 3) Serial_Printf(SERIAL_PORT_DEBUG, "[SD] loop%d CMD55=%02X ACMD41=%02X\r\n", i, r55, r41);
        }
        Serial_Printf(SERIAL_PORT_DEBUG, "[SD] ACMD41 ret=%02X after %d loops\r\n", r, i);
        if (r != 0x00) return SD_INIT_ERROR;

        SD_CS_LOW();
        r = SD_SendCmdRaw(58, 0, 0x01);
        if (r == 0x00) {
            for (int j = 0; j < 4; j++) buf[j] = SD_SPI_Transfer(0xFF);
            Serial_Printf(SERIAL_PORT_DEBUG, "[SD] OCR=%02X %02X %02X %02X\r\n",
                          buf[0], buf[1], buf[2], buf[3]);
            if (buf[0] & 0x40) {
                g_sd_type = SD_TYPE_SDHC;
                Serial_Printf(SERIAL_PORT_DEBUG, "[SD] SDHC (block addressing)\r\n");
            } else {
                g_sd_type = SD_TYPE_V1;
                Serial_Printf(SERIAL_PORT_DEBUG, "[SD] SDSC V2 (byte addressing)\r\n");
            }
        } else {
            g_sd_type = SD_TYPE_SDHC;
        }
        SD_CS_HIGH();
        SD_SendClocks(2);

    } else {
        r = SD_SendCmd(55, 0, 0x01);
        if (r <= 0x01) {
            r = SD_SendCmd(41, 0, 0x01);
            if (r == 0x00) {
                g_sd_type = SD_TYPE_V1;
            } else {
                for (i = 0; i < 10000; i++) {
                    r = SD_SendCmd(1, 0, 0x00);
                    if (r == 0x00) break;
                }
                if (r != 0x00) return SD_INIT_ERROR;
                g_sd_type = SD_TYPE_V1;
            }
        }
    }

    SD_SPI_BaudRate(SPI_BaudRatePrescaler_Mode4);
    SD_SendClocks(50);
    return SD_INIT_OK;
}

int SD_IsCardPresent(void) {
    return Hal_GPIO_Read(SD_CD_PIN_ENC) == 0;
}

SD_CardType_t SD_GetType(void) {
    return g_sd_type;
}

static int SD_ReadWriteBlock(uint8_t cmd, uint32_t addr,
                              uint8_t *buf, uint32_t count, int is_write) {
    uint8_t r, token;
    uint32_t i;

    if (is_write) SD_WaitNotBusy();
    SD_CS_LOW();
    r = SD_SendCmdRaw(cmd, addr, 0x01);
    if (r != 0x00) {
        Serial_Printf(SERIAL_PORT_DEBUG, "[SD] CMD%d ret=%02X addr=%lu\r\n", cmd, r, (unsigned long)addr);
        SD_CS_HIGH();
        SD_SendClocks(2);
        return -1;
    }

    if (!is_write) {
        volatile uint32_t timeout = 500000u;
        do {
            token = SD_SPI_Transfer(0xFF);
        } while (token == 0xFF && --timeout);
        if (token != 0xFE) {
            SD_CS_HIGH();
            SD_SendClocks(2);
            return -2;
        }
        for (i = 0; i < SD_SECTOR_SIZE; i++) buf[i] = SD_SPI_Transfer(0xFF);
        SD_SPI_Transfer(0xFF);
        SD_SPI_Transfer(0xFF);
    } else {
        SD_SendClocks(2);
        SD_MOSI_ToAF();
        SD_SPI_Transfer(0xFE);
        for (i = 0; i < SD_SECTOR_SIZE; i++) SD_SPI_Transfer(buf[i]);
        SD_SPI_Transfer(0xFF);
        SD_SPI_Transfer(0xFF);
        SD_MOSI_ToIdle();
        volatile uint32_t timeout = 50000u;
        do {
            r = SD_SPI_Transfer(0xFF);
            if ((r & 0x1F) == 0x05) break;
        } while (--timeout);
        if ((r & 0x1F) != 0x05) {
            Serial_Printf(SERIAL_PORT_DEBUG, "[SD] data_resp=0x%02X (exp 0x05)\r\n", r);
            SD_CS_HIGH();
            SD_SendClocks(2);
            return -3;
        }
        uint32_t busy_start = g_icm42605_ms_tick;
        while (1) {
            r = SD_SPI_Transfer(0xFF);
            if (r == 0xFF) break;
            if ((g_icm42605_ms_tick - busy_start) > 3000u) {
                Serial_Printf(SERIAL_PORT_DEBUG, "[SD] busy_timeout 3s after write addr=%lu\r\n", (unsigned long)addr);
                SD_CS_HIGH();
                SD_SendClocks(2);
                return -4;
            }
        }
    }
    SD_CS_HIGH();
    SD_SendClocks(2);
    return 0;
}

int SD_ReadSector(uint32_t sector, uint8_t *buf) {
    uint32_t addr = (g_sd_type == SD_TYPE_SDHC) ? sector : sector * SD_SECTOR_SIZE;
    return SD_ReadWriteBlock(17, addr, buf, 1, 0);
}

int SD_WriteSector(uint32_t sector, const uint8_t *buf) {
    uint32_t addr = (g_sd_type == SD_TYPE_SDHC) ? sector : sector * SD_SECTOR_SIZE;
    return SD_ReadWriteBlock(24, addr, (uint8_t *)buf, 1, 1);
}

int SD_ReadMultiSector(uint32_t sector, uint8_t *buf, uint32_t count) {
    uint32_t addr = (g_sd_type == SD_TYPE_SDHC) ? sector : sector * SD_SECTOR_SIZE;
    uint8_t r, token;
    uint32_t i, j;

    if (count == 0) return 0;
    if (count == 1) return SD_ReadSector(sector, buf);

    SD_CS_LOW();
    r = SD_SendCmdRaw(18, addr, 0x01);
    if (r != 0x00) {
        SD_CS_HIGH();
        SD_SendClocks(2);
        return -1;
    }

    for (j = 0; j < count; j++) {
        volatile uint32_t timeout = 500000u;
        do {
            token = SD_SPI_Transfer(0xFF);
        } while (token == 0xFF && --timeout);
        if (token != 0xFE) {
            SD_SendCmdRaw(12, 0, 0x01);
            SD_CS_HIGH();
            SD_SendClocks(2);
            return -2;
        }
        for (i = 0; i < SD_SECTOR_SIZE; i++) buf[j * SD_SECTOR_SIZE + i] = SD_SPI_Transfer(0xFF);
        SD_SPI_Transfer(0xFF);
        SD_SPI_Transfer(0xFF);
    }

    SD_SendCmdRaw(12, 0, 0x01);
    SD_CS_HIGH();
    SD_SendClocks(2);
    return 0;
}

int SD_WriteMultiSector(uint32_t sector, const uint8_t *buf, uint32_t count) {
    uint32_t addr = (g_sd_type == SD_TYPE_SDHC) ? sector : sector * SD_SECTOR_SIZE;
    uint8_t r;
    uint32_t i, j;

    if (count == 0) return 0;
    if (count == 1) return SD_WriteSector(sector, buf);

    SD_CS_LOW();
    r = SD_SendCmdRaw(25, addr, 0x01);
    if (r != 0x00) {
        SD_CS_HIGH();
        SD_SendClocks(2);
        return -1;
    }

    for (j = 0; j < count; j++) {
        SD_SendClocks(2);
        SD_MOSI_ToAF();
        SD_SPI_Transfer(0xFC);
        for (i = 0; i < SD_SECTOR_SIZE; i++) SD_SPI_Transfer(buf[j * SD_SECTOR_SIZE + i]);
        SD_SPI_Transfer(0xFF);
        SD_SPI_Transfer(0xFF);
        SD_MOSI_ToIdle();
        volatile uint32_t timeout = 50000u;
        do {
            r = SD_SPI_Transfer(0xFF);
            if ((r & 0x1F) == 0x05) break;
        } while (--timeout);
        if ((r & 0x1F) != 0x05) {
            SD_SendCmdRaw(12, 0, 0x01);
            SD_CS_HIGH();
            SD_SendClocks(2);
            return -3;
        }
        timeout = 500000u;
        do {
            r = SD_SPI_Transfer(0xFF);
            if (r == 0xFF) break;
        } while (--timeout);
    }

    SD_MOSI_ToAF();
    SD_SPI_Transfer(0xFD);
    SD_MOSI_ToIdle();
    volatile uint32_t timeout2 = 500000u;
    do {
        r = SD_SPI_Transfer(0xFF);
        if (r == 0xFF) break;
    } while (--timeout2);
    SD_CS_HIGH();
    SD_SendClocks(2);
    return 0;
}

int SD_SelfTest(void) {
    uint8_t buf[SD_SECTOR_SIZE];
    int i;
    int ret;

    Serial_Printf(SERIAL_PORT_DEBUG, "[SD] card present: %d\r\n", SD_IsCardPresent());

    ret = SD_Init();
    if (ret != SD_INIT_OK) {
        Serial_Printf(SERIAL_PORT_DEBUG, "[SD] init failed, ret=%d\r\n", ret);
        return ret;
    }
    Serial_Printf(SERIAL_PORT_DEBUG, "[SD] init ok, type=%d\r\n", (int)SD_GetType());

    for (i = 0; i < SD_SECTOR_SIZE; i++) buf[i] = (uint8_t)(i & 0xFF);

    ret = SD_WriteSector(1, buf);
    Serial_Printf(SERIAL_PORT_DEBUG, "[SD] write ret=%d\r\n", ret);
    if (ret != 0) return ret;

    memset(buf, 0, SD_SECTOR_SIZE);
    ret = SD_ReadSector(1, buf);
    Serial_Printf(SERIAL_PORT_DEBUG, "[SD] read ret=%d\r\n", ret);
    if (ret != 0) return ret;

    for (i = 0; i < SD_SECTOR_SIZE; i++) {
        if (buf[i] != (uint8_t)(i & 0xFF)) {
            Serial_Printf(SERIAL_PORT_DEBUG, "[SD] verify mismatch at %d: got %d exp %d\r\n",
                          i, buf[i], i & 0xFF);
            return -1;
        }
    }
    Serial_Printf(SERIAL_PORT_DEBUG, "[SD] verify 512 bytes ok\r\n");
    return 0;
}