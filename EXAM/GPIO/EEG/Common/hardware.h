/********************************** (C) COPYRIGHT  *******************************
* File Name          : hardware.h
* Description        : Top-level hardware entry. Dual-core IPC public APIs are
*                      declared in dualcore_ipc.h and included here for
*                      backward compatibility with existing V3F modules.
*******************************************************************************/
#ifndef __HARDWARE_H
#define __HARDWARE_H

#ifdef __cplusplus
 extern "C" {
#endif

#include <stdint.h>
#include "ch32h417.h"
#include "debug.h"
#include "dualcore_ipc.h"

#define MODE_EEG_ANALYSIS    1
#define MODE_SPI_TEST        2
#define MODE_SD_TEST         3
#define ICM_42605_Mode       4
#define MODE_GLXSS           5
#define MODE_GLXSS_BURN      6
#define MODE_MAX98357A_TEST  7
#define MODE_WAV_TEST        8
#define MODE_I2C_DEBUG       9
#define SYSTEM_MODE          MODE_EEG_ANALYSIS

#define GYRO_DIR_INFER_ENABLE 1
#define FOCUS_TRAINING_SIM_ENABLE 1

void Hardware(void);

#ifdef __cplusplus
}
#endif

#endif
