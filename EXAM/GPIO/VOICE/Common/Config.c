#include "Config.h"

void USART_ClockEnable(USART_TypeDef *USARTx)
{
    if (USARTx == USART1) {
        RCC_HB2PeriphClockCmd(RCC_HB2Periph_USART1, ENABLE);
    } else if (USARTx == USART2) {
        RCC_HB1PeriphClockCmd(RCC_HB1Periph_USART2, ENABLE);
    } else if (USARTx == USART3) {
        RCC_HB1PeriphClockCmd(RCC_HB1Periph_USART3, ENABLE);
    } else if (USARTx == USART4) {
        RCC_HB1PeriphClockCmd(RCC_HB1Periph_USART4, ENABLE);
    }
}

void DMA_ClockEnable(DMA_TypeDef *DMAx)
{
    if (DMAx == DMA1) {
        RCC_HBPeriphClockCmd(RCC_HBPeriph_DMA1, ENABLE);
    } else if (DMAx == DMA2) {
        RCC_HBPeriphClockCmd(RCC_HBPeriph_DMA2, ENABLE);
    }
}

void AFIO_ClockEnable(void)
{
    RCC_HB2PeriphClockCmd(RCC_HB2Periph_AFIO, ENABLE);
}