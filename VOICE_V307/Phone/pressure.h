#ifndef __PRESSURE_H__
#define __PRESSURE_H__

#include <stdint.h>

/*============================================================================
 * 压差传感器读取（BOARD_V3 专用）
 *
 * 硬件: ±500 Pa 压差传感器（3V3 供电版本，ratiometric 模拟输出）
 *       VOUT → LMV321B 跟随器(增益=1) → ADS1115 AIN0 / AIN1
 *       两个吸/吹口相互独立：AIN1 = 传感器1，AIN0 = 传感器2
 *       ★ 净表(电路.txt)快照里还是旧型号 GZP6897A001KPW50K(±1kPa/3V3A)，
 *         实物已换 3V3/-500~500Pa 版 —— 以实物为准。
 *
 *   ADS1115: PGA ±6.144V → 1 LSB = 187.5 µV
 *   3V3 满摆幅 ≈ ±6.144V 量程的 ±53.7%（17613 计数 @ VDD），零点 ≈ 8806 计数
 *
 * 量程 -500~+500 Pa（总跨度 1 kPa）。本模块负责"读得准"：
 *   计数 → 微伏（数学精确）、计数 → Pa（线性标定，常数待上板实测）。
 * 吸/吹判定的状态机属于上层（phone_control），不在这里。
 *============================================================================*/

#define PRESSURE_SENSOR_1   1    /* AIN1，传感器1 */
#define PRESSURE_SENSOR_2   2    /* AIN0，传感器2 */

#define PRESSURE_RANGE_PA   500  /* 单侧量程 ±500 Pa */

/* 初始化（内部调用 ADS1115_Init，重复调用无害） */
void Pressure_Init(void);

/* 原始读取：ADS1115 有符号计数值。
 * 传感器失效/总线无应答时返回 0 —— 与"真实 0 计数"不可区分，
 * 需要区分时调用方自行连续采样判稳。 */
int16_t Pressure_ReadCounts(uint8_t sensor);

/* 换算成微伏（数学精确，仅依赖 PGA 量程）*/
int32_t Pressure_ReaduV(uint8_t sensor);

/* 换算成压差 Pa（-500~+500，依赖标定常数，见 pressure.c）*/
int32_t Pressure_ReadPa(uint8_t sensor);

/* 零点校准：连续采 samples 次取中位数，写入零点基准。
 * 上电静置后调用一次；运行中可在确认无气流时再次调用。 */
void Pressure_ZeroCalibrate(uint8_t samples);

#endif /* __PRESSURE_H__ */
