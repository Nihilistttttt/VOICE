/********************************** (C) COPYRIGHT *******************************
* File Name          : pressure.c
* Description        : 压差传感器读取（BOARD_V3 专用，±500 Pa / 3V3 版本）
*
*                      依赖: ads1115.c 的 I2C 读取（ADS1115 是权威采样路径，
*                      片内 ADC8/ADC9 那条 LMV321 支路暂不使用）。
*
*                      本文件是纯新增文件 —— 与目录重构（码道）互不接触。
*******************************************************************************/

#include "pressure.h"
#include "ads1115.h"
#include "debug.h"

/*============================================================================
 * 标定常数 —— ★ TODO: 上板实测后校准 ★
 *
 * ±500 Pa 版本（3V3 供电，ratiometric）：
 *   零点（0 Pa）≈ 0.5×VDD = 1.65 V → 8806 计数
 *   满跨度（1 kPa = -500~+500 Pa）理论占 VDD 的 80% → 14090 计数/kPa
 *   —— 80% 是占位假设，真实跨度以传感器手册/实测为准。
 *
 * 零点用 Pressure_ZeroCalibrate() 实测覆盖（个体公差+安装应力+温漂），
 * 斜率常数实测后替换下面的值（换算公式不变）。
 *============================================================================*/
#define PRESSURE_COUNTS_PER_KPA  14090  /* 占位：1 kPa 跨度 ≈ 0.8×VDD，待标定 */

/*============================================================================
 * 内部状态
 *============================================================================*/
static int32_t s_zero1 = 0;    /* 零点计数（校准后写入）*/
static int32_t s_zero2 = 0;

/*---------------------------------------------------------------------------
 * 中位数滤波（样本数小，插入排序最省事）
 *---------------------------------------------------------------------------*/
static int16_t median16(int16_t *buf, uint8_t n)
{
    uint8_t i, j;
    for (i = 1; i < n; i++) {
        int16_t key = buf[i];
        j = i;
        while (j > 0 && buf[j-1] > key) { buf[j] = buf[j-1]; j--; }
        buf[j] = key;
    }
    return buf[n/2];   /* n 为偶数时取上中位，足够用 */
}

/*============================================================================
 * 对外接口
 *============================================================================*/
void Pressure_Init(void)
{
    ADS1115_Init();

    /* 上电零点先置 0，等 Pressure_ZeroCalibrate() 实测覆盖 */
    s_zero1 = 0;
    s_zero2 = 0;
}

int16_t Pressure_ReadCounts(uint8_t sensor)
{
    switch (sensor) {
        case PRESSURE_SENSOR_1: return ADS1115_ReadChannel(1);  /* AIN1 */
        case PRESSURE_SENSOR_2: return ADS1115_ReadChannel(0);  /* AIN0 */
        default:                return 0;
    }
}

int32_t Pressure_ReaduV(uint8_t sensor)
{
    int32_t counts = Pressure_ReadCounts(sensor);

    /* counts × 187.5 µV，用 ×1875/10 避免 double（MCU 上省浮点库）*/
    return counts * 1875 / 10;
}

int32_t Pressure_ReadPa(uint8_t sensor)
{
    int32_t counts = Pressure_ReadCounts(sensor);
    int32_t zero   = (sensor == PRESSURE_SENSOR_1) ? s_zero1 : s_zero2;
    int32_t diff   = counts - zero;

    /* (counts - zero) / COUNTS_PER_KPA × 1000 Pa
     * 溢出核算: |diff| ≤ 17613×2, ×1000 ≈ 3.5e7 < 2^31 ✓ */
    return diff * 1000 / PRESSURE_COUNTS_PER_KPA;
}

void Pressure_ZeroCalibrate(uint8_t samples)
{
    int16_t b1[9], b2[9];
    uint8_t n = (samples > 9) ? 9 : samples;   /* 缓冲上限 9 */
    uint8_t i;

    if (n < 3) n = 3;

    for (i = 0; i < n; i++) {
        b1[i] = Pressure_ReadCounts(PRESSURE_SENSOR_1);
        b2[i] = Pressure_ReadCounts(PRESSURE_SENSOR_2);
    }

    s_zero1 = median16(b1, n);
    s_zero2 = median16(b2, n);

    printf("[Pressure] zero calibrate: s1=%d s2=%d (%u samples)\r\n",
           s_zero1, s_zero2, n);
}
