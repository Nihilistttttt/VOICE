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
 * 量程 -500~+500 Pa（实测输出跨度 14469 计数 ≈ 1027 Pa，零点在 53% 处）。
 * ★ 传感器输出两端都是硬轨（顶 2.72V≈+480Pa / 底 0V≈-542Pa）：
 *   超量程 = 钉在对应轨上（不是绕回）。ReadPa 线性换算后钳位到 ±500，
 *   顶轨状态用 Pressure_Saturated() 查询。
 *   停止吹气后的短暂 -500 [SAT] 闪现是真实的负压回弹振铃（物理现象），
 *   由上层状态机消抖。吸/吹判定的状态机属于上层（phone_control）。
 *============================================================================*/

#define PRESSURE_SENSOR_1   1    /* AIN1，传感器1 */
#define PRESSURE_SENSOR_2   2    /* AIN0，传感器2 */

#define PRESSURE_RANGE_PA   500  /* 单侧量程 ±500 Pa */

/* 饱和判定阈值（实测: 正向钉在 raw≈14477，负向钉在 raw≈15-16 —— 输出顶到电源轨）
 * 落在这两个阈值之外 = 传感器输出已顶轨，Pa 值是线性外推，不可信 */
#define PRESSURE_SAT_LOW_COUNTS    500
#define PRESSURE_SAT_HIGH_COUNTS   14400  /* 实测顶轨 ≈14484(+481Pa)，阈值略低以便标志 */

/* 初始化（内部调用 ADS1115_Init，重复调用无害） */
void Pressure_Init(void);

/* 原始读取：ADS1115 有符号计数值。
 * 传感器失效/总线无应答时返回 0 —— 与"真实 0 计数"不可区分，
 * 需要区分时调用方自行连续采样判稳。 */
int16_t Pressure_ReadCounts(uint8_t sensor);

/* 换算成微伏（数学精确，仅依赖 PGA 量程）*/
int32_t Pressure_ReaduV(uint8_t sensor);

/* 换算成压差 Pa（钳位到 ±PRESSURE_RANGE_PA）。
 * 若顶轨（超量程），返回值是钳位后的 ±500，真实压力在量程外 ——
 * 用 Pressure_Saturated() 查询最近一次读取是否顶轨。 */
int32_t Pressure_ReadPa(uint8_t sensor);

/* 最近一次读取是否顶到电源轨（饱和）：
 * 1 = 输出已顶轨，Pa 值是外推值不可信；0 = 线性区内，读数可信 */
uint8_t Pressure_Saturated(uint8_t sensor);

/* 零点校准：连续采 samples 次取中位数，写入零点基准。
 * 上电静置后调用一次；运行中可在确认无气流时再次调用。 */
void Pressure_ZeroCalibrate(uint8_t samples);

#endif /* __PRESSURE_H__ */
