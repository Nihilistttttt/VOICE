/********************************** (C) COPYRIGHT *******************************
* File Name          : pressure.c
* Description        : 压差传感器读取（BOARD_V3 专用，±500 Pa / 3V3 版本）
*
*                      依赖: ads1115.c 的 I2C 读取（ADS1115 是权威采样路径，
*                      片内 ADC8/ADC9 那条 LMV321 支路暂不使用）。
*
* ★ 传感器输出两端都是硬轨（顶 2.72V≈+480Pa / 底 0V≈-542Pa），
*   超量程 = 钉在对应轨上（不是绕回）；读取后钳位到 ±500。
*******************************************************************************/

#include "pressure.h"
#include "ads1115.h"
#include "debug.h"

/*============================================================================
 * 换算公式 —— 来自规格书表6（正负压型，3.3V 供电）
 *
 *   压力(kPa) = (VOUT - 1.45V) / K，±0.5 kPa 档 K = 2.5 V/kPa
 *   （表6: ±1 kPa 档 K=1.25、输出 0.2~2.7V；所有 ± 档共用该输出跨度，
 *     量程减半 K 翻倍 → ±0.5 档 K=2.5，输出同样是 0.2~2.7V）
 *   VOUT(V) = raw × 187.5µV  ⇒  压力(Pa) = (raw - zero) × 3 / 40
 *
 * 手册对照（实测全部落在规格内）:
 *   零点 1.45V = raw 7733（实测 7690，个体偏移 -43 计数，由校准吸收）
 *   上限钳位 3/4~1 VDD = 2.475~3.3V（实测顶轨 2.716V ✓）
 *   下限钳位 0~1/4 VDD  = 0~0.825V（实测底轨 0.003V ✓）
 *   顶轨 +509 Pa、底轨 -576 Pa —— 额定 ±500 完整落在两轨之间 ✓
 *============================================================================*/
#define PRESSURE_PA_NUM   3     /* Pa = (raw - zero) × 3 / 40（手册表6）*/
#define PRESSURE_PA_DEN   40

/*============================================================================
 * 内部状态
 *============================================================================*/
static int32_t s_zero1 = 0;    /* 零点计数（校准后写入）*/
static int32_t s_zero2 = 0;
static uint8_t s_sat1 = 0;     /* 最近一次读取是否顶轨（饱和）*/
static uint8_t s_sat2 = 0;

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
    int16_t raw;
    uint8_t sat;

    switch (sensor) {
        case PRESSURE_SENSOR_1: raw = ADS1115_ReadChannel(1); break;  /* AIN1 */
        case PRESSURE_SENSOR_2: raw = ADS1115_ReadChannel(0); break;  /* AIN0 */
        default:                return 0;
    }

    /* 饱和判定: 输出顶到电源轨（实测正向钉 ≈14484，负向钉 ≈15-16）
     * 钳位轨上的读数按手册斜率换算并钳位 ±500（见 ReadPa）*/
    sat = (raw < PRESSURE_SAT_LOW_COUNTS) || (raw > PRESSURE_SAT_HIGH_COUNTS);
    if (sensor == PRESSURE_SENSOR_1) s_sat1 = sat; else s_sat2 = sat;

    return raw;
}

int32_t Pressure_ReaduV(uint8_t sensor)
{
    int32_t counts = Pressure_ReadCounts(sensor);

    /* counts × 187.5 µV，用 ×1875/10 避免 double（MCU 上省浮点库）*/
    return counts * 1875 / 10;
}

int32_t Pressure_ReadPa(uint8_t sensor)
{
    int16_t raw  = Pressure_ReadCounts(sensor);   /* 同时更新 s_sat */
    int32_t zero = (sensor == PRESSURE_SENSOR_1) ? s_zero1 : s_zero2;
    int32_t diff = (int32_t)raw - zero;

    /* 手册表6 公式 + 额定量程钳位。
     * 传感器输出两端都是钳位轨（上限 3/4~1VDD≈2.72V=+509Pa，
     * 下限 0~1/4VDD≈0V=-576Pa）：吹超限钳 +500、吸超限钳 -500。
     * 停止吹气后的 -500 [SAT] 闪现是真实的负压回弹振铃（端口气柱
     * 的亥姆霍兹振铃），由上层状态机消抖，不在驱动层过滤。 */
    int32_t pa = diff * PRESSURE_PA_NUM / PRESSURE_PA_DEN;

    if (pa >  PRESSURE_RANGE_PA) pa =  PRESSURE_RANGE_PA;
    if (pa < -PRESSURE_RANGE_PA) pa = -PRESSURE_RANGE_PA;
    return pa;
}

uint8_t Pressure_Saturated(uint8_t sensor)
{
    return (sensor == PRESSURE_SENSOR_1) ? s_sat1 : s_sat2;
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

    printf("[Pressure] zero calibrate: s1=%ld s2=%ld (%u samples)\r\n",
           s_zero1, s_zero2, n);
}
