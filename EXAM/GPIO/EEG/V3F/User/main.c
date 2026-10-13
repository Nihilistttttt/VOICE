/********************************** (C) COPYRIGHT *******************************
 * File Name          : main_v3f.c
 * Author             : WCH
 * Version            : V1.1.0
 * Date               : 2026/07/01
 * Description        : Dual-core EEG v2.7 structure-optimized build. V3F handles acquisition
 *                      and peripherals; V5F handles feature/inference backend.
 *                      GLXSS mode: all logic lives in Common/hardware.c MODE_GLXSS.
 *******************************************************************************/

#include "debug.h"
#include "hardware.h"
#include "Serial.h"

#ifdef GLXSS_ENABLED
#include "glxss_me.h"
#include "glxss_sd_fw.h"
#include "sd_card.h"
#include "ipc_log.h"
#include <string.h>
#endif

int main(void)
{
    SystemInit();
    SystemAndCoreClockUpdate();
    Delay_Init();

    Serial_Init(SERIAL_PORT_DEBUG);
    Delay_Ms(200);

    Serial_Printf(SERIAL_PORT_DEBUG, "\r\n[V3F] boot\r\n");
    Serial_Printf(SERIAL_PORT_DEBUG, "[V3F] SystemCoreClk:%d\r\n", SystemCoreClock);
    Delay_Ms(200);

#ifdef GLXSS_ENABLED
    /* GLXSS + EEG fusion: V3F does EEG dual-core IPC (normal flow) and
     * Common/hardware.c MODE_EEG_ANALYSIS initializes glasses from SD. */
#endif

#if defined(GLXSS_ENABLED) && (SYSTEM_MODE == MODE_EEG_ANALYSIS || SYSTEM_MODE == MODE_GLXSS)
    /* GLXSS + EEG fusion: upload glasses firmware BEFORE waking V5F so that
     * V5F can act as USB Host on the application-mode glasses. */
    {
        Serial_Printf(SERIAL_PORT_DEBUG, "[GLXSS] SD init + FW upload...\r\n");
        int ret = SD_Init();
        if (ret != 0) {
            Serial_Printf(SERIAL_PORT_DEBUG, "[GLXSS] SD init FAIL, skipping glasses\r\n");
        } else {
            uint32_t fw_size = glxss_sd_fw_get_size();
            if (fw_size == 0) {
                Serial_Printf(SERIAL_PORT_DEBUG, "[GLXSS] No FW on SD, skipping glasses\r\n");
            } else {
                Serial_Printf(SERIAL_PORT_DEBUG, "[GLXSS] FW %lu bytes, uploading...\r\n",
                              (unsigned long)fw_size);
                glxss_err_t err = glxss_init(glxss_sd_fw_read, fw_size, 15000);
                if (err != GLXSS_OK) {
                    Serial_Printf(SERIAL_PORT_DEBUG, "[GLXSS] Init failed: %d, skipping\r\n", err);
                } else {
                    Serial_Printf(SERIAL_PORT_DEBUG, "[GLXSS] Glasses ready!\r\n");
                    glxss_set_brightness(200);
                    Delay_Ms(500);
                    glxss_power_switch(1);
                    Delay_Ms(200);
                    glxss_set_display_mode(0);
                    Delay_Ms(200);
                    {
                        volatile IPC_Log_Shared_t *shared = IPC_LOG_SHARED;
                        memset((void *)shared, 0, sizeof(IPC_Log_Shared_t));
                        shared->v5f_status = IPC_LOG_STATUS_IDLE;
                    }
                    Serial_Printf(SERIAL_PORT_DEBUG, "[GLXSS] IPC shared mem ready\r\n");
                }
            }
        }
    }
#endif


#if (Run_Core == Run_Core_V3FandV5F)

    Serial_Printf(SERIAL_PORT_DEBUG, "[V3F] wake V5F...\r\n");

    /* Wake V5F from its start address. */
    NVIC_WakeUp_V5F(Core_V5F_StartAddr);

    /* Wait until V5F releases HSEM_ID0. If V5F does not start, V3F will stop here. */
    HSEM_ITConfig(HSEM_ID0, ENABLE);
    NVIC->SCTLR |= 1 << 4;
    RCC_HB1PeriphClockCmd(RCC_HB1Periph_PWR, ENABLE);
    PWR_EnterSTOPMode(PWR_Regulator_ON, PWR_STOPEntry_WFE);
    HSEM_ClearFlag(HSEM_ID0);

    Serial_Printf(SERIAL_PORT_DEBUG, "[V3F] V5F HSEM released\r\n");

    {
        uint32_t v5f_magic = IPC_ReadMSG(IPC_MSG2);
        uint32_t v5f_mepc  = IPC_ReadMSG(IPC_MSG3);
        uint32_t v5f_mtval = IPC_ReadMSG(IPC_MSG1);
        if (v5f_magic == 0xDEADF00Du) {
            Serial_Printf(SERIAL_PORT_DEBUG, "[V3F] V5F PREV FAULT: MEPC=0x%08lX MTVAL=0x%08lX\r\n",
                          (unsigned long)v5f_mepc, (unsigned long)v5f_mtval);
            IPC_WriteMSG(IPC_MSG1, 0);
            IPC_WriteMSG(IPC_MSG2, 0);
            IPC_WriteMSG(IPC_MSG3, 0);
        }
    }

    DualCore_IPC_Init_V3F();
    Serial_Printf(SERIAL_PORT_DEBUG, "[V3F] IPC EEG dual-core v2.7 structured enabled\r\n");
    Serial_Printf(SERIAL_PORT_DEBUG, "[V3F] Select mode in V3F/Hardware/Signal_Analysis.c: EEG_APP_MODE\r\n");
    Delay_Ms(300);

    Hardware();

#elif (Run_Core == Run_Core_V3F)

    Serial_Printf(SERIAL_PORT_DEBUG, "[V3F] single-core mode\r\n");
    Delay_Ms(300);
    Hardware();

#elif (Run_Core == Run_Core_V5F)

    NVIC_WakeUp_V5F(Core_V5F_StartAddr);
    PWR_EnterSTOPMode(PWR_Regulator_ON, PWR_STOPEntry_WFE);

#endif

    while (1)
    {
    }
}
