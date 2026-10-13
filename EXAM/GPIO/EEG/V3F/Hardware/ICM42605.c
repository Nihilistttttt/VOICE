#include "ICM42605.h"
#include "Config.h"
#include "debug.h"
#include "OLED.h"
#include "posture_detect.h"
#include "patient_monitor.h"
#include <math.h>

#ifdef HAS_ICM42605


#define ICM42605_CS_LOW()            Hal_GPIO_Reset(ICM42605_CS_PIN_ENC)
#define ICM42605_CS_HIGH()           Hal_GPIO_Set(ICM42605_CS_PIN_ENC)

/* 防止硬件异常时永久卡死在SPI状态等待中 */
#define ICM42605_SPI_INSTANCE_TIMEOUT         100000UL

/*
 * 角度积分参数
 * angle_deg10 = sum(raw * 61) / 40000
 */
#define ICM42605_ANGLE_NUM_PER_LSB     61
#define ICM42605_ANGLE_DEN             40000
#define ICM42605_ANGLE_FULL_TURN_DEG10 3600
#define ICM42605_ANGLE_FULL_TURN_NUMERATOR 144000000L

/* 静止死区：20 LSB ≈ 0.305 dps */
#define ICM42605_GYRO_DEAD_ZONE_LSB    20

/*
 * 角度支路滤波参数（100Hz采样）：
 * 1) 一阶低通约10Hz，抑制积分前的高频噪声；
 * 2) 加速度模长接近1g且三轴角速度都很小时判为静止；
 * 3) 连续静止2秒后，才允许零偏/温度系数归一化LMS更新。
 *
 * 这些参数只作用于OLED和DEBUG三轴角，不改变姿态/翻身/坠床支路。
 */
#define ICM42605_GYRO_LPF_ALPHA              0.3859f
#define ICM42605_STILL_ACC_MIN_G              0.92f
#define ICM42605_STILL_ACC_MAX_G              1.08f
#define ICM42605_STILL_GYRO_MAX_LSB           98.0f
#define ICM42605_STILL_HOLD_SAMPLES           200U
#define ICM42605_BIAS_TEMP_LMS_GAIN           0.0020f
#define ICM42605_BIAS_BASE_TRACK_LIMIT_LSB    200.0f
#define ICM42605_TEMP_SLOPE_LIMIT_LSB_PER_C   10.0f
#define ICM42605_TEMP_UPDATE_PERIOD_MS        1000U
#define ICM42605_TEMP_LPF_ALPHA                0.10f
#define ICM42605_TEMP_VALID_MIN_C             (-50.0f)
#define ICM42605_TEMP_VALID_MAX_C             110.0f

/* ICM采样周期和OLED显示周期 */
#define ICM42605_SAMPLE_PERIOD_MS      10U
#define ICM42605_OLED_PERIOD_MS        200U


/* 默认量程对应的规格书灵敏度 */
#define ICM42605_ACC_SCALE_G         0.000122f  /* ±4g: 0.122 mg/LSB */
#define ICM42605_GYR_SCALE_DPS       0.01525f   /* ±500dps: 15.25 mdps/LSB */

static ICM42605_Status ICM42605_SPI_INSTANCE_TransferByte(uint8_t tx_data, uint8_t *rx_data);
static ICM42605_Status ICM42605_SPI_INSTANCE_WaitNotBusy(void);
static void ICM42605_SPI_INSTANCE_ClearRxNE(void);

static void ICM42605_DMA_Init(void);
static ICM42605_Status ICM42605_DMA_StartReadRaw(void);
static void ICM42605_DMA_StopAndReleaseCS(void);
static void ICM42605_DMA_ParseRaw(ICM42605_RawData *raw);

static ICM42605_Status ICM42605_CalibrateGyroZero(void);
static void ICM42605_UpdateAngleFromRaw(const ICM42605_RawData *raw);
static void ICM42605_AngleFilterReset(void);
static void ICM42605_UpdateAngleTemperature(void);
static int32_t ICM42605_WrapAngleNumerator(int32_t numerator);
static int32_t ICM42605_AngleNumeratorToDeg10(int32_t numerator);
static int32_t ICM42605_RoundFloatToInt32(float value);
static float ICM42605_ClampFloat(float value, float min_value, float max_value);


static int16_t ICM42605_CombineInt16(uint8_t high_byte, uint8_t low_byte);
static ICM42605_Status ICM42605_ReadInt16Single(
    uint8_t high_reg,
    uint8_t low_reg,
    int16_t *value
) __attribute__((unused));
static ICM42605_Status ICM42605_VerifyRegister(
    uint8_t reg,
    uint8_t expected
);

/* ===================== ICM42605 DMA/任务状态 ===================== */
static uint8_t icm42605_dma_tx_buf[ICM42605_DMA_FRAME_SIZE];
static uint8_t icm42605_dma_rx_buf[ICM42605_DMA_FRAME_SIZE];

volatile uint8_t g_icm42605_initialized = 0U;
volatile uint8_t g_icm42605_sample_request = 0U;
volatile uint8_t g_icm42605_oled_request = 0U;
volatile uint8_t g_icm42605_dma_busy = 0U;
volatile uint8_t g_icm42605_dma_data_ready = 0U;

volatile uint32_t g_icm42605_dma_ok_count = 0U;
volatile uint32_t g_icm42605_dma_lost_count = 0U;
volatile uint32_t g_icm42605_dma_error_count = 0U;

/*
 * 1ms系统节拍。
 * 在 ICM42605_1msTickISR() 中累加，用于计算IMU实际采样间隔。
 */
volatile uint32_t g_icm42605_ms_tick = 0U;

volatile uint8_t  g_icm42605_pm_event_pending = 0U;
volatile uint8_t  g_icm42605_pm_event_type    = 0U;

/*
 * DMA启动时记录采样时间。
 * 角度积分使用两次DMA采样启动时间之差作为dt_ms。
 */
static uint32_t icm42605_current_sample_ms = 0U;
static uint32_t icm42605_last_sample_ms = 0U;

static int32_t icm42605_gyro_offset_x = 0;
static int32_t icm42605_gyro_offset_y = 0;
static int32_t icm42605_gyro_offset_z = 0;

static int32_t icm42605_angle_x_numerator = 0;
static int32_t icm42605_angle_y_numerator = 0;
static int32_t icm42605_angle_z_numerator = 0;

static int32_t icm42605_angle_x_deg10 = 0;
static int32_t icm42605_angle_y_deg10 = 0;
static int32_t icm42605_angle_z_deg10 = 0;

/* 角度专用的在线零偏、温度模型和低通状态；不覆盖原PM使用的启动零偏。 */
static float icm42605_angle_bias_base_x_lsb = 0.0f;
static float icm42605_angle_bias_base_y_lsb = 0.0f;
static float icm42605_angle_bias_base_z_lsb = 0.0f;
static float icm42605_angle_temp_slope_x_lsb_per_c = 0.0f;
static float icm42605_angle_temp_slope_y_lsb_per_c = 0.0f;
static float icm42605_angle_temp_slope_z_lsb_per_c = 0.0f;
static float icm42605_angle_temperature_ref_c = 0.0f;
static float icm42605_angle_temperature_c = 0.0f;
static float icm42605_angle_lpf_x_lsb = 0.0f;
static float icm42605_angle_lpf_y_lsb = 0.0f;
static float icm42605_angle_lpf_z_lsb = 0.0f;
static uint16_t icm42605_angle_still_samples = 0U;
static uint8_t icm42605_angle_lpf_initialized = 0U;
static uint8_t icm42605_angle_temperature_valid = 0U;
static uint32_t icm42605_angle_last_temp_tick = 0U;


/**
 * @brief  配置CH32H417的SPI4和对应GPIO。
 * @note   SPI模式3：CPOL=1、CPHA=1；8位；MSB先发；软件CS。
 */
void ICM42605_SPI4_Init(void)
{
    SPI_InitTypeDef SPI_InitStructure = {0};

    GPIO_ClockEnable(ICM42605_CS_PORT);
    GPIO_ClockEnable(ICM42605_SCK_PORT);
    GPIO_ClockEnable(ICM42605_MISO_PORT);
    GPIO_ClockEnable(ICM42605_MOSI_PORT);
    RCC_HB1PeriphClockCmd(RCC_HB1Periph_SPI4, ENABLE);

    Hal_GPIO_Init(ICM42605_CS_PIN_ENC, HAL_GPIO_MODE_OUTPUT_PP, HAL_GPIO_SPEED_VERY_HIGH, 0);
    ICM42605_CS_HIGH();

    Hal_GPIO_Init(ICM42605_SCK_PIN_ENC, HAL_GPIO_MODE_AF_PP, HAL_GPIO_SPEED_VERY_HIGH, ICM42605_SCK_AF);
    Hal_GPIO_Init(ICM42605_MOSI_PIN_ENC, HAL_GPIO_MODE_AF_PP, HAL_GPIO_SPEED_VERY_HIGH, ICM42605_MOSI_AF);
    Hal_GPIO_Init(ICM42605_MISO_PIN_ENC, HAL_GPIO_MODE_AF_INPUT, HAL_GPIO_SPEED_VERY_HIGH, ICM42605_MISO_AF);

    SPI_I2S_DeInit(ICM42605_SPI_INSTANCE);

    SPI_InitStructure.SPI_Direction = SPI_Direction_2Lines_FullDuplex;
    SPI_InitStructure.SPI_Mode = SPI_Mode_Master;
    SPI_InitStructure.SPI_DataSize = SPI_DataSize_8b;

    /*
     * 规格书时序：时钟空闲为高，下降沿更新数据，上升沿采样数据。
     * 对应SPI Mode 3：CPOL=1，CPHA=1。
     */
    SPI_InitStructure.SPI_CPOL = SPI_CPOL_High;
    SPI_InitStructure.SPI_CPHA = SPI_CPHA_2Edge;

    SPI_InitStructure.SPI_NSS = SPI_NSS_Soft;

    /*
     * 调试阶段使用较低SPI时钟。
     * Mode6对应较大的分频系数，可明显低于传感器10MHz上限。
     */
    SPI_InitStructure.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_Mode6;

    SPI_InitStructure.SPI_FirstBit = SPI_FirstBit_MSB;
    SPI_InitStructure.SPI_CRCPolynomial = 7;

    SPI_Init(ICM42605_SPI_INSTANCE, &SPI_InitStructure);
    SPI_NSSInternalSoftwareConfig(ICM42605_SPI_INSTANCE, SPI_NSSInternalSoft_Set);
    SPI_Cmd(ICM42605_SPI_INSTANCE, ENABLE);
}

/**
 * @brief  写入配置后读取寄存器核对。
 */
static ICM42605_Status ICM42605_VerifyRegister(
    uint8_t reg,
    uint8_t expected
)
{
    uint8_t actual = 0U;
    ICM42605_Status status;

    status = ICM42605_ReadRegister(reg, &actual);

    if(status != ICM42605_OK)
    {
        return status;
    }

    if(actual != expected)
    {
        return ICM42605_ERROR_VERIFY;
    }

    return ICM42605_OK;
}

/**
 * @brief  分别读取高、低寄存器，绕过连续地址自增，用于排查原始数据。
 */
static ICM42605_Status ICM42605_ReadInt16Single(
    uint8_t high_reg,
    uint8_t low_reg,
    int16_t *value
)
{
    uint8_t high_byte = 0U;
    uint8_t low_byte = 0U;
    ICM42605_Status status;

    if(value == 0)
    {
        return ICM42605_ERROR_PARAM;
    }

    status = ICM42605_ReadRegister(high_reg, &high_byte);
    if(status != ICM42605_OK)
    {
        return status;
    }

    status = ICM42605_ReadRegister(low_reg, &low_byte);
    if(status != ICM42605_OK)
    {
        return status;
    }

    *value = ICM42605_CombineInt16(high_byte, low_byte);

    return ICM42605_OK;
}

/**
 * @brief  初始化ICM-42605。
 * @return ICM42605_OK表示初始化成功。
 */
ICM42605_Status ICM42605_Init(void)
{
    uint8_t who_am_i = 0;
    uint8_t data_stat = 0;
    ICM42605_Status status;

    ICM42605_SPI4_Init();

    /* 上电后给传感器留出启动时间 */
    Delay_Ms(20);

    status = ICM42605_WriteRegister(ICM42605_REG_SOFT_RST, 0xA5U);
    if(status != ICM42605_OK)
    {
        return status;
    }

    Delay_Ms(20);

    status = ICM42605_ReadWhoAmI(&who_am_i);
    if(status != ICM42605_OK)
    {
        return status;
    }

    if(who_am_i != ICM42605_WHO_AM_I_VALUE)
    {
        return ICM42605_ERROR_ID;
    }

    /*
     * BDU=1，地址自动递增=1，主SPI使用四线模式。
     */
    status = ICM42605_WriteRegister(
        ICM42605_REG_COM_CFG,
        ICM42605_COM_CFG_DEFAULT
    );
    if(status != ICM42605_OK)
    {
        return status;
    }

    status = ICM42605_VerifyRegister(
        ICM42605_REG_COM_CFG,
        ICM42605_COM_CFG_DEFAULT
    );
    if(status != ICM42605_OK)
    {
        return status;
    }

    /*
     * 按规格书要求，0x7D先写0x0E：
     * TEMP_EN=1，ACC_EN=1，GYR_EN=1。
     */
    status = ICM42605_WriteRegister(
        ICM42605_REG_PWR_CTRL,
        ICM42605_PWR_CTRL_DEFAULT
    );
    if(status != ICM42605_OK)
    {
        return status;
    }

    Delay_Ms(10);

    status = ICM42605_VerifyRegister(
        ICM42605_REG_PWR_CTRL,
        ICM42605_PWR_CTRL_DEFAULT
    );
    if(status != ICM42605_OK)
    {
        return status;
    }

    /* 加速度计：高性能、100Hz、NORM_AVG4 */
    status = ICM42605_WriteRegister(
        ICM42605_REG_ACC_CONF,
        ICM42605_ACC_CONF_DEFAULT
    );
    if(status != ICM42605_OK)
    {
        return status;
    }

    /* 加速度计量程：±4g */
    status = ICM42605_WriteRegister(
        ICM42605_REG_ACC_RANGE,
        ICM42605_ACC_RANGE_DEFAULT
    );
    if(status != ICM42605_OK)
    {
        return status;
    }

    /* 陀螺仪：高性能、100Hz、NORM_AVG4 */
    status = ICM42605_WriteRegister(
        ICM42605_REG_GYR_CONF,
        ICM42605_GYR_CONF_DEFAULT
    );
    if(status != ICM42605_OK)
    {
        return status;
    }

    /* 陀螺仪量程：±500dps */
    status = ICM42605_WriteRegister(
        ICM42605_REG_GYR_RANGE,
        ICM42605_GYR_RANGE_DEFAULT
    );
    if(status != ICM42605_OK)
    {
        return status;
    }

    status = ICM42605_VerifyRegister(
        ICM42605_REG_ACC_CONF,
        ICM42605_ACC_CONF_DEFAULT
    );
    if(status != ICM42605_OK)
    {
        return status;
    }

    status = ICM42605_VerifyRegister(
        ICM42605_REG_ACC_RANGE,
        ICM42605_ACC_RANGE_DEFAULT
    );
    if(status != ICM42605_OK)
    {
        return status;
    }

    status = ICM42605_VerifyRegister(
        ICM42605_REG_GYR_CONF,
        ICM42605_GYR_CONF_DEFAULT
    );
    if(status != ICM42605_OK)
    {
        return status;
    }

    status = ICM42605_VerifyRegister(
        ICM42605_REG_GYR_RANGE,
        ICM42605_GYR_RANGE_DEFAULT
    );
    if(status != ICM42605_OK)
    {
        return status;
    }

    /*
     * 规格书给出的100Hz开启时间典型值约50ms。
     */
    Delay_Ms(50);

    status = ICM42605_ReadRegister(ICM42605_REG_DATA_STAT, &data_stat);
    if(status != ICM42605_OK)
    {
        return status;
    }

    if((data_stat & (ICM42605_DATA_STAT_ACC_ERR |
                     ICM42605_DATA_STAT_GYR_ERR)) != 0U)
    {
        return ICM42605_ERROR_CONFIG;
    }

    return ICM42605_OK;
}

/**
 * @brief  写一个寄存器。
 */
ICM42605_Status ICM42605_WriteRegister(uint8_t reg, uint8_t value)
{
    uint8_t dummy;
    ICM42605_Status status;

    ICM42605_CS_LOW();

    /* 写命令：最高位为0，低7位为寄存器地址 */
    status = ICM42605_SPI_INSTANCE_TransferByte((uint8_t)(reg & 0x7FU), &dummy);
    if(status == ICM42605_OK)
    {
        status = ICM42605_SPI_INSTANCE_TransferByte(value, &dummy);
    }

    if(status == ICM42605_OK)
    {
        status = ICM42605_SPI_INSTANCE_WaitNotBusy();
    }

    ICM42605_CS_HIGH();

    /*
     * 规格书建议寄存器配置完成后延时1ms再读。
     * 这里统一保留1ms，初始化阶段更稳定。
     */
    if(status == ICM42605_OK)
    {
        Delay_Ms(1);
    }

    return status;
}

/**
 * @brief  从指定寄存器开始连续读取。
 */
ICM42605_Status ICM42605_ReadRegisters(
    uint8_t reg,
    uint8_t *data,
    uint16_t length
)
{
    uint16_t i;
    uint8_t dummy;
    ICM42605_Status status;

    if((data == 0) || (length == 0U))
    {
        return ICM42605_ERROR_PARAM;
    }

    ICM42605_CS_LOW();

    /* 读命令：最高位置1 */
    status = ICM42605_SPI_INSTANCE_TransferByte((uint8_t)(reg | 0x80U), &dummy);

    for(i = 0; (i < length) && (status == ICM42605_OK); i++)
    {
        status = ICM42605_SPI_INSTANCE_TransferByte(0xFFU, &data[i]);
    }

    if(status == ICM42605_OK)
    {
        status = ICM42605_SPI_INSTANCE_WaitNotBusy();
    }

    ICM42605_CS_HIGH();

    return status;
}

/**
 * @brief  读取一个寄存器。
 */
ICM42605_Status ICM42605_ReadRegister(uint8_t reg, uint8_t *value)
{
    return ICM42605_ReadRegisters(reg, value, 1U);
}

/**
 * @brief  读取WHO_AM_I。
 */
ICM42605_Status ICM42605_ReadWhoAmI(uint8_t *who_am_i)
{
    if(who_am_i == 0)
    {
        return ICM42605_ERROR_PARAM;
    }

    return ICM42605_ReadRegister(ICM42605_REG_WHO_AM_I, who_am_i);
}

/**
 * @brief  查询加速度计和陀螺仪是否都产生了新数据。
 * @return 1：两者均有新数据；0：尚未同时准备好或通信失败。
 */
uint8_t ICM42605_DataReady(void)
{
    uint8_t status_reg = 0;

    if(ICM42605_ReadRegister(ICM42605_REG_DATA_STAT, &status_reg) !=
       ICM42605_OK)
    {
        return 0U;
    }

    if((status_reg & (ICM42605_DATA_STAT_ACC_RDY |
                      ICM42605_DATA_STAT_GYR_RDY)) ==
       (ICM42605_DATA_STAT_ACC_RDY |
        ICM42605_DATA_STAT_GYR_RDY))
    {
        return 1U;
    }

    return 0U;
}

/**
 * @brief  连续读取三轴加速度和三轴陀螺仪原始数据。
 */
ICM42605_Status ICM42605_ReadRaw(ICM42605_RawData *raw)
{
    uint8_t buffer[12];
    ICM42605_Status status;

    if(raw == 0)
    {
        return ICM42605_ERROR_PARAM;
    }

    status = ICM42605_ReadRegisters(
        ICM42605_REG_ACC_XH,
        buffer,
        sizeof(buffer)
    );

    if(status != ICM42605_OK)
    {
        return status;
    }

    raw->acc_x = ICM42605_CombineInt16(buffer[0], buffer[1]);
    raw->acc_y = ICM42605_CombineInt16(buffer[2], buffer[3]);
    raw->acc_z = ICM42605_CombineInt16(buffer[4], buffer[5]);

    raw->gyro_x = ICM42605_CombineInt16(buffer[6], buffer[7]);
    raw->gyro_y = ICM42605_CombineInt16(buffer[8], buffer[9]);
    raw->gyro_z = ICM42605_CombineInt16(buffer[10], buffer[11]);

    return ICM42605_OK;
}

/**
 * @brief  读取并换算成g和dps。
 */
ICM42605_Status ICM42605_ReadData(ICM42605_Data *data)
{
    ICM42605_RawData raw;
    ICM42605_Status status;

    if(data == 0)
    {
        return ICM42605_ERROR_PARAM;
    }

    status = ICM42605_ReadRaw(&raw);
    if(status != ICM42605_OK)
    {
        return status;
    }

    data->acc_x_g = (float)raw.acc_x * ICM42605_ACC_SCALE_G;
    data->acc_y_g = (float)raw.acc_y * ICM42605_ACC_SCALE_G;
    data->acc_z_g = (float)raw.acc_z * ICM42605_ACC_SCALE_G;

    data->gyro_x_dps = (float)raw.gyro_x * ICM42605_GYR_SCALE_DPS;
    data->gyro_y_dps = (float)raw.gyro_y * ICM42605_GYR_SCALE_DPS;
    data->gyro_z_dps = (float)raw.gyro_z * ICM42605_GYR_SCALE_DPS;

    return ICM42605_OK;
}

/**
 * @brief  读取芯片温度。
 * @note   规格书公式：T = raw / 512 + 23摄氏度。
 */
ICM42605_Status ICM42605_ReadTemperature(float *temperature_c)
{
    uint8_t buffer[2];
    int16_t raw_temperature;
    ICM42605_Status status;

    if(temperature_c == 0)
    {
        return ICM42605_ERROR_PARAM;
    }

    status = ICM42605_ReadRegisters(
        ICM42605_REG_TEMP_H,
        buffer,
        sizeof(buffer)
    );

    if(status != ICM42605_OK)
    {
        return status;
    }

    raw_temperature = ICM42605_CombineInt16(buffer[0], buffer[1]);
    *temperature_c = ((float)raw_temperature / 512.0f) + 23.0f;

    return ICM42605_OK;
}

/**
 * @brief  初始化角度支路的零偏、温度模型和低通状态。
 * @note   HXY寄存器表与温度换算保持原驱动定义，不套用TDK标准寄存器表。
 */
static void ICM42605_AngleFilterReset(void)
{
    float temperature_c;

    icm42605_angle_bias_base_x_lsb = (float)icm42605_gyro_offset_x;
    icm42605_angle_bias_base_y_lsb = (float)icm42605_gyro_offset_y;
    icm42605_angle_bias_base_z_lsb = (float)icm42605_gyro_offset_z;

    icm42605_angle_temp_slope_x_lsb_per_c = 0.0f;
    icm42605_angle_temp_slope_y_lsb_per_c = 0.0f;
    icm42605_angle_temp_slope_z_lsb_per_c = 0.0f;

    icm42605_angle_lpf_x_lsb = 0.0f;
    icm42605_angle_lpf_y_lsb = 0.0f;
    icm42605_angle_lpf_z_lsb = 0.0f;
    icm42605_angle_lpf_initialized = 0U;
    icm42605_angle_still_samples = 0U;
    icm42605_angle_last_temp_tick = 0U;

    if((ICM42605_ReadTemperature(&temperature_c) == ICM42605_OK) &&
       (temperature_c >= ICM42605_TEMP_VALID_MIN_C) &&
       (temperature_c <= ICM42605_TEMP_VALID_MAX_C))
    {
        icm42605_angle_temperature_ref_c = temperature_c;
        icm42605_angle_temperature_c = temperature_c;
        icm42605_angle_temperature_valid = 1U;
    }
    else
    {
        icm42605_angle_temperature_ref_c = 23.0f;
        icm42605_angle_temperature_c = 23.0f;
        icm42605_angle_temperature_valid = 0U;
    }
}

/**
 * @brief  低频更新芯片温度缓存，避免在100Hz角度路径里增加SPI阻塞。
 */
static void ICM42605_UpdateAngleTemperature(void)
{
    float temperature_c;

    if((uint32_t)(g_icm42605_ms_tick - icm42605_angle_last_temp_tick) <
       ICM42605_TEMP_UPDATE_PERIOD_MS)
    {
        return;
    }

    icm42605_angle_last_temp_tick = g_icm42605_ms_tick;

    if((ICM42605_ReadTemperature(&temperature_c) == ICM42605_OK) &&
       (temperature_c >= ICM42605_TEMP_VALID_MIN_C) &&
       (temperature_c <= ICM42605_TEMP_VALID_MAX_C))
    {
        if(icm42605_angle_temperature_valid == 0U)
        {
            /* 首次有效温度作为启动零偏的参考温度。 */
            icm42605_angle_temperature_ref_c = temperature_c;
            icm42605_angle_temperature_c = temperature_c;
            icm42605_angle_temperature_valid = 1U;
        }
        else
        {
            /* 1Hz温度数据再做慢速低通，避免单次温度毛刺扰动零偏模型。 */
            icm42605_angle_temperature_c += ICM42605_TEMP_LPF_ALPHA *
                (temperature_c - icm42605_angle_temperature_c);
        }
    }
}

/**
 * @brief  把积分累加器限制在[0, 360度)范围。
 */
static int32_t ICM42605_WrapAngleNumerator(int32_t numerator)
{
    while(numerator >= ICM42605_ANGLE_FULL_TURN_NUMERATOR)
    {
        numerator -= ICM42605_ANGLE_FULL_TURN_NUMERATOR;
    }
    while(numerator < 0)
    {
        numerator += ICM42605_ANGLE_FULL_TURN_NUMERATOR;
    }
    return numerator;
}

/**
 * @brief  float四舍五入到int32，避免依赖额外数学库接口。
 */
static int32_t ICM42605_RoundFloatToInt32(float value)
{
    if(value >= 0.0f)
    {
        return (int32_t)(value + 0.5f);
    }
    return (int32_t)(value - 0.5f);
}

static float ICM42605_ClampFloat(float value, float min_value, float max_value)
{
    if(value < min_value)
    {
        return min_value;
    }
    if(value > max_value)
    {
        return max_value;
    }
    return value;
}


/**
 * @brief  把角度积分累加器换算为0.1度，并限制到0.0~359.9度。
 */
static int32_t ICM42605_AngleNumeratorToDeg10(int32_t numerator)
{
    int32_t angle_deg10;

    numerator = ICM42605_WrapAngleNumerator(numerator);
    numerator += (ICM42605_ANGLE_DEN / 2);
    angle_deg10 = (int32_t)(numerator / ICM42605_ANGLE_DEN);

    if(angle_deg10 >= ICM42605_ANGLE_FULL_TURN_DEG10)
    {
        angle_deg10 = 0;
    }
    return angle_deg10;
}

#ifndef EEG_OLED_DISABLED
/**
 * @brief  OLED显示一轴角度。
 * @note   显示格式：X:+0123.4deg
 */
static void ICM42605_OLED_ShowAngleAxis(
    uint8_t line,
    char axis,
    int32_t angle_deg10
)
{
    uint32_t absolute_value;

    if(angle_deg10 > 99999)
    {
        angle_deg10 = 99999;
    }
    else if(angle_deg10 < -99999)
    {
        angle_deg10 = -99999;
    }

    OLED_ShowChar(SPI, line, 0, axis);
    OLED_ShowChar(SPI, line, 1, ':');

    if(angle_deg10 >= 0)
    {
        OLED_ShowChar(SPI, line, 2, '+');
        absolute_value = (uint32_t)angle_deg10;
    }
    else
    {
        OLED_ShowChar(SPI, line, 2, '-');
        absolute_value = (uint32_t)(-angle_deg10);
    }

    OLED_ShowNum(SPI, line, 3, absolute_value / 10U, 4);
    OLED_ShowChar(SPI, line, 7, '.');
    OLED_ShowNum(SPI, line, 8, absolute_value % 10U, 1);
    OLED_ShowString(SPI, line, 9, "deg   ");
}
#endif /* EEG_OLED_DISABLED */

/**
 * @brief  OLED低频显示当前角度。
 * @note   只能在主循环里调用，不能放进中断。
 */
#ifndef EEG_OLED_DISABLED
static void ICM42605_OLED_ShowAngle(void)
{
    OLED_ShowString(SPI, 0, 0, "ANGLE OFFSET    ");
    ICM42605_OLED_ShowAngleAxis(1, 'X', icm42605_angle_x_deg10);
    ICM42605_OLED_ShowAngleAxis(2, 'Y', icm42605_angle_y_deg10);
    ICM42605_OLED_ShowAngleAxis(3, 'Z', icm42605_angle_z_deg10);
}
#endif /* EEG_OLED_DISABLED */

/**
 * @brief  校准陀螺仪零偏。
 * @note   本函数会阻塞约2秒，必须在脑电正式开始采集前调用。
 */
static ICM42605_Status ICM42605_CalibrateGyroZero(void)
{
    ICM42605_RawData raw;
    ICM42605_Status status;
    uint16_t i;
    uint16_t valid_count = 0U;

    int32_t sum_x = 0;
    int32_t sum_y = 0;
    int32_t sum_z = 0;

#ifndef EEG_OLED_DISABLED
    OLED_Clear(SPI);
    OLED_ShowString(SPI, 0, 0, "SET ZERO POINT");
    OLED_ShowString(SPI, 1, 0, "KEEP STILL");
    OLED_ShowString(SPI, 2, 0, "ABOUT 2 SECOND");
#endif

    Delay_Ms(100);

    for(i = 0U; i < 200U; i++)
    {
        status = ICM42605_ReadRaw(&raw);

        if(status == ICM42605_OK)
        {
            sum_x += (int32_t)raw.gyro_x;
            sum_y += (int32_t)raw.gyro_y;
            sum_z += (int32_t)raw.gyro_z;
            valid_count++;
        }

        Delay_Ms(10);
    }

    if(valid_count == 0U)
    {
        return ICM42605_ERROR_CONFIG;
    }

    icm42605_gyro_offset_x = sum_x / (int32_t)valid_count;
    icm42605_gyro_offset_y = sum_y / (int32_t)valid_count;
    icm42605_gyro_offset_z = sum_z / (int32_t)valid_count;

    icm42605_angle_x_numerator = 0;
    icm42605_angle_y_numerator = 0;
    icm42605_angle_z_numerator = 0;

    icm42605_angle_x_deg10 = 0;
    icm42605_angle_y_deg10 = 0;
    icm42605_angle_z_deg10 = 0;

    ICM42605_AngleFilterReset();

    return ICM42605_OK;
}

/**
 * @brief  由一帧原始陀螺仪数据更新相对角度。
 */
static void ICM42605_UpdateAngleFromRaw(const ICM42605_RawData *raw)
{
    float temperature_delta_c;
    float bias_x_lsb;
    float bias_y_lsb;
    float bias_z_lsb;
    float gyro_x_lsb;
    float gyro_y_lsb;
    float gyro_z_lsb;
    float acc_x_g;
    float acc_y_g;
    float acc_z_g;
    float acc_mag_sq;
    float still_min_sq;
    float still_max_sq;
    float lms_step;
    float lms_error_x;
    float lms_error_y;
    float lms_error_z;
    int32_t gyro_x;
    int32_t gyro_y;
    int32_t gyro_z;

    if(raw == 0)
    {
        return;
    }

    temperature_delta_c = 0.0f;
    if(icm42605_angle_temperature_valid != 0U)
    {
        temperature_delta_c =
            icm42605_angle_temperature_c - icm42605_angle_temperature_ref_c;
    }

    bias_x_lsb = icm42605_angle_bias_base_x_lsb +
        icm42605_angle_temp_slope_x_lsb_per_c * temperature_delta_c;
    bias_y_lsb = icm42605_angle_bias_base_y_lsb +
        icm42605_angle_temp_slope_y_lsb_per_c * temperature_delta_c;
    bias_z_lsb = icm42605_angle_bias_base_z_lsb +
        icm42605_angle_temp_slope_z_lsb_per_c * temperature_delta_c;

    gyro_x_lsb = (float)raw->gyro_x - bias_x_lsb;
    gyro_y_lsb = (float)raw->gyro_y - bias_y_lsb;
    gyro_z_lsb = (float)raw->gyro_z - bias_z_lsb;

    acc_x_g = (float)raw->acc_x * ICM42605_ACC_SCALE_G;
    acc_y_g = (float)raw->acc_y * ICM42605_ACC_SCALE_G;
    acc_z_g = (float)raw->acc_z * ICM42605_ACC_SCALE_G;
    acc_mag_sq = acc_x_g * acc_x_g + acc_y_g * acc_y_g + acc_z_g * acc_z_g;
    still_min_sq = ICM42605_STILL_ACC_MIN_G * ICM42605_STILL_ACC_MIN_G;
    still_max_sq = ICM42605_STILL_ACC_MAX_G * ICM42605_STILL_ACC_MAX_G;

    if((acc_mag_sq >= still_min_sq) &&
       (acc_mag_sq <= still_max_sq) &&
       (fabsf(gyro_x_lsb) <= ICM42605_STILL_GYRO_MAX_LSB) &&
       (fabsf(gyro_y_lsb) <= ICM42605_STILL_GYRO_MAX_LSB) &&
       (fabsf(gyro_z_lsb) <= ICM42605_STILL_GYRO_MAX_LSB))
    {
        if(icm42605_angle_still_samples < ICM42605_STILL_HOLD_SAMPLES)
        {
            icm42605_angle_still_samples++;
        }
    }
    else
    {
        icm42605_angle_still_samples = 0U;
    }

    if(icm42605_angle_still_samples >= ICM42605_STILL_HOLD_SAMPLES)
    {
        /*
         * 静止时真实角速度应为0，原始读数就是当前零偏观测。
         * 用[1, 温差]作为特征做归一化LMS，可同时跟踪常值零偏和
         * 每轴随温度变化的斜率；运动时完全冻结模型。
         */
        lms_step = ICM42605_BIAS_TEMP_LMS_GAIN /
            (1.0f + temperature_delta_c * temperature_delta_c);
        lms_error_x = (float)raw->gyro_x - bias_x_lsb;
        lms_error_y = (float)raw->gyro_y - bias_y_lsb;
        lms_error_z = (float)raw->gyro_z - bias_z_lsb;

        icm42605_angle_bias_base_x_lsb +=
            lms_step * lms_error_x;
        icm42605_angle_bias_base_y_lsb +=
            lms_step * lms_error_y;
        icm42605_angle_bias_base_z_lsb +=
            lms_step * lms_error_z;

        icm42605_angle_temp_slope_x_lsb_per_c +=
            lms_step * lms_error_x * temperature_delta_c;
        icm42605_angle_temp_slope_y_lsb_per_c +=
            lms_step * lms_error_y * temperature_delta_c;
        icm42605_angle_temp_slope_z_lsb_per_c +=
            lms_step * lms_error_z * temperature_delta_c;

        /* 防止误静止或异常温度读数使模型参数无限漂移。 */
        icm42605_angle_bias_base_x_lsb = ICM42605_ClampFloat(
            icm42605_angle_bias_base_x_lsb,
            (float)icm42605_gyro_offset_x - ICM42605_BIAS_BASE_TRACK_LIMIT_LSB,
            (float)icm42605_gyro_offset_x + ICM42605_BIAS_BASE_TRACK_LIMIT_LSB);
        icm42605_angle_bias_base_y_lsb = ICM42605_ClampFloat(
            icm42605_angle_bias_base_y_lsb,
            (float)icm42605_gyro_offset_y - ICM42605_BIAS_BASE_TRACK_LIMIT_LSB,
            (float)icm42605_gyro_offset_y + ICM42605_BIAS_BASE_TRACK_LIMIT_LSB);
        icm42605_angle_bias_base_z_lsb = ICM42605_ClampFloat(
            icm42605_angle_bias_base_z_lsb,
            (float)icm42605_gyro_offset_z - ICM42605_BIAS_BASE_TRACK_LIMIT_LSB,
            (float)icm42605_gyro_offset_z + ICM42605_BIAS_BASE_TRACK_LIMIT_LSB);

        icm42605_angle_temp_slope_x_lsb_per_c = ICM42605_ClampFloat(
            icm42605_angle_temp_slope_x_lsb_per_c,
            -ICM42605_TEMP_SLOPE_LIMIT_LSB_PER_C,
            ICM42605_TEMP_SLOPE_LIMIT_LSB_PER_C);
        icm42605_angle_temp_slope_y_lsb_per_c = ICM42605_ClampFloat(
            icm42605_angle_temp_slope_y_lsb_per_c,
            -ICM42605_TEMP_SLOPE_LIMIT_LSB_PER_C,
            ICM42605_TEMP_SLOPE_LIMIT_LSB_PER_C);
        icm42605_angle_temp_slope_z_lsb_per_c = ICM42605_ClampFloat(
            icm42605_angle_temp_slope_z_lsb_per_c,
            -ICM42605_TEMP_SLOPE_LIMIT_LSB_PER_C,
            ICM42605_TEMP_SLOPE_LIMIT_LSB_PER_C);

        bias_x_lsb = icm42605_angle_bias_base_x_lsb +
            icm42605_angle_temp_slope_x_lsb_per_c * temperature_delta_c;
        bias_y_lsb = icm42605_angle_bias_base_y_lsb +
            icm42605_angle_temp_slope_y_lsb_per_c * temperature_delta_c;
        bias_z_lsb = icm42605_angle_bias_base_z_lsb +
            icm42605_angle_temp_slope_z_lsb_per_c * temperature_delta_c;

        gyro_x_lsb = (float)raw->gyro_x - bias_x_lsb;
        gyro_y_lsb = (float)raw->gyro_y - bias_y_lsb;
        gyro_z_lsb = (float)raw->gyro_z - bias_z_lsb;
    }

    if(icm42605_angle_lpf_initialized == 0U)
    {
        icm42605_angle_lpf_x_lsb = gyro_x_lsb;
        icm42605_angle_lpf_y_lsb = gyro_y_lsb;
        icm42605_angle_lpf_z_lsb = gyro_z_lsb;
        icm42605_angle_lpf_initialized = 1U;
    }
    else
    {
        icm42605_angle_lpf_x_lsb += ICM42605_GYRO_LPF_ALPHA *
            (gyro_x_lsb - icm42605_angle_lpf_x_lsb);
        icm42605_angle_lpf_y_lsb += ICM42605_GYRO_LPF_ALPHA *
            (gyro_y_lsb - icm42605_angle_lpf_y_lsb);
        icm42605_angle_lpf_z_lsb += ICM42605_GYRO_LPF_ALPHA *
            (gyro_z_lsb - icm42605_angle_lpf_z_lsb);
    }

    gyro_x = ICM42605_RoundFloatToInt32(icm42605_angle_lpf_x_lsb);
    gyro_y = ICM42605_RoundFloatToInt32(icm42605_angle_lpf_y_lsb);
    gyro_z = ICM42605_RoundFloatToInt32(icm42605_angle_lpf_z_lsb);

    if((gyro_x >= -ICM42605_GYRO_DEAD_ZONE_LSB) &&
       (gyro_x <=  ICM42605_GYRO_DEAD_ZONE_LSB))
    {
        gyro_x = 0;
    }

    if((gyro_y >= -ICM42605_GYRO_DEAD_ZONE_LSB) &&
       (gyro_y <=  ICM42605_GYRO_DEAD_ZONE_LSB))
    {
        gyro_y = 0;
    }

    if((gyro_z >= -ICM42605_GYRO_DEAD_ZONE_LSB) &&
       (gyro_z <=  ICM42605_GYRO_DEAD_ZONE_LSB))
    {
        gyro_z = 0;
    }

    /*
     * 保留最初测试驱动的固定100Hz积分比例：
     *
     * angle_numerator 每次累加 raw × 61
     * 除以40000后得到0.1度。
     *
     * 这里不再使用dt_ms，避免Timer计时误差把角度比例拉偏。
     * 关键是保证 Timer_1ms.c 确实每1ms中断一次。
     */
    icm42605_angle_x_numerator = ICM42605_WrapAngleNumerator(
        icm42605_angle_x_numerator +
        gyro_x * ICM42605_ANGLE_NUM_PER_LSB);
    icm42605_angle_y_numerator = ICM42605_WrapAngleNumerator(
        icm42605_angle_y_numerator +
        gyro_y * ICM42605_ANGLE_NUM_PER_LSB);
    icm42605_angle_z_numerator = ICM42605_WrapAngleNumerator(
        icm42605_angle_z_numerator +
        gyro_z * ICM42605_ANGLE_NUM_PER_LSB);

    icm42605_angle_x_deg10 =
        ICM42605_AngleNumeratorToDeg10(icm42605_angle_x_numerator);
    icm42605_angle_y_deg10 =
        ICM42605_AngleNumeratorToDeg10(icm42605_angle_y_numerator);
    icm42605_angle_z_deg10 =
        ICM42605_AngleNumeratorToDeg10(icm42605_angle_z_numerator);
}

/**
 * @brief  初始化ICM42605脑电联调任务。
 * @note   建议在 ADS1299_Init() 之前调用，避免2秒校准阻塞脑电采集。
 */
ICM42605_Status ICM42605_BCI_Init(void)
{
    ICM42605_Status status;
    uint8_t who_am_i = 0U;

    if(g_icm42605_initialized != 0U)
    {
        return ICM42605_OK;
    }

#ifndef EEG_OLED_DISABLED
    OLED_Init();
    OLED_Clear(SPI);
    OLED_ShowString(SPI, 0, 0, "ICM INIT...");
    OLED_ShowString(SPI, 1, 0, "PLEASE WAIT");
#endif

    status = ICM42605_Init();
    (void)ICM42605_ReadWhoAmI(&who_am_i);

    if(status != ICM42605_OK)
    {
#ifndef EEG_OLED_DISABLED
        OLED_Clear(SPI);
        OLED_ShowString(SPI, 0, 0, "ICM INIT ERROR");
        OLED_ShowString(SPI, 1, 0, "STATUS:");
        OLED_ShowSignedNum(SPI, 1, 7, (int32_t)status, 1);
        OLED_ShowString(SPI, 2, 0, "WHOAMI:0x");
        OLED_ShowHexNum(SPI, 2, 9, who_am_i, 2);
        OLED_ShowString(SPI, 3, 0, "CHECK WIRING");
#endif
        return status;
    }

    status = ICM42605_CalibrateGyroZero();
    if(status != ICM42605_OK)
    {
#ifndef EEG_OLED_DISABLED
        OLED_Clear(SPI);
        OLED_ShowString(SPI, 0, 0, "ICM CAL ERROR");
        OLED_ShowString(SPI, 1, 0, "KEEP STILL");
#endif
        return status;
    }

    ICM42605_DMA_Init();

    Posture_Init(NULL);
    PM_Init(NULL);

    g_icm42605_sample_request = 0U;
    g_icm42605_oled_request = 1U;
    g_icm42605_dma_busy = 0U;
    g_icm42605_dma_data_ready = 0U;

    g_icm42605_dma_ok_count = 0U;
    g_icm42605_dma_lost_count = 0U;
    g_icm42605_dma_error_count = 0U;

    g_icm42605_ms_tick = 0U;
    icm42605_current_sample_ms = 0U;
    icm42605_last_sample_ms = 0U;

    g_icm42605_initialized = 1U;

#ifndef EEG_OLED_DISABLED
    OLED_Clear(SPI);
    ICM42605_OLED_ShowAngle();
#endif

    return ICM42605_OK;
}

/**
 * @brief  1ms节拍函数。
 * @note   放到 SysTick 或 TIM 的1ms中断里调用。
 *         函数内部只置标志，不读SPI，不刷OLED。
 */
void ICM42605_1msTickISR(void)
{
    static uint16_t sample_ms = 0U;
    static uint16_t oled_ms = 0U;

    /*
     * 这个计数即使ICM未初始化也可以累加。
     * 初始化完成后会清零。
     */
    g_icm42605_ms_tick++;

    if(g_icm42605_initialized == 0U)
    {
        return;
    }

    sample_ms++;
    oled_ms++;

    if(sample_ms >= ICM42605_SAMPLE_PERIOD_MS)
    {
        sample_ms = 0U;
        g_icm42605_sample_request = 1U;
    }

    if(oled_ms >= ICM42605_OLED_PERIOD_MS)
    {
        oled_ms = 0U;
        g_icm42605_oled_request = 1U;
    }
}

/**
 * @brief  主循环任务函数。
 * @note   必须放在脑电 ring_buffer_get_frame() 处理之后调用。
 */
void ICM42605_Task(void)
{
    ICM42605_RawData raw;

    if(g_icm42605_initialized == 0U)
    {
        (void)ICM42605_BCI_Init();
        return;
    }

    /*
     * DMA收到完整一帧后，在主循环中解析和积分。
     * 不在DMA中断里计算，避免影响ADS1299。
     */
    if(g_icm42605_dma_data_ready != 0U)
    {
        g_icm42605_dma_data_ready = 0U;

        ICM42605_DMA_ParseRaw(&raw);
        ICM42605_UpdateAngleFromRaw(&raw);

        {
            float ax_g = (float)raw.acc_x * ICM42605_ACC_SCALE_G;
            float ay_g = (float)raw.acc_y * ICM42605_ACC_SCALE_G;
            float az_g = (float)raw.acc_z * ICM42605_ACC_SCALE_G;
            Posture_FeedAccel(ax_g, ay_g, az_g, ICM42605_SAMPLE_PERIOD_MS);

            float gx_dps = (float)(raw.gyro_x - icm42605_gyro_offset_x) * ICM42605_GYR_SCALE_DPS;
            float gy_dps = (float)(raw.gyro_y - icm42605_gyro_offset_y) * ICM42605_GYR_SCALE_DPS;
            float gz_dps = (float)(raw.gyro_z - icm42605_gyro_offset_z) * ICM42605_GYR_SCALE_DPS;
            float acc_mag = sqrtf(ax_g * ax_g + ay_g * ay_g + az_g * az_g);

            PM_Event_t pm_ev = PM_Update(Posture_GetResult(),
                                      gx_dps, gy_dps, gz_dps,
                                      acc_mag,
                                      g_icm42605_ms_tick);
            if (pm_ev != PM_EVENT_NONE) {
                g_icm42605_pm_event_type = (uint8_t)pm_ev;
                g_icm42605_pm_event_pending = 1U;
            }
        }
    }

    /*
     * 温度只需低频更新。仅在SPI DMA空闲时读取，避免与六轴采样争用总线。
     */
    if(g_icm42605_dma_busy == 0U)
    {
        ICM42605_UpdateAngleTemperature();
    }

    /*
     * 10ms采样请求。
     * 若上一次DMA还没完成，则跳过本次IMU采样，保证不拖累脑电。
     */
    if(g_icm42605_sample_request != 0U)
    {
        g_icm42605_sample_request = 0U;

        if(g_icm42605_dma_busy == 0U)
        {
            (void)ICM42605_DMA_StartReadRaw();
        }
        else
        {
            g_icm42605_dma_lost_count++;
        }
    }

    /*
     * OLED低频刷新。
     * OLED是最低优先级，只在主循环里执行。
     */
#ifndef EEG_OLED_DISABLED
    if(g_icm42605_oled_request != 0U)
    {
        g_icm42605_oled_request = 0U;
        ICM42605_OLED_ShowAngle();
    }
#endif
}

/**
 * @brief  兼容原主文件的调用名。
 * @note   这个函数现在不再while(1)，每次调用只执行一次任务片段并立即返回。
 */
void ICM_42605_Read(void)
{
    ICM42605_Task();
}

/**
 * @brief  清零当前角度，把当前位置重新作为0点。
 * @note   只清角度积分，不重新校准零偏。
 */
void ICM42605_ResetAngleZero(void)
{
    icm42605_angle_x_numerator = 0;
    icm42605_angle_y_numerator = 0;
    icm42605_angle_z_numerator = 0;

    icm42605_angle_x_deg10 = 0;
    icm42605_angle_y_deg10 = 0;
    icm42605_angle_z_deg10 = 0;

    icm42605_last_sample_ms = icm42605_current_sample_ms;

    g_icm42605_oled_request = 1U;
}

/**
 * @brief  读取当前角度，单位0.1度，范围0~3599。
 */
void ICM42605_GetAngleDeg10(
    int32_t *x_deg10,
    int32_t *y_deg10,
    int32_t *z_deg10
)
{
    if(x_deg10 != 0)
    {
        *x_deg10 = icm42605_angle_x_deg10;
    }

    if(y_deg10 != 0)
    {
        *y_deg10 = icm42605_angle_y_deg10;
    }

    if(z_deg10 != 0)
    {
        *z_deg10 = icm42605_angle_z_deg10;
    }
}

/**
 * @brief  初始化ICM42605 SPI4 DMA。
 */
static void ICM42605_DMA_Init(void)
{
    DMA_InitTypeDef DMA_InitStructure = {0};

    RCC_HBPeriphClockCmd(RCC_HBPeriph_DMA2, ENABLE);

    SPI_I2S_DMACmd(ICM42605_SPI_INSTANCE, SPI_I2S_DMAReq_Rx, DISABLE);
    SPI_I2S_DMACmd(ICM42605_SPI_INSTANCE, SPI_I2S_DMAReq_Tx, DISABLE);

    DMA_Cmd(ICM42605_RX_DMA_CHANNEL, DISABLE);
    DMA_Cmd(ICM42605_TX_DMA_CHANNEL, DISABLE);

    /* RX DMA：SPI DATAR -> rx buffer */
    DMA_InitStructure.DMA_PeripheralBaseAddr =
        (uint32_t)&(ICM42605_SPI_INSTANCE->DATAR);
    DMA_InitStructure.DMA_Memory0BaseAddr =
        (uint32_t)icm42605_dma_rx_buf;
    DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralSRC;
    DMA_InitStructure.DMA_BufferSize = ICM42605_DMA_FRAME_SIZE;
    DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
    DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
    DMA_InitStructure.DMA_Mode = DMA_Mode_Normal;
    DMA_InitStructure.DMA_M2M = DMA_M2M_Disable;
    DMA_InitStructure.DMA_Priority = DMA_Priority_Low;

    DMA_Init(ICM42605_RX_DMA_CHANNEL, &DMA_InitStructure);
    DMA_MuxChannelConfig(
        ICM42605_RX_DMAMUX_CHANNEL,
        ICM42605_RX_DMA_REQUEST
    );
    DMA_ITConfig(ICM42605_RX_DMA_CHANNEL, DMA_IT_TC, ENABLE);
    DMA_ITConfig(ICM42605_RX_DMA_CHANNEL, DMA_IT_TE, ENABLE);

    /* TX DMA：tx buffer -> SPI DATAR */
    DMA_InitStructure.DMA_PeripheralBaseAddr =
        (uint32_t)&(ICM42605_SPI_INSTANCE->DATAR);
    DMA_InitStructure.DMA_Memory0BaseAddr =
        (uint32_t)icm42605_dma_tx_buf;
    DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralDST;
    DMA_InitStructure.DMA_BufferSize = ICM42605_DMA_FRAME_SIZE;
    DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
    DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
    DMA_InitStructure.DMA_Mode = DMA_Mode_Normal;
    DMA_InitStructure.DMA_M2M = DMA_M2M_Disable;
    DMA_InitStructure.DMA_Priority = DMA_Priority_Low;

    DMA_Init(ICM42605_TX_DMA_CHANNEL, &DMA_InitStructure);
    DMA_MuxChannelConfig(
        ICM42605_TX_DMAMUX_CHANNEL,
        ICM42605_TX_DMA_REQUEST
    );
    DMA_ITConfig(ICM42605_TX_DMA_CHANNEL, DMA_IT_TE, ENABLE);

    DMA_ClearITPendingBit(ICM42605_DMA_INSTANCE, ICM42605_RX_DMA_TC_FLAG);
    DMA_ClearITPendingBit(ICM42605_DMA_INSTANCE, ICM42605_RX_DMA_TE_FLAG);
    DMA_ClearITPendingBit(ICM42605_DMA_INSTANCE, ICM42605_TX_DMA_TC_FLAG);
    DMA_ClearITPendingBit(ICM42605_DMA_INSTANCE, ICM42605_TX_DMA_TE_FLAG);

    /*
     * V3F 2级嵌套: (preempt<<7)|(sub<<4)
     * ADS1299 DRDY: 抢占0(最高), ADS1299 DMA: 抢占1子0
     * ICM DMA: 抢占1子5, 低于脑电DMA
     */
    NVIC_SetPriority(ICM42605_RX_DMA_IRQn, (1 << 7) | (5 << 4));
    NVIC_EnableIRQ(ICM42605_RX_DMA_IRQn);

    NVIC_SetPriority(ICM42605_TX_DMA_IRQn, (1 << 7) | (5 << 4));
    NVIC_EnableIRQ(ICM42605_TX_DMA_IRQn);
}

/**
 * @brief  启动一次SPI4 DMA读取六轴原始数据。
 */
static ICM42605_Status ICM42605_DMA_StartReadRaw(void)
{
    uint8_t i;

    if(g_icm42605_dma_busy != 0U)
    {
        g_icm42605_dma_lost_count++;
        return ICM42605_ERROR_CONFIG;
    }

    g_icm42605_dma_busy = 1U;
    icm42605_current_sample_ms = g_icm42605_ms_tick;

    /*
     * 第1字节发送读命令，后12字节发送dummy以产生SPI时钟。
     * RX第0字节是命令阶段收到的无效字节，RX[1..12]才是有效数据。
     */
    icm42605_dma_tx_buf[0] =
        (uint8_t)(ICM42605_REG_ACC_XH | 0x80U);

    for(i = 1U; i < ICM42605_DMA_FRAME_SIZE; i++)
    {
        icm42605_dma_tx_buf[i] = 0xFFU;
    }

    SPI_I2S_DMACmd(ICM42605_SPI_INSTANCE, SPI_I2S_DMAReq_Rx, DISABLE);
    SPI_I2S_DMACmd(ICM42605_SPI_INSTANCE, SPI_I2S_DMAReq_Tx, DISABLE);

    DMA_Cmd(ICM42605_RX_DMA_CHANNEL, DISABLE);
    DMA_Cmd(ICM42605_TX_DMA_CHANNEL, DISABLE);

    DMA_ClearITPendingBit(ICM42605_DMA_INSTANCE, ICM42605_RX_DMA_TC_FLAG);
    DMA_ClearITPendingBit(ICM42605_DMA_INSTANCE, ICM42605_RX_DMA_TE_FLAG);
    DMA_ClearITPendingBit(ICM42605_DMA_INSTANCE, ICM42605_TX_DMA_TC_FLAG);
    DMA_ClearITPendingBit(ICM42605_DMA_INSTANCE, ICM42605_TX_DMA_TE_FLAG);

    ICM42605_SPI_INSTANCE_ClearRxNE();

    ICM42605_RX_DMA_CHANNEL->MADDR =
        (uint32_t)icm42605_dma_rx_buf;
    DMA_SetCurrDataCounter(
        ICM42605_RX_DMA_CHANNEL,
        ICM42605_DMA_FRAME_SIZE
    );

    ICM42605_TX_DMA_CHANNEL->MADDR =
        (uint32_t)icm42605_dma_tx_buf;
    DMA_SetCurrDataCounter(
        ICM42605_TX_DMA_CHANNEL,
        ICM42605_DMA_FRAME_SIZE
    );

    ICM42605_CS_LOW();
    Delay_Us(2);

    DMA_Cmd(ICM42605_RX_DMA_CHANNEL, ENABLE);
    DMA_Cmd(ICM42605_TX_DMA_CHANNEL, ENABLE);

    SPI_I2S_DMACmd(ICM42605_SPI_INSTANCE, SPI_I2S_DMAReq_Rx, ENABLE);
    SPI_I2S_DMACmd(ICM42605_SPI_INSTANCE, SPI_I2S_DMAReq_Tx, ENABLE);

    return ICM42605_OK;
}

/**
 * @brief  停止SPI4 DMA并释放CS。
 */
static void ICM42605_DMA_StopAndReleaseCS(void)
{
    SPI_I2S_DMACmd(ICM42605_SPI_INSTANCE, SPI_I2S_DMAReq_Rx, DISABLE);
    SPI_I2S_DMACmd(ICM42605_SPI_INSTANCE, SPI_I2S_DMAReq_Tx, DISABLE);

    DMA_Cmd(ICM42605_RX_DMA_CHANNEL, DISABLE);
    DMA_Cmd(ICM42605_TX_DMA_CHANNEL, DISABLE);

    (void)ICM42605_SPI_INSTANCE_WaitNotBusy();

    Delay_Us(2);
    ICM42605_CS_HIGH();

    g_icm42605_dma_busy = 0U;
}

/**
 * @brief  从DMA接收缓冲区解析六轴原始数据。
 */
static void ICM42605_DMA_ParseRaw(ICM42605_RawData *raw)
{
    uint8_t *buffer;

    if(raw == 0)
    {
        return;
    }

    /*
     * rx_buf[0]为发送地址命令时收到的dummy；
     * rx_buf[1]开始是0x0C寄存器数据。
     */
    buffer = &icm42605_dma_rx_buf[1];

    raw->acc_x = ICM42605_CombineInt16(buffer[0], buffer[1]);
    raw->acc_y = ICM42605_CombineInt16(buffer[2], buffer[3]);
    raw->acc_z = ICM42605_CombineInt16(buffer[4], buffer[5]);

    raw->gyro_x = ICM42605_CombineInt16(buffer[6], buffer[7]);
    raw->gyro_y = ICM42605_CombineInt16(buffer[8], buffer[9]);
    raw->gyro_z = ICM42605_CombineInt16(buffer[10], buffer[11]);
}

/**
 * @brief  SPI接收FIFO清空。
 */
static void ICM42605_SPI_INSTANCE_ClearRxNE(void)
{
    volatile uint16_t dummy;

    while(SPI_I2S_GetFlagStatus(ICM42605_SPI_INSTANCE, SPI_I2S_FLAG_RXNE) != RESET)
    {
        dummy = SPI_I2S_ReceiveData(ICM42605_SPI_INSTANCE);
        (void)dummy;
    }
}

/**
 * @brief  ICM42605 SPI4 RX DMA完成/错误中断。
 */
void DMA2_Channel1_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));

void DMA2_Channel1_IRQHandler(void)
{
    if(DMA_GetITStatus(ICM42605_DMA_INSTANCE, ICM42605_RX_DMA_TC_FLAG) != RESET)
    {
        DMA_ClearITPendingBit(ICM42605_DMA_INSTANCE, ICM42605_RX_DMA_TC_FLAG);

        ICM42605_DMA_StopAndReleaseCS();

        g_icm42605_dma_ok_count++;
        g_icm42605_dma_data_ready = 1U;
    }

    if(DMA_GetITStatus(ICM42605_DMA_INSTANCE, ICM42605_RX_DMA_TE_FLAG) != RESET)
    {
        DMA_ClearITPendingBit(ICM42605_DMA_INSTANCE, ICM42605_RX_DMA_TE_FLAG);

        ICM42605_DMA_StopAndReleaseCS();

        g_icm42605_dma_error_count++;
    }
}

/**
 * @brief  ICM42605 SPI4 TX DMA错误中断。
 */
void DMA2_Channel2_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));

void DMA2_Channel2_IRQHandler(void)
{
    if(DMA_GetITStatus(ICM42605_DMA_INSTANCE, ICM42605_TX_DMA_TE_FLAG) != RESET)
    {
        DMA_ClearITPendingBit(ICM42605_DMA_INSTANCE, ICM42605_TX_DMA_TE_FLAG);

        ICM42605_DMA_StopAndReleaseCS();

        g_icm42605_dma_error_count++;
    }
}


/**
 * @brief  SPI全双工收发一个字节。
 */
static ICM42605_Status ICM42605_SPI_INSTANCE_TransferByte(
    uint8_t tx_data,
    uint8_t *rx_data
)
{
    uint32_t timeout = ICM42605_SPI_INSTANCE_TIMEOUT;

    if(rx_data == 0)
    {
        return ICM42605_ERROR_PARAM;
    }

    while(SPI_I2S_GetFlagStatus(ICM42605_SPI_INSTANCE, SPI_I2S_FLAG_TXE) == RESET)
    {
        if(timeout == 0U)
        {
            return ICM42605_ERROR_TIMEOUT;
        }
        timeout--;
    }

    SPI_I2S_SendData(ICM42605_SPI_INSTANCE, tx_data);

    timeout = ICM42605_SPI_INSTANCE_TIMEOUT;

    while(SPI_I2S_GetFlagStatus(ICM42605_SPI_INSTANCE, SPI_I2S_FLAG_RXNE) == RESET)
    {
        if(timeout == 0U)
        {
            return ICM42605_ERROR_TIMEOUT;
        }
        timeout--;
    }

    *rx_data = (uint8_t)SPI_I2S_ReceiveData(ICM42605_SPI_INSTANCE);

    return ICM42605_OK;
}

/**
 * @brief  等待SPI总线空闲后再释放CS。
 */
static ICM42605_Status ICM42605_SPI_INSTANCE_WaitNotBusy(void)
{
    uint32_t timeout = ICM42605_SPI_INSTANCE_TIMEOUT;

    while(SPI_I2S_GetFlagStatus(ICM42605_SPI_INSTANCE, SPI_I2S_FLAG_BSY) != RESET)
    {
        if(timeout == 0U)
        {
            return ICM42605_ERROR_TIMEOUT;
        }
        timeout--;
    }

    return ICM42605_OK;
}

/**
 * @brief  高字节在前的有符号16位数据拼接。
 */
static int16_t ICM42605_CombineInt16(uint8_t high_byte, uint8_t low_byte)
{
    return (int16_t)(
        ((uint16_t)high_byte << 8) |
        (uint16_t)low_byte
    );
}


#endif /* HAS_ICM42605 */