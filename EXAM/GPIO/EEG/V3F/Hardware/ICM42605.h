#ifndef __ICM42605_H
#define __ICM42605_H

#include "ch32h417.h"
#include <stdint.h>
#include "Config.h"


#ifdef __cplusplus
extern "C" {
#endif

/*
 * ICM-42605（HXY规格书版本）驱动
 *
 * MCU   : CH32H417QEU6
 * SPI   : SPI4，四线硬件SPI
 * SCK   : PE2  AF5
 * MISO  : PE13 AF5
 * MOSI  : PE14 AF5
 * CS    : PE3  普通GPIO软件片选
 *
 * 注意：
 * 1. 本驱动严格按照用户提供的HXY规格书编写，WHO_AM_I应为0x6A。
 * 2. 如果实际读到的WHO_AM_I不是0x6A，请不要继续使用本寄存器表。
 * 3. 驱动不使用INT1/INT2，不会占用额外外部中断。
 */

/* -------------------- 寄存器地址 -------------------- */
#define ICM42605_REG_WHO_AM_I       0x01U
#define ICM42605_REG_COM_CFG        0x05U
#define ICM42605_REG_DATA_STAT      0x0BU

#define ICM42605_REG_ACC_XH         0x0CU
#define ICM42605_REG_ACC_XL         0x0DU
#define ICM42605_REG_ACC_YH         0x0EU
#define ICM42605_REG_ACC_YL         0x0FU
#define ICM42605_REG_ACC_ZH         0x10U
#define ICM42605_REG_ACC_ZL         0x11U

#define ICM42605_REG_GYR_XH         0x12U
#define ICM42605_REG_GYR_XL         0x13U
#define ICM42605_REG_GYR_YH         0x14U
#define ICM42605_REG_GYR_YL         0x15U
#define ICM42605_REG_GYR_ZH         0x16U
#define ICM42605_REG_GYR_ZL         0x17U

#define ICM42605_REG_TEMP_H         0x22U
#define ICM42605_REG_TEMP_L         0x23U

#define ICM42605_REG_ACC_CONF       0x40U
#define ICM42605_REG_ACC_RANGE      0x41U
#define ICM42605_REG_GYR_CONF       0x42U
#define ICM42605_REG_GYR_RANGE      0x43U
#define ICM42605_REG_SOFT_RST       0x4AU
#define ICM42605_REG_PWR_CTRL       0x7DU

#define ICM42605_WHO_AM_I_VALUE     0x6AU

/* DATA_STAT */
#define ICM42605_DATA_STAT_ACC_RDY  0x01U
#define ICM42605_DATA_STAT_GYR_RDY  0x02U
#define ICM42605_DATA_STAT_TMP_RDY  0x04U
#define ICM42605_DATA_STAT_ACC_ERR  0x10U
#define ICM42605_DATA_STAT_GYR_ERR  0x20U

/* 默认配置：100Hz、加速度±4g、陀螺仪±500dps、正常模式 */
#define ICM42605_ACC_CONF_DEFAULT   0xA8U
#define ICM42605_ACC_RANGE_DEFAULT  0x01U
#define ICM42605_GYR_CONF_DEFAULT   0xA8U
#define ICM42605_GYR_RANGE_DEFAULT  0x02U
#define ICM42605_PWR_CTRL_DEFAULT   0x0EU
#define ICM42605_COM_CFG_DEFAULT    0x50U

typedef enum
{
    ICM42605_OK             =  0,
    ICM42605_ERROR_PARAM    = -1,
    ICM42605_ERROR_TIMEOUT  = -2,
    ICM42605_ERROR_ID       = -3,
    ICM42605_ERROR_CONFIG   = -4,
    ICM42605_ERROR_VERIFY   = -5
} ICM42605_Status;

typedef struct
{
    int16_t acc_x;
    int16_t acc_y;
    int16_t acc_z;

    int16_t gyro_x;
    int16_t gyro_y;
    int16_t gyro_z;
} ICM42605_RawData;

typedef struct
{
    float acc_x_g;
    float acc_y_g;
    float acc_z_g;

    float gyro_x_dps;
    float gyro_y_dps;
    float gyro_z_dps;
} ICM42605_Data;

/* 初始化SPI4、GPIO和传感器 */
void ICM42605_SPI4_Init(void);
ICM42605_Status ICM42605_Init(void);

/* 基础寄存器读写 */
ICM42605_Status ICM42605_WriteRegister(uint8_t reg, uint8_t value);
ICM42605_Status ICM42605_ReadRegisters(uint8_t reg, uint8_t *data, uint16_t length);
ICM42605_Status ICM42605_ReadRegister(uint8_t reg, uint8_t *value);

/* 设备状态 */
ICM42605_Status ICM42605_ReadWhoAmI(uint8_t *who_am_i);
uint8_t ICM42605_DataReady(void);

/* 读取六轴数据 */
ICM42605_Status ICM42605_ReadRaw(ICM42605_RawData *raw);
ICM42605_Status ICM42605_ReadData(ICM42605_Data *data);

/* 温度单独读取，避免每次六轴采样都增加SPI传输量 */
ICM42605_Status ICM42605_ReadTemperature(float *temperature_c);


/*
 * ===================== 脑电联调版接口 =====================
 *
 * 推荐调用顺序：
 *
 * 1. 系统初始化阶段，脑电ADS1299正式开始采集前：
 *      ICM42605_BCI_Init();
 *
 * 2. 1ms定时器或SysTick中断里：
 *      ICM42605_1msTickISR();
 *
 * 3. 主循环里，并且放在脑电 ring_buffer_get_frame() 处理之后：
 *      ICM42605_Task();
 *
 * 兼容旧主文件：
 *      ICM_42605_Read();
 * 现在等价于 ICM42605_Task()，不会进入while(1)，不会Delay阻塞。
 */
ICM42605_Status ICM42605_BCI_Init(void);
void ICM42605_1msTickISR(void);
void ICM42605_Task(void);
void ICM_42605_Read(void);

void ICM42605_ResetAngleZero(void);
/* 返回0.1度单位的相对角，范围0~3599（0.0°~359.9°）。 */
void ICM42605_GetAngleDeg10(
    int32_t *x_deg10,
    int32_t *y_deg10,
    int32_t *z_deg10
);

/* ICM42605联调状态计数，可用于串口或OLED观察 */
extern volatile uint8_t g_icm42605_initialized;
extern volatile uint8_t g_icm42605_sample_request;
extern volatile uint8_t g_icm42605_oled_request;
extern volatile uint8_t g_icm42605_dma_busy;
extern volatile uint8_t g_icm42605_dma_data_ready;

extern volatile uint32_t g_icm42605_dma_ok_count;
extern volatile uint32_t g_icm42605_dma_lost_count;
extern volatile uint32_t g_icm42605_dma_error_count;
extern volatile uint32_t g_icm42605_ms_tick;

extern volatile uint8_t g_icm42605_pm_event_pending;
extern volatile uint8_t g_icm42605_pm_event_type;

#ifdef __cplusplus
}
#endif

#endif
