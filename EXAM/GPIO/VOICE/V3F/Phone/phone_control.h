#ifndef __PHONE_CONTROL_H
#define __PHONE_CONTROL_H

#include <stdint.h>

/*============================================================================
 * 手机控制命令处理
 *
 * 实现 Voice_Protocol_OnFrame 回调, 解析 AA55 命令并分发:
 *   - 手机控制命令 -> ADB Host (USBHS, 暂为预留接口)
 *   - 板载 GPIO 命令 -> hal_gpio 直接执行
 *
 * 命令 payload 格式 (大端):
 *   CMD_TAP:         [x_H][x_L][y_H][y_L]                    4 bytes
 *   CMD_SWIPE:       [x1_H][x1_L][y1_H][y1_L][x2_H][x2_L][y2_H][y2_L][dur_H][dur_L]  10 bytes
 *   CMD_KEY:         [key_H][key_L]                          2 bytes
 *   CMD_TEXT:        ASCII text                              N bytes
 *   CMD_GPIO_xxx:    [port][pin]                             2 bytes
 *============================================================================*/

void Phone_Control_Init(void);
void Phone_Control_Poll(void);

#endif /* __PHONE_CONTROL_H */