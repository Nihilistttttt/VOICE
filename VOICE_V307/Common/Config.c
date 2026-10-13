#include "Config.h"

/*============================================================================
 * 外设时钟使能 —— SPI / DMA / AFIO
 *   GPIO_ClockEnable 在 hal_gpio.c（与 GPIO 操作放一起）
 *   新增一种外设时钟只需在此加一个 if 分支。
 *============================================================================*/

void SPI_ClockEnable(SPI_TypeDef *SPIx)
{
    if      (SPIx == SPI1) RCC_APB2PeriphClockCmd(RCC_APB2Periph_SPI1, ENABLE);
    else if (SPIx == SPI2) RCC_APB1PeriphClockCmd(RCC_APB1Periph_SPI2, ENABLE);
    else if (SPIx == SPI3) RCC_APB1PeriphClockCmd(RCC_APB1Periph_SPI3, ENABLE);
}

void I2C_ClockEnable(I2C_TypeDef *I2Cx)
{
    /* ★ CH32V307 的 I2C1/I2C2 都挂在 APB1（与 STM32F103 的 I2C1 在 APB2 不同）*/
    if      (I2Cx == I2C1) RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2C1, ENABLE);
    else if (I2Cx == I2C2) RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2C2, ENABLE);
}

void DMA_ClockEnable(DMA_TypeDef *DMAx)
{
    if      (DMAx == DMA1) RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);
    else if (DMAx == DMA2) RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA2, ENABLE);
}

void AFIO_ClockEnable(void)
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE);
}