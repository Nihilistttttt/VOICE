/********************************** (C) COPYRIGHT  *******************************
* File Name          : hardware.h
* Description        : Top-level hardware entry for VOICE project.
*                      USBHS 手机控制 (暂时串口命令通道).
*********************************************************************************/
#ifndef __HARDWARE_H
#define __HARDWARE_H

#ifdef __cplusplus
 extern "C" {
#endif

#include "ch32h417.h"
#include "debug.h"

/* 系统模式 */
#define MODE_GPIO_TOGGLE       1
#define MODE_PHONE_CONTROL     2
#define MODE_DUAL_MIC_TEST     3
#define SYSTEM_MODE            MODE_PHONE_CONTROL

void Hardware(void);

#ifdef __cplusplus
}
#endif

#endif
