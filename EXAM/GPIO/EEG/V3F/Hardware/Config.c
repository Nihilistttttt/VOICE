#include "Config.h"

void SPI_ClockEnable (SPI_TypeDef *SPIx) {
    if (SPIx == SPI1) {
        RCC_HB2PeriphClockCmd (RCC_HB2Periph_SPI1, ENABLE);
    } else if (SPIx == SPI2) {
        RCC_HB1PeriphClockCmd (RCC_HB1Periph_SPI2, ENABLE);
    } else if (SPIx == SPI3) {
        RCC_HB1PeriphClockCmd (RCC_HB1Periph_SPI3, ENABLE);
    } else if (SPIx == SPI4) {
        RCC_HB1PeriphClockCmd (RCC_HB1Periph_SPI4, ENABLE);
    }
}

void I2C_ClockEnable (I2C_TypeDef *I2Cx) {
    if (I2Cx == I2C1) {
        RCC_HB2PeriphClockCmd (RCC_HB1Periph_I2C1, ENABLE);
    } else if (I2Cx == I2C2) {
        RCC_HB1PeriphClockCmd (RCC_HB1Periph_I2C2, ENABLE);
    } else if (I2Cx == I2C3) {
        RCC_HB1PeriphClockCmd (RCC_HB1Periph_I2C3, ENABLE);
    } else if (I2Cx == I2C4) {
        RCC_HB1PeriphClockCmd (RCC_HB2Periph_I2C4, ENABLE);
    }
}

void USART_ClockEnable (USART_TypeDef *USARTx) {
    if (USARTx == USART1) {
        RCC_HB2PeriphClockCmd (RCC_HB2Periph_USART1, ENABLE);
    } else if (USARTx == USART2) {
        RCC_HB1PeriphClockCmd (RCC_HB1Periph_USART2, ENABLE);
    } else if (USARTx == USART3) {
        RCC_HB1PeriphClockCmd (RCC_HB1Periph_USART3, ENABLE);
    } else if (USARTx == USART4) {
        RCC_HB1PeriphClockCmd (RCC_HB1Periph_USART4, ENABLE);
    }
}

void DMA_ClockEnable (DMA_TypeDef *DMAx) {
    if (DMAx == DMA1) {
        RCC_HBPeriphClockCmd (RCC_HBPeriph_DMA1, ENABLE);
    } else if (DMAx == DMA2) {
        RCC_HBPeriphClockCmd (RCC_HBPeriph_DMA2, ENABLE);
    }
}

void AFIO_ClockEnable (void) {
    RCC_HB2PeriphClockCmd (RCC_HB2Periph_AFIO, ENABLE);
}