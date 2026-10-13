/********************************** (C) COPYRIGHT  *******************************
* File Name          : hardware.c
* Description        : Top-level hardware mode selection for VOICE project.
*                      MODE_PHONE_CONTROL: 串口命令通道 + USBHS/ADB 预留.
*********************************************************************************/
#include "hardware.h"

#if defined(Core_V3F)
#include "Serial.h"
#include "phone_control.h"
#include "mic_hum.h"
#endif

void Hardware(void)
{
#if defined(Core_V3F)

#if (SYSTEM_MODE == MODE_GPIO_TOGGLE)
    /* GPIO 翻转模式 (原始示例) */
    {
        GPIO_InitTypeDef GPIO_InitStructure = {0};
        u8 i = 0;

        RCC_HB2PeriphClockCmd(RCC_HB2Periph_GPIOB, ENABLE);
        GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_1;
        GPIO_InitStructure.GPIO_Speed = GPIO_Speed_Very_High;
        GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_PP;
        GPIO_Init(GPIOB, &GPIO_InitStructure);

        Serial_Dbg_Printf("[V3F] MODE_GPIO_TOGGLE\r\n");
        while (1) {
            Delay_Ms(250);
            GPIO_WriteBit(GPIOB, GPIO_Pin_1, (i == 0) ? (i = Bit_SET) : (i = Bit_RESET));
        }
    }

#elif (SYSTEM_MODE == MODE_PHONE_CONTROL)
    Serial_Dbg_Printf("[V3F] MODE_PHONE_CONTROL\r\n");
    Phone_Control_Init();
    Serial_Dbg_Printf("[V3F] entering command loop\r\n");

    while (1) {
        Phone_Control_Poll();
    }

#elif (SYSTEM_MODE == MODE_DUAL_MIC_TEST)
    Serial_Dbg_Printf("[V3F] MODE_DUAL_MIC_TEST\r\n");
    Mic_Hum_TestLoop();

#endif

#else
    /* V5F: 暂无任务 */
    while (1) {
    }
#endif
}
