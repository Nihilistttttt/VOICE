#include "Timer_1ms.h"
#include "ICM42605.h"

#ifdef HAS_ICM42605

/*
 * TIM2 1ms定时中断 —— V3F 96MHz版
 *
 * 现象推断：
 * 之前按144MHz配置后，90度只能显示60多度。
 *
 * 这说明定时器实际周期仍然偏长：
 * 显示角度 / 真实角度 ≈ 60 / 90 = 2 / 3
 * 实际采样周期 ≈ 10ms / (2/3) = 15ms
 *
 * 如果 Prescaler = 144 - 1, Period = 1000 - 1，
 * 实际周期为15ms / 10 = 1.5ms，
 * 则说明 TIM2 实际时钟约为96MHz。
 *
 * 因此本版按96MHz配置真正1ms：
 * 96MHz / 96 = 1MHz
 * 1MHz / 1000 = 1kHz = 1ms
 */

void Timer_1ms_Init(void)
{
    TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStructure = {0};

    RCC_HB1PeriphClockCmd(RCC_HB1Periph_TIM2, ENABLE);

    TIM_DeInit(TIM2);
    TIM_InternalClockConfig(TIM2);

    /*
     * V3F当前工程TIM2时钟按96MHz处理：
     * 96MHz / 96 = 1MHz
     * 1MHz计数1000次 = 1ms
     */
    TIM_TimeBaseInitStructure.TIM_Prescaler = 96 - 1;
    TIM_TimeBaseInitStructure.TIM_Period = 1000 - 1;
    TIM_TimeBaseInitStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseInitStructure.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInitStructure.TIM_RepetitionCounter = 0;

    TIM_TimeBaseInit(TIM2, &TIM_TimeBaseInitStructure);

    TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
    TIM_ITConfig(TIM2, TIM_IT_Update, ENABLE);

    /*
     * V3F 2级嵌套: (preempt<<7)|(sub<<4)
     * 抢占1(低), 子优先级4: 低于ADS1299 DMA(子0)和IPC(子1)
     */
    NVIC_SetPriority(TIM2_IRQn, (1 << 7) | (4 << 4));
    NVIC_EnableIRQ(TIM2_IRQn);

    TIM_Cmd(TIM2, ENABLE);
}

void TIM2_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));

void TIM2_IRQHandler(void)
{
    if(TIM_GetITStatus(TIM2, TIM_IT_Update) != RESET)
    {
        TIM_ClearITPendingBit(TIM2, TIM_IT_Update);

        /*
         * 中断里只置标志：
         * 每10ms请求一次ICM采样；
         * 每200ms请求一次OLED刷新。
         */
        ICM42605_1msTickISR();
    }
}


#endif /* HAS_ICM42605 */