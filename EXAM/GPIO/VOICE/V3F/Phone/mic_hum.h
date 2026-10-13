#ifndef __MIC_HUM_H
#define __MIC_HUM_H

#include <stdint.h>

/*============================================================================
 * 双 INMP441 麦克风吹气检测
 *   MIC_A I2S2: WS=PB12(AF5) CK=PB13(AF5) SD=PC1(AF5)
 *               DMA2_Channel1 + MuxChannel9 + 请求号66(SPI2_RX)
 *   MIC_B I2S3: WS=PA4(AF6) CK=PC10(AF6) SD=PC12(AF6)
 *               DMA2_Channel2 + MuxChannel10 + 请求号68(SPI3_RX)
 *
 * 命令映射 (每tick约16ms):
 *   MIC_A: 短=ENTER  中/长=NEXT(或VOL_UP音量模式)  短短=PLAY  短短短=VOL_MODE
 *   MIC_B: 短=BACK   中/长=PREV(或VOL_DN音量模式)  短短=MODE  短短短=VOL_MODE
 *   跨麦: 左短+右短=LEFT  右短+左短=RIGHT
 *   (中吹单次, 长吹连续每256ms, 短短短切换音量模式)
 *============================================================================*/
#define HUM_CMD_NONE     0
#define HUM_CMD_ENTER    1
#define HUM_CMD_PREV     2
#define HUM_CMD_NEXT     3
#define HUM_CMD_PLAY     4
#define HUM_CMD_BACK     5
#define HUM_CMD_VOL_DN   6
#define HUM_CMD_VOL_UP   7
#define HUM_CMD_RETURN   8
#define HUM_CMD_MODE     9
#define HUM_CMD_LEFT     10
#define HUM_CMD_RIGHT    11
#define HUM_CMD_VOL_MODE 12

void Mic_Hum_Init(void);
int  Mic_Hum_Poll(uint16_t *p_mean_a, uint16_t *p_mean_b);  /* 非阻塞, 返回命令, 输出能量值 */
void Mic_Hum_TestLoop(void);  /* 双麦克风测试, 打印能量值(阻塞) */

/* 运行时可调参数 */
extern uint16_t g_blow_threshold;
extern uint16_t g_short_max_tick;
extern uint16_t g_mid_max_tick;
extern uint16_t g_combo_wait_tick;
extern uint16_t g_long_repeat_tick;
extern uint16_t g_min_gap_tick;

/* 音量模式 (短短短切换, 中/长吹变为VOL_UP/DN) */
extern int g_volume_mode;

/* 采集结果 (命令返回时填充) */
extern uint16_t g_col_peak1, g_col_avg1, g_col_ticks1, g_col_judge1;
extern uint16_t g_col_peak2, g_col_avg2, g_col_ticks2, g_col_judge2;
extern uint16_t g_col_gap;

#endif /* __MIC_HUM_H */
