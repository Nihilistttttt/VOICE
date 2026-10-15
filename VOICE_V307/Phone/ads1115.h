#ifndef __ADS1115_H__
#define __ADS1115_H__

#include <stdint.h>

#define ADS1115_REG_CONVERSION  0x00
#define ADS1115_REG_CONFIG      0x01
#define ADS1115_REG_LO_THRESH   0x02
#define ADS1115_REG_HI_THRESH   0x03

#define ADS1115_MUX_AIN0_GND   0x4000
#define ADS1115_MUX_AIN1_GND   0x5000
#define ADS1115_MUX_AIN2_GND   0x6000
#define ADS1115_MUX_AIN3_GND   0x7000

#define ADS1115_PGA_6_144V     0x0000
#define ADS1115_PGA_4_096V     0x0200
#define ADS1115_PGA_2_048V     0x0400
#define ADS1115_PGA_1_024V     0x0600

#define ADS1115_DR_128SPS      0x0080
#define ADS1115_DR_250SPS      0x0090
#define ADS1115_DR_475SPS      0x00A0
#define ADS1115_DR_860SPS      0x00B0

#define ADS1115_MODE_SINGLE    0x0100
#define ADS1115_MODE_CONTINUOUS 0x0000
#define ADS1115_OS_START       0x8000

void ADS1115_Init(void);
uint8_t ADS1115_WriteReg(uint8_t reg, uint16_t value);
uint16_t ADS1115_ReadReg(uint8_t reg);
int16_t ADS1115_ReadChannel(uint8_t channel);

/* ---- 总线故障带外通道（审计 F2）----
 * ReadChannel 失败时返回 0, 与"真实 0 计数"不可区分; 总线死掉时设备
 * 表现为"无压力"但外观完好。用这两个查询区分:
 *   BusDead()=1  → 连续失败达阈值, 读数不可信（应做可感知降级指示）
 *   FailStreak() → 当前连续失败次数（成功清零）*/
uint8_t  ADS1115_BusDead(void);
uint16_t ADS1115_FailStreak(void);

#endif