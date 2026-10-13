#ifndef __EEG_INFER_GLXSS_H
#define __EEG_INFER_GLXSS_H

#include <stdint.h>

/* GLXSS direction-inference target sequence state machine.
 * Doctor sends CMD_INFER_CFG {mode, rounds}; MCU drives target arrows on the
 * AR glasses (via IPC_CMD_ARROW to V5F) and reports each window's inference
 * result with its ground-truth target label, keeping stimulus and acquisition
 * on the same clock. */

#define GLXSS_INFER_MODE_CYCLE   0
#define GLXSS_INFER_MODE_RANDOM  1

/*============================================================================
 * GYRO_DIR_INFER_ENABLE — 陀螺仪俯仰角方向评判开关
 *   0 = 使用V5F MI推理结果投票(默认)
 *   1 = 使用陀螺仪俯仰角投票: 0~180度=LEFT, 181~359度=RIGHT
 *       每4秒(1000帧)投票决定最终方向, 复用现有arrow显示机制
 *============================================================================*/
#ifndef GYRO_DIR_INFER_ENABLE
#define GYRO_DIR_INFER_ENABLE 0
#endif

/* 新时序: 目标显示+采集合并为4s(投票期间只显示白色目标箭头),
 * 结果1.5s + 消失0.2s = 1.7s(期间不推理), 周期5.7s */
#define GLXSS_INFER_TARGET_FRAMES  1000 /* 4s @250Hz: 目标显示+投票采集 */
#define GLXSS_INFER_COLLECT_FRAMES 1000 /* (与TARGET合并, 保持兼容) */
#define GLXSS_INFER_RESULT_FRAMES  425  /* 1.7s: 结果1.5s + 消失0.2s */

void GLXSS_Infer_Config(uint8_t mode, uint8_t rounds);
void GLXSS_Infer_Start(void);
void GLXSS_Infer_Stop(void);
uint8_t GLXSS_Infer_IsActive(void);
/* Call once per ADS1299 frame from Signal_Analysis main loop. */
void GLXSS_Infer_Poll(void);

#endif