#include "W25Q64.h"

#define HARDWARE 0
#define SOFTWARE 1
#define SPI_TYPE HARDWARE

#define SPIx SPI2
#if (SPI_TYPE == SOFTWARE)  // 软件模式引脚
#define SPI2_PORT GPIOB
#define SPI2_MOSI_PIN GPIO_Pin_15
#define SPI2_MISO_PIN GPIO_Pin_14
#define SPI2_SCK_PIN GPIO_Pin_13
#define SPI2_CS_PIN GPIO_Pin_12
#else  // 硬件模式引脚
#define SPI2_PORT GPIOB
#define SPI2_MOSI_PIN GPIO_Pin_15
#define SPI2_MISO_PIN GPIO_Pin_14
#define SPI2_SCK_PIN GPIO_Pin_13
#define SPI2_CS_PIN GPIO_Pin_12
#define SPI2_MISO_PINSOURCE GPIO_PinSource14
#define SPI2_MOSI_PINSOURCE GPIO_PinSource15
#define SPI2_SCK_PINSOURCE GPIO_PinSource13
#endif

void SPI_W_CS (uint8_t BitValue) {
    GPIO_WriteBit (SPI2_PORT, SPI2_CS_PIN, (BitAction)BitValue);
}

#if (SPI_TYPE == SOFTWARE)  // 软件模式引脚
void SPI_W_SCK (uint8_t BitValue) {
    GPIO_WriteBit (SPI2_PORT, SPI2_SCK_PIN, (BitAction)BitValue);
}

void SPI_W_MOSI (uint8_t BitValue) {
    GPIO_WriteBit (SPI2_PORT, SPI2_MOSI_PIN, (BitAction)BitValue);
}

uint8_t SPI_R_MISO (void) {
    return GPIO_ReadInputDataBit (SPI2_PORT, SPI2_MISO_PIN);
}
#endif


void W25Q64_Init (void) {
#if (SPI_TYPE == SOFTWARE)
    RCC_HB2PeriphClockCmd (RCC_HB2Periph_GPIOB, ENABLE);

    GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Pin = SPI2_MOSI_PIN | SPI2_SCK_PIN | SPI2_CS_PIN;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_Very_High;
    GPIO_Init (SPI2_PORT, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_InitStructure.GPIO_Pin = SPI2_MISO_PIN;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_Very_High;
    GPIO_Init (SPI2_PORT, &GPIO_InitStructure);

    SPI_W_CS (1);
    SPI_W_SCK (0);
#else
    RCC_HB2PeriphClockCmd (RCC_HB2Periph_AFIO | RCC_HB2Periph_GPIOB, ENABLE);
    RCC_HB1PeriphClockCmd (RCC_HB1Periph_SPI2, ENABLE);

    GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Pin = SPI2_CS_PIN;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_Very_High;
    GPIO_Init (SPI2_PORT, &GPIO_InitStructure);

    GPIO_PinAFConfig (GPIOB, SPI2_MOSI_PINSOURCE, GPIO_AF5);
    GPIO_PinAFConfig (GPIOB, SPI2_SCK_PINSOURCE, GPIO_AF5);
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Pin = SPI2_MOSI_PIN | SPI2_SCK_PIN;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_Very_High;
    GPIO_Init (SPI2_PORT, &GPIO_InitStructure);

    GPIO_PinAFConfig (GPIOB, SPI2_MISO_PINSOURCE, GPIO_AF5);
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_InitStructure.GPIO_Pin = SPI2_MISO_PIN;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_Very_High;
    GPIO_Init (SPI2_PORT, &GPIO_InitStructure);

    SPI_InitTypeDef SPI_InitStructure;
    SPI_InitStructure.SPI_Mode = SPI_Mode_Master;
    SPI_InitStructure.SPI_Direction = SPI_Direction_2Lines_FullDuplex;
    SPI_InitStructure.SPI_DataSize = SPI_DataSize_8b;
    SPI_InitStructure.SPI_FirstBit = SPI_FirstBit_MSB;
    SPI_InitStructure.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_Mode2;
    SPI_InitStructure.SPI_CPOL = SPI_CPOL_Low;
    SPI_InitStructure.SPI_CPHA = SPI_CPHA_1Edge;
    SPI_InitStructure.SPI_NSS = SPI_NSS_Soft;
    SPI_InitStructure.SPI_CRCPolynomial = 7;
    SPI_Init (SPIx, &SPI_InitStructure);

    SPI_Cmd (SPIx, ENABLE);

    SPI_W_CS (1);
#endif
}

void SPI_Start (void) {
    SPI_W_CS (0);
#if (SPI_TYPE == HARDWARE)
    Delay_Us (1);
#endif
}

void SPI_Stop (void) {
#if (SPI_TYPE == HARDWARE)
    Delay_Us (1);
#endif
    SPI_W_CS (1);
}

uint8_t SPI_SwapByte (uint8_t ByteSend) {
#if (SPI_TYPE == SOFTWARE)
    for (uint8_t i = 0; i < 8; ++i) {
        SPI_W_MOSI (ByteSend & 0x80);
        ByteSend <<= 1;
        SPI_W_SCK (1);
        if (SPI_R_MISO() == 1)
            ByteSend |= 0x01;
        SPI_W_SCK (0);
    }
    return ByteSend;
#else
    while (SPI_I2S_GetFlagStatus (SPIx, SPI_I2S_FLAG_TXE) != SET);
    SPI_I2S_SendData (SPIx, ByteSend);

#if (SPI_TYPE == SPI_HARDWARE)
    Delay_Us (1);
#endif

    while (SPI_I2S_GetFlagStatus (SPIx, SPI_I2S_FLAG_RXNE) != SET);
    return SPI_I2S_ReceiveData (SPIx);
#endif
}

void W25Q64_WriteEnable (void) {
    SPI_Start();
    SPI_SwapByte (W25Q64_WRITE_ENABLE);
    SPI_Stop();
}

void W25Q64_WaitBusy (void) {
    uint32_t Timeout = 100000;
    SPI_Start();
    SPI_SwapByte (W25Q64_READ_STATUS_REGISTER_1);
    while ((SPI_SwapByte (W25Q64_DUMMY_BYTE) & 0x01) == 0x01) {
        Timeout--;
        if (Timeout == 0) {
            // 错误处理
            break;
        }
    }
    SPI_Stop();
}

void W25Q64_ReadID (uint8_t *MID, uint16_t *DID) {
    W25Q64_WaitBusy();
    SPI_Start();
    SPI_SwapByte (W25Q64_JEDEC_ID);
    *MID = SPI_SwapByte (W25Q64_DUMMY_BYTE);
    *DID = SPI_SwapByte (W25Q64_DUMMY_BYTE);
    *DID <<= 8;
    *DID |= SPI_SwapByte (W25Q64_DUMMY_BYTE);
    SPI_Stop();
}

void W25Q64_PageProgram (uint32_t Address, uint8_t *DataArray, uint16_t Count) {
    W25Q64_WaitBusy();
    W25Q64_WriteEnable();
    SPI_Start();
    SPI_SwapByte (W25Q64_PAGE_PROGRAM);
    SPI_SwapByte (Address >> 16);
    SPI_SwapByte (Address >> 8);
    SPI_SwapByte (Address);
    for (uint16_t i = 0; i < Count; ++i) {
        SPI_SwapByte (DataArray[i]);
    }
    SPI_Stop();
}

void W25Q64_SectorErase (uint32_t Address) {
    W25Q64_WaitBusy();
    W25Q64_WriteEnable();
    SPI_Start();
    SPI_SwapByte (W25Q64_SECTOR_ERASE_4KB);
    SPI_SwapByte (Address >> 16);
    SPI_SwapByte (Address >> 8);
    SPI_SwapByte (Address);
    SPI_Stop();
}

void W25Q64_ReadData (uint32_t Address, uint8_t *DataArray, uint32_t Count) {
    W25Q64_WaitBusy();
    SPI_Start();
    SPI_SwapByte (W25Q64_READ_DATA);
    SPI_SwapByte (Address >> 16);
    SPI_SwapByte (Address >> 8);
    SPI_SwapByte (Address);
    for (uint32_t i = 0; i < Count; ++i) {
        DataArray[i] = SPI_SwapByte (W25Q64_DUMMY_BYTE);
    }
    SPI_Stop();
}
