/********************************** (C) COPYRIGHT *******************************
* File Name          : main.c
* Description        : CH32V307 入口 —— 只做两件事：
*                        1. 与"用哪个模式"无关的启动准备（中断分组 / 时钟 / 延时 / 串口 / 手机供电）
*                        2. 按 Common/Config.h 的 TEST_MODE 选一个模式进去
*
*                      各模式的完整实现在 User/test_modes.c，
*                      读这个文件不会被迫先读完任何一个功能的内部逻辑。
*******************************************************************************/

#include "debug.h"
#include "Config.h"
#include "test_modes.h"

int main(void)
{
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
    SystemCoreClockUpdate();
    Delay_Init();
    USART_Printf_Init(2000000);

    printf("SystemClk:%d\r\n", SystemCoreClock);
    printf("ChipID:%08x\r\n", DBGMCU_GetCHIPID());
    Delay_Ms(500);

    /* PB5 = PWR_CTRL：拉高 = 给手机供电。
     * ★ 这不是板载 LED —— 链路是 PB5 → R12(4.7k) → Q3(NPN) → Q2(P-MOS) → VPHONE。
     *   真正的板载 LED 在 PB8（经 R52）。依据：hardware/电路.txt 网表。 */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1 | RCC_APB2Periph_GPIOB, ENABLE);
    {
        GPIO_InitTypeDef GPIO_InitStructure;

        GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_5;
        GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
        GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_PP;
        GPIO_Init(GPIOB, &GPIO_InitStructure);
        GPIO_SetBits(GPIOB, GPIO_Pin_5);
    }

#if TEST_MODE == 0
    Mode_PhoneControl();     /* 正式功能：吹气采集 + USB AOA 手机控制 */
#elif TEST_MODE == 1
    Mode_SdioBench();        /* SDIO SD 卡读写速率基准 */
#elif TEST_MODE == 2
    Mode_Udisk();            /* USB MSC U 盘 */
#elif TEST_MODE == 3
    Mode_Fatfs();            /* SD 卡 FAT 读文件 */
#elif TEST_MODE == 4
    Mode_UsbhsEnum();        /* USBHS Device 枚举调试 */
#elif TEST_MODE == 5
    Mode_HidMouse();         /* USBFS HID 摇杆鼠标 */
#else
    printf("Unknown TEST_MODE %d\r\n", TEST_MODE);
    while (1);
#endif

    /* 所有模式函数都不返回，这里只是兜底 */
    while (1);
}
