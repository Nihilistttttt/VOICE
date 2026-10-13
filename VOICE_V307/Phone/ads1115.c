#include "ads1115.h"
#include "Config.h"
#include "debug.h"

static void I2C_Delay(void)
{
    Delay_Us(5);
}

static void I2C_SDA_High(void)  { Hal_GPIO_Set(ADS1115_SDA_PIN_ENC); }
static void I2C_SDA_Low(void)   { Hal_GPIO_Reset(ADS1115_SDA_PIN_ENC); }
static void I2C_SCL_High(void)  { Hal_GPIO_Set(ADS1115_SCL_PIN_ENC); }
static void I2C_SCL_Low(void)   { Hal_GPIO_Reset(ADS1115_SCL_PIN_ENC); }
static uint8_t I2C_SDA_Read(void) { return Hal_GPIO_Read(ADS1115_SDA_PIN_ENC); }

static void I2C_Start(void)
{
    I2C_SDA_High();
    I2C_SCL_High();
    I2C_Delay();
    I2C_SDA_Low();
    I2C_Delay();
    I2C_SCL_Low();
    I2C_Delay();
}

static void I2C_Stop(void)
{
    I2C_SDA_Low();
    I2C_SCL_High();
    I2C_Delay();
    I2C_SDA_High();
    I2C_Delay();
}

static uint8_t I2C_WriteByte(uint8_t data)
{
    uint8_t i, ack;

    for (i = 0; i < 8; i++)
    {
        if (data & 0x80)
            I2C_SDA_High();
        else
            I2C_SDA_Low();
        data <<= 1;
        I2C_Delay();
        I2C_SCL_High();
        I2C_Delay();
        I2C_SCL_Low();
    }

    I2C_SDA_High();
    I2C_Delay();
    I2C_SCL_High();
    I2C_Delay();
    ack = I2C_SDA_Read();
    I2C_SCL_Low();
    I2C_Delay();

    return ack;
}

static uint8_t I2C_ReadByte(uint8_t ack)
{
    uint8_t i, data = 0;

    I2C_SDA_High();
    for (i = 0; i < 8; i++)
    {
        I2C_SCL_High();
        I2C_Delay();
        data <<= 1;
        if (I2C_SDA_Read())
            data |= 1;
        I2C_SCL_Low();
        I2C_Delay();
    }

    if (ack)
        I2C_SDA_Low();
    else
        I2C_SDA_High();
    I2C_Delay();
    I2C_SCL_High();
    I2C_Delay();
    I2C_SCL_Low();
    I2C_Delay();

    return data;
}

void ADS1115_Init(void)
{
    GPIO_ClockEnable(HAL_GPIO_PORT(ADS1115_SCL_PIN_ENC));
    GPIO_ClockEnable(HAL_GPIO_PORT(ADS1115_SDA_PIN_ENC));
    GPIO_ClockEnable(HAL_GPIO_PORT(ADS1115_ALERT_PIN_ENC));

    Hal_GPIO_Init(ADS1115_SCL_PIN_ENC, HAL_GPIO_MODE_OUTPUT_OD, HAL_GPIO_SPEED_HIGH, 0);
    Hal_GPIO_Init(ADS1115_SDA_PIN_ENC, HAL_GPIO_MODE_OUTPUT_OD, HAL_GPIO_SPEED_HIGH, 0);
    Hal_GPIO_Init(ADS1115_ALERT_PIN_ENC, HAL_GPIO_MODE_INPUT_PU, HAL_GPIO_SPEED_HIGH, 0);

    I2C_SCL_High();
    I2C_SDA_High();
}

uint8_t ADS1115_WriteReg(uint8_t reg, uint16_t value)
{
    I2C_Start();
    if (I2C_WriteByte(ADS1115_I2C_ADDR << 1)) { I2C_Stop(); return 1; }
    if (I2C_WriteByte(reg)) { I2C_Stop(); return 1; }
    if (I2C_WriteByte((uint8_t)(value >> 8))) { I2C_Stop(); return 1; }
    if (I2C_WriteByte((uint8_t)(value & 0xFF))) { I2C_Stop(); return 1; }
    I2C_Stop();
    return 0;
}

uint16_t ADS1115_ReadReg(uint8_t reg)
{
    uint16_t value;

    I2C_Start();
    if (I2C_WriteByte(ADS1115_I2C_ADDR << 1)) { I2C_Stop(); return 0; }
    if (I2C_WriteByte(reg)) { I2C_Stop(); return 0; }

    I2C_Start();
    if (I2C_WriteByte((ADS1115_I2C_ADDR << 1) | 1)) { I2C_Stop(); return 0; }
    value = (uint16_t)I2C_ReadByte(1) << 8;
    value |= I2C_ReadByte(0);
    I2C_Stop();

    return value;
}

int16_t ADS1115_ReadChannel(uint8_t channel)
{
    uint16_t mux;
    uint16_t config;
    uint16_t cfg_read;


    switch (channel)
    {
        case 0: mux = ADS1115_MUX_AIN0_GND; break;
        case 1: mux = ADS1115_MUX_AIN1_GND; break;
        case 2: mux = ADS1115_MUX_AIN2_GND; break;
        case 3: mux = ADS1115_MUX_AIN3_GND; break;
        default: return 0;
    }

    config = ADS1115_OS_START | mux | ADS1115_PGA_6_144V |
             ADS1115_MODE_SINGLE | ADS1115_DR_128SPS;

    if (ADS1115_WriteReg(ADS1115_REG_CONFIG, config))
        return 0;

    Delay_Ms(10);

    cfg_read = ADS1115_ReadReg(ADS1115_REG_CONFIG);
    if ((cfg_read & 0x8000) == 0)
        return 0;

    return (int16_t)ADS1115_ReadReg(ADS1115_REG_CONVERSION);
}
