/********************************** (C) COPYRIGHT *******************************
 * File Name          : main.c
 * Author            : WCH
 * Version           : V2.2.0
 * Description        : V3F main - VOICE 手机控制工程
 *                      USART1 交互命令模式, 输入数字测试 ADB/USBHS.
 *******************************************************************************/

#include "debug.h"
#include "hardware.h"
#include "Serial.h"

int main(void)
{
    SystemInit();
    SystemAndCoreClockUpdate();
    Delay_Init();

    /* 只用 USART1: PA9(TX)/PA10(RX) 2Mbaud, 既输出又输入 */
    Serial_Init(SERIAL_PORT_DEBUG);
    Delay_Ms(200);

    Serial_Printf(SERIAL_PORT_DEBUG, "\r\n");
    Serial_Printf(SERIAL_PORT_DEBUG, "========================================\r\n");
    Serial_Printf(SERIAL_PORT_DEBUG, "  VOICE Phone Control V2.2\r\n");
    Serial_Printf(SERIAL_PORT_DEBUG, "  SystemCoreClk: %d Hz\r\n", SystemCoreClock);
    Serial_Printf(SERIAL_PORT_DEBUG, "  USART1: PA9(TX)/PA10(RX) 2Mbaud\r\n");
    Serial_Printf(SERIAL_PORT_DEBUG, "  Input number to test ADB/USBHS\r\n");
    Serial_Printf(SERIAL_PORT_DEBUG, "========================================\r\n");
    Delay_Ms(100);

#if (Run_Core == Run_Core_V3FandV5F)
    Serial_Printf(SERIAL_PORT_DEBUG, "[V3F] Dual-core mode, waking V5F...\r\n");

    NVIC_WakeUp_V5F(Core_V5F_StartAddr);
    HSEM_ITConfig(HSEM_ID0, ENABLE);
    NVIC->SCTLR |= 1 << 4;
    RCC_HB1PeriphClockCmd(RCC_HB1Periph_PWR, ENABLE);
    PWR_EnterSTOPMode(PWR_Regulator_ON, PWR_STOPEntry_WFE);
    HSEM_ClearFlag(HSEM_ID0);

    Serial_Printf(SERIAL_PORT_DEBUG, "[V3F] V5F HSEM released\r\n");
    Delay_Ms(300);

    Hardware();

#elif (Run_Core == Run_Core_V3F)
    Serial_Printf(SERIAL_PORT_DEBUG, "[V3F] Single-core mode\r\n");
    Delay_Ms(300);
    Hardware();

#elif (Run_Core == Run_Core_V5F)
    NVIC_WakeUp_V5F(Core_V5F_StartAddr);
    PWR_EnterSTOPMode(PWR_Regulator_ON, PWR_STOPEntry_WFE);
#endif

    while (1) {
    }
}
