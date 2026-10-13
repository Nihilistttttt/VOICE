#include "ADS1299.h"
#include "Serial.h"
#include "Message_Parser.h"
#include "Config.h"
#include <string.h>
#include <math.h>

/*============================ �궨�� ============================*/
/* ���Ų����� */
#define ADS1299_CS_LOW() Hal_GPIO_Reset (ADS1299_CS_PIN_ENC)
#define ADS1299_CS_HIGH() Hal_GPIO_Set (ADS1299_CS_PIN_ENC)

#define ADS1299_RESET_LOW() Hal_GPIO_Reset (ADS1299_RESET_PIN_ENC)
#define ADS1299_RESET_HIGH() Hal_GPIO_Set (ADS1299_RESET_PIN_ENC)

#define ADS1299_START_LOW() Hal_GPIO_Reset (ADS1299_START_PIN_ENC)
#define ADS1299_START_HIGH() Hal_GPIO_Set (ADS1299_START_PIN_ENC)


/* ���������� */
#define RING_BUF_SIZE 64
#define FRAME_SIZE ADS1299_FRAME_BYTE_NUM

/*============================ 静态全局变量 ============================*/
static uint8_t ads1299_dma_tx_dummy[FRAME_SIZE] = {0};

static uint8_t dma_buf0[FRAME_SIZE];
static uint8_t dma_buf1[FRAME_SIZE];

static uint8_t ring_buffer[RING_BUF_SIZE][FRAME_SIZE];
static volatile uint8_t head = 0;
static volatile uint8_t tail = 0;

/* ״̬ͳ�Ʊ��� */
volatile uint8_t g_ads1299_spi_dma_busy = 0;
volatile uint32_t g_ads1299_spi_dma_ok_count = 0;
volatile uint32_t g_ads1299_spi_dma_lost_count = 0;
volatile uint32_t g_ads1299_spi_dma_error_count = 0;

/*============================ ˽�к���������static�� ============================*/
/* Ӳ����ʼ����������˳�� */
static void ADS1299_GPIO_Init (void);
static void ADS1299_SPI_Init (void);
static void ADS1299_DMA_Init (void);
static void ADS1299_Register_Init (void);

/* �ײ� SPI �������� */
static uint8_t ADS1299_SPI_TxRxByte (uint8_t tx_data);
static void ADS1299_SPI_WaitBusy (void);
static void ADS1299_SPI_ClearRxNE (void);

/* Ӳ����λ������ */
static void ADS1299_ResetByPin (void);
static void ADS1299_SendCommand (uint8_t cmd);
static int32_t ADS1299_SignExtend24 (uint32_t raw24);

/* �Ĵ������� */
static uint8_t ADS1299_ReadReg (uint8_t reg);
// static void    ADS1299_ReadRegs(uint8_t start_reg, uint8_t *buf, uint8_t len);
static void ADS1299_WriteReg (uint8_t reg, uint8_t value);
// static void    ADS1299_WriteRegs(uint8_t start_reg, const uint8_t *buf, uint8_t len);

/* ����ģʽ���� */
static void ADS1299_StartContinuous (void);
// static void    ADS1299_StopContinuous(void);

/* DMA ���� */
static void ADS1299_DMA_StartRead (void);
static void ADS1299_DMA_StopAndReleaseCS (void);

/* ���ݽ������� */
static int32_t ADS1299_SignExtend24 (uint32_t raw24);


/* Ӳ����ʼ����������˳�� */
uint8_t ADS1299_Init (void) {
    ADS1299_GPIO_Init();
    ADS1299_SPI_Init();
    ADS1299_DMA_Init();
    ADS1299_Register_Init();

    uint8_t id = ADS1299_ReadReg (ADS1299_REG_ID);

    ADS1299_StartContinuous();

    return id;  // ���� ID ���ⲿ���
}

static void ADS1299_GPIO_Init (void) {
    AFIO_ClockEnable();

    GPIO_ClockEnable (ADS1299_CS_PORT);
    GPIO_ClockEnable (ADS1299_DRDY_PORT);
    GPIO_ClockEnable (ADS1299_RESET_PORT);
    GPIO_ClockEnable (ADS1299_START_PORT);
    GPIO_ClockEnable (ADS1299_SCK_PORT);
    GPIO_ClockEnable (ADS1299_MISO_PORT);
    GPIO_ClockEnable (ADS1299_MOSI_PORT);

    /*-------------------- SPI SCK ���ţ��������� --------------------*/
    Hal_GPIO_Init (ADS1299_SCK_ENC, HAL_GPIO_MODE_AF_PP,
                   HAL_GPIO_SPEED_VERY_HIGH, ADS1299_SCK_AF);

    /*-------------------- SPI MOSI ���ţ��������� --------------------*/
    Hal_GPIO_Init (ADS1299_MOSI_ENC, HAL_GPIO_MODE_AF_PP,
                   HAL_GPIO_SPEED_VERY_HIGH, ADS1299_MOSI_AF);

    /*-------------------- SPI MISO ���ţ��������� --------------------*/
    Hal_GPIO_Init (ADS1299_MISO_ENC, HAL_GPIO_MODE_AF_INPUT,
                   HAL_GPIO_SPEED_VERY_HIGH, ADS1299_MISO_AF);

    /*-------------------- CS ���ţ�������� --------------------*/
    Hal_GPIO_Init (ADS1299_CS_PIN_ENC, HAL_GPIO_MODE_OUTPUT_PP,
                   HAL_GPIO_SPEED_VERY_HIGH, 0);
    ADS1299_CS_HIGH();

    /*-------------------- RESET ���ţ�������� --------------------*/
    Hal_GPIO_Init (ADS1299_RESET_PIN_ENC, HAL_GPIO_MODE_OUTPUT_PP,
                   HAL_GPIO_SPEED_VERY_HIGH, 0);
    ADS1299_RESET_HIGH();

    /*-------------------- START ���ţ�������� --------------------*/
    Hal_GPIO_Init (ADS1299_START_PIN_ENC, HAL_GPIO_MODE_OUTPUT_PP,
                   HAL_GPIO_SPEED_VERY_HIGH, 0);
    ADS1299_START_LOW();

    /*-------------------- DRDY ���ţ��������� + �ж����� --------------------*/
    Hal_GPIO_Init (ADS1299_DRDY_PIN_ENC, HAL_GPIO_MODE_INPUT_PU,
                   HAL_GPIO_SPEED_LOW, 0);

    /* ע���жϻص��������½��أ����ȼ� (0,0) */
    Hal_GPIO_IRQ_Config (ADS1299_DRDY_PIN_ENC, HAL_GPIO_IRQ_FALLING,
                         0, 0, ADS1299_DMA_StartRead);
    Hal_GPIO_IRQ_Enable (ADS1299_DRDY_PIN_ENC, true);
}

static void ADS1299_SPI_Init (void) {
    /* ���� SPI2 ����ʱ�ӡ� */
    SPI_ClockEnable (ADS1299_SPI_INSTANCE);

    SPI_InitTypeDef SPI_InitStructure = {0};
    SPI_InitStructure.SPI_Direction = SPI_Direction_2Lines_FullDuplex;
    /* CH32H417 ��Ϊ SPI ������ADS1299 ��Ϊ SPI �ӻ��� */
    SPI_InitStructure.SPI_Mode = SPI_Mode_Master;
    /* ADS1299 ����Ĵ��������ݾ��� 8 bit �ֽڴ��䡣 */
    SPI_InitStructure.SPI_DataSize = SPI_DataSize_8b;
    /* ADS1299 ʹ�� SPI Mode 1��CPOL=0��CPHA=1�� */
    SPI_InitStructure.SPI_CPOL = SPI_CPOL_Low;
    SPI_InitStructure.SPI_CPHA = SPI_CPHA_2Edge;
    /* NSS ���������������� CS �� GPIO �ֶ����ơ� */
    SPI_InitStructure.SPI_NSS = SPI_NSS_Soft;
    /* ��ʹ�ýϵ� SPI Ƶ�ʣ�Ӳ���ȶ���ɸ��� ADS1299 ʱ�����Ƶ�ʵ���ߡ� */
    SPI_InitStructure.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_Mode6;
    /* ADS1299 SPI ���ݸ�λ�ȴ��� */
    SPI_InitStructure.SPI_FirstBit = SPI_FirstBit_MSB;
    /* ��ʹ�� CRC�� */
    SPI_InitStructure.SPI_CRCPolynomial = 7;

    SPI_Init (ADS1299_SPI_INSTANCE, &SPI_InitStructure);
    SPI_Cmd (ADS1299_SPI_INSTANCE, ENABLE);
}

static void ADS1299_DMA_Init (void) {

    DMA_InitTypeDef DMA_InitStructure = {0};
    DMA_ClockEnable (ADS1299_DMA_INSTANCE);

    SPI_I2S_DMACmd (ADS1299_SPI_INSTANCE, SPI_I2S_DMAReq_Rx, DISABLE);
    SPI_I2S_DMACmd (ADS1299_SPI_INSTANCE, SPI_I2S_DMAReq_Tx, DISABLE);

    DMA_Cmd (ADS1299_RX_DMA_CHANNEL, DISABLE);
    DMA_Cmd (ADS1299_TX_DMA_CHANNEL, DISABLE);

    DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)&(ADS1299_SPI_INSTANCE->DATAR);
    DMA_InitStructure.DMA_Memory0BaseAddr = (uint32_t)dma_buf0;
    DMA_InitStructure.DMA_Memory1BaseAddr = (uint32_t)dma_buf1;
    DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralSRC;
    DMA_InitStructure.DMA_BufferSize = FRAME_SIZE;
    DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
    DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
    DMA_InitStructure.DMA_Mode = DMA_Mode_Circular;
    DMA_InitStructure.DMA_M2M = DMA_M2M_Disable;
    DMA_InitStructure.DMA_Priority = DMA_Priority_VeryHigh;
    DMA_InitStructure.DMA_BufferMode = DMA_DoubleBufferMode;
    DMA_InitStructure.DMA_DoubleBuffer_StartMemory = DMA_DoubleBufferMode_Memory_0;

    DMA_Init (ADS1299_RX_DMA_CHANNEL, &DMA_InitStructure);
    DMA_MuxChannelConfig (ADS1299_RX_DMAMUX_CHANNEL, ADS1299_RX_DMA_REQUEST);
    DMA_ITConfig (ADS1299_RX_DMA_CHANNEL, DMA_IT_TC, ENABLE);
    DMA_ITConfig (ADS1299_RX_DMA_CHANNEL, DMA_IT_TE, ENABLE);

    DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)&(ADS1299_SPI_INSTANCE->DATAR);
    DMA_InitStructure.DMA_Memory0BaseAddr = (uint32_t)ads1299_dma_tx_dummy;
    DMA_InitStructure.DMA_Memory1BaseAddr = 0;
    DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralDST;
    DMA_InitStructure.DMA_BufferSize = FRAME_SIZE;
    DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
    DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
    DMA_InitStructure.DMA_Mode = DMA_Mode_Normal;
    DMA_InitStructure.DMA_M2M = DMA_M2M_Disable;
    DMA_InitStructure.DMA_Priority = DMA_Priority_High;
    DMA_InitStructure.DMA_BufferMode = DMA_SingleBufferMode;
    DMA_InitStructure.DMA_DoubleBuffer_StartMemory = DMA_DoubleBufferMode_Memory_0;

    DMA_Init (ADS1299_TX_DMA_CHANNEL, &DMA_InitStructure);
    DMA_MuxChannelConfig (ADS1299_TX_DMAMUX_CHANNEL, ADS1299_TX_DMA_REQUEST);
    DMA_ITConfig (ADS1299_TX_DMA_CHANNEL, DMA_IT_TE, ENABLE);

    DMA_ClearITPendingBit (ADS1299_DMA_INSTANCE, ADS1299_RX_DMA_TC_FLAG);
    DMA_ClearITPendingBit (ADS1299_DMA_INSTANCE, ADS1299_RX_DMA_TE_FLAG);
    DMA_ClearITPendingBit (ADS1299_DMA_INSTANCE, ADS1299_TX_DMA_TC_FLAG);
    DMA_ClearITPendingBit (ADS1299_DMA_INSTANCE, ADS1299_TX_DMA_TE_FLAG);

    NVIC_SetPriority (ADS1299_RX_DMA_IRQn, (1 << 7) | (0 << 4));
    NVIC_EnableIRQ (ADS1299_RX_DMA_IRQn);

    NVIC_SetPriority (ADS1299_TX_DMA_IRQn, (1 << 7) | (0 << 4));
    NVIC_EnableIRQ (ADS1299_TX_DMA_IRQn);

    g_ads1299_spi_dma_busy = 0;
    g_ads1299_spi_dma_ok_count = 0;
    g_ads1299_spi_dma_lost_count = 0;
    g_ads1299_spi_dma_error_count = 0;
}

static void ADS1299_Register_Init (void) {
    /* 1. �ϵ�ȴ� */
    Delay_Ms (ADS1299_POWER_ON_DELAY_MS);

    /* 2. Ӳ����λ */
    ADS1299_ResetByPin();

    /* 3. �˳�������ģʽ */
    ADS1299_SendCommand (ADS1299_CMD_SDATAC);
    Delay_Ms (10);

    /* 4. ���� CONFIG1~CONFIG4 */
    ADS1299_WriteReg (ADS1299_REG_CONFIG1, ADS1299_CFG1_DATARATE_250SPS);
    Delay_Ms (10);
    ADS1299_WriteReg (ADS1299_REG_CONFIG2, ADS1299_CFG2_INT_TEST_OFF);
    Delay_Ms (10);
    ADS1299_WriteReg (ADS1299_REG_CONFIG3, ADS1299_CFG3_BIAS_REF_INT);
    Delay_Ms (ADS1299_REF_STABLE_DELAY_MS);
    ADS1299_WriteReg (ADS1299_REG_CONFIG4, ADS1299_CFG4_SINGLE_SHOT);
    Delay_Ms (ADS1299_REF_STABLE_DELAY_MS);

    ADS1299_WriteReg (ADS1299_REG_BIAS_SENSP, ADS1299_BIAS_SENSP_ALL);
    Delay_Ms (10);
    ADS1299_WriteReg (ADS1299_REG_BIAS_SENSN, ADS1299_BIAS_SENSN_ALL);
    Delay_Ms (10);

    ADS1299_WriteReg (ADS1299_REG_MISC1, ADS1299_MISC1_DEFAULT);
    Delay_Ms (10);

    ADS1299_WriteReg (ADS1299_REG_CH1SET, ADS1299_CHSET_POWER_NORMAL);
    Delay_Ms (10);
    ADS1299_WriteReg (ADS1299_REG_CH2SET, ADS1299_CHSET_POWER_NORMAL);
    Delay_Ms (10);
    ADS1299_WriteReg (ADS1299_REG_CH3SET, ADS1299_CHSET_POWER_NORMAL);
    Delay_Ms (10);
    ADS1299_WriteReg (ADS1299_REG_CH4SET, ADS1299_CHSET_POWER_NORMAL);
    Delay_Ms (10);
    ADS1299_WriteReg (ADS1299_REG_CH5SET, ADS1299_CHSET_POWER_NORMAL);
    Delay_Ms (10);
    ADS1299_WriteReg (ADS1299_REG_CH6SET, ADS1299_CHSET_POWER_NORMAL);
    Delay_Ms (10);
    ADS1299_WriteReg (ADS1299_REG_CH7SET, ADS1299_CHSET_POWER_NORMAL);
    Delay_Ms (10);
    ADS1299_WriteReg (ADS1299_REG_CH8SET, ADS1299_CHSET_POWER_NORMAL);
    Delay_Ms (10);
}

/* �ײ� SPI �������� */
static uint8_t ADS1299_SPI_TxRxByte (uint8_t tx_data) {
    uint32_t timeout;

    /* �ȴ����ͻ�����Ϊ�գ���ʾ����д����һ���ֽڡ� */
    timeout = ADS1299_SPI_TIMEOUT;
    while (SPI_I2S_GetFlagStatus (ADS1299_SPI_INSTANCE, SPI_I2S_FLAG_TXE) == RESET) {
        if (timeout-- == 0) {
            return 0xFF;
        }
    }

    /* д��һ���ֽں�SPI Ӳ�����Զ����� 8 �� SCK ʱ�ӡ� */
    SPI_I2S_SendData (ADS1299_SPI_INSTANCE, tx_data);

    /* �ȴ����ջ������ǿա�SPI ��ȫ˫����ÿ�� 1 �ֽڣ�Ҳ��ͬʱ�յ� 1 �ֽڡ� */
    timeout = ADS1299_SPI_TIMEOUT;
    while (SPI_I2S_GetFlagStatus (ADS1299_SPI_INSTANCE, SPI_I2S_FLAG_RXNE) == RESET) {
        if (timeout-- == 0) {
            return 0xFF;
        }
    }

    return (uint8_t)SPI_I2S_ReceiveData (ADS1299_SPI_INSTANCE);
}

static void ADS1299_SPI_WaitBusy (void) {
    uint32_t timeout = ADS1299_SPI_TIMEOUT;

    /* �ȴ� SPI ���߲�æ����ֹ���һ�� bit ��û��������� CS�� */
    while (SPI_I2S_GetFlagStatus (ADS1299_SPI_INSTANCE, SPI_I2S_FLAG_BSY) != RESET) {
        if (timeout-- == 0) {
            break;
        }
    }
}

static void ADS1299_SPI_ClearRxNE (void) {

    volatile uint16_t dummy;


    while (SPI_I2S_GetFlagStatus (ADS1299_SPI_INSTANCE, SPI_I2S_FLAG_RXNE) != RESET) {

        dummy = SPI_I2S_ReceiveData (ADS1299_SPI_INSTANCE);

        (void)dummy;
    }
}

/* Ӳ����λ������ */
static void ADS1299_ResetByPin (void) {
    /* ��λǰ��ֹͣת�������⸴λ�ڼ� START ״̬��ȷ���� */
    ADS1299_START_LOW();
    ADS1299_CS_HIGH();

    /* RESET �����塣 */
    ADS1299_RESET_HIGH();
    Delay_Ms (10);
    ADS1299_RESET_LOW();
    Delay_Ms (ADS1299_RESET_LOW_DELAY_MS);
    ADS1299_RESET_HIGH();

    /* �ȴ� ADS1299 �ڲ������߼��ͼĴ����ָ�Ĭ��ֵ�� */
    Delay_Ms (ADS1299_POST_RESET_DELAY_MS);
}

static void ADS1299_SendCommand (uint8_t cmd) {
    ADS1299_CS_LOW();
    Delay_Us (2);

    (void)ADS1299_SPI_TxRxByte (cmd);
    ADS1299_SPI_WaitBusy();

    Delay_Us (2);
    ADS1299_CS_HIGH();

    /* ����֮����һ��������֤ ADS1299 ��ʱ�����롣 */
    Delay_Us (10);
}

/* �Ĵ������� */
static uint8_t ADS1299_ReadReg (uint8_t reg) {
    uint8_t value;

    ADS1299_CS_LOW();
    Delay_Us (2);

    /* �� 1 �ֽڣ�RREG | �Ĵ�����ַ�� */
    (void)ADS1299_SPI_TxRxByte ((uint8_t)(ADS1299_CMD_RREG | (reg & 0x1F)));

    /* �� 2 �ֽڣ���ȡ���� - 1����ȡ 1 ���Ĵ��������Է��� 0x00�� */
    (void)ADS1299_SPI_TxRxByte (0x00);

    /* �� 3 �ֽڣ����� dummy byte��ͬʱ�� MISO ���ؼĴ���ֵ�� */
    value = ADS1299_SPI_TxRxByte (0x00);
    ADS1299_SPI_WaitBusy();

    Delay_Us (2);
    ADS1299_CS_HIGH();
    Delay_Us (10);

    return value;
}

// static void ADS1299_ReadRegs (uint8_t start_reg, uint8_t *buf, uint8_t len) {
//     uint8_t i;
//     if ((buf == 0) || (len == 0)) {
//         return;
//     }
//     ADS1299_CS_LOW();
//     Delay_Us (2);
//     /* ��ʼ�Ĵ�����ַ�� */
//     (void)ADS1299_SPI_TxRxByte ((uint8_t)(ADS1299_CMD_RREG | (start_reg & 0x1F)));
//     /* ������ȡ len ���Ĵ������ڶ��ֽ�Ҫ���� len - 1�� */
//     (void)ADS1299_SPI_TxRxByte ((uint8_t)(len - 1));
//     for (i = 0; i < len; i++) {
//         buf[i] = ADS1299_SPI_TxRxByte (0x00);
//     }
//     ADS1299_SPI_WaitBusy();
//     Delay_Us (2);
//     ADS1299_CS_HIGH();
//     Delay_Us (10);
// }

static void ADS1299_WriteReg (uint8_t reg, uint8_t value) {
    ADS1299_CS_LOW();
    Delay_Us (2);

    /* �� 1 �ֽڣ�WREG | �Ĵ�����ַ�� */
    (void)ADS1299_SPI_TxRxByte ((uint8_t)(ADS1299_CMD_WREG | (reg & 0x1F)));

    /* �� 2 �ֽڣ�д������ - 1��д 1 ���Ĵ��������Է��� 0x00�� */
    (void)ADS1299_SPI_TxRxByte (0x00);

    /* �� 3 �ֽڣ�����д��Ĵ��������ݡ� */
    (void)ADS1299_SPI_TxRxByte (value);
    ADS1299_SPI_WaitBusy();

    Delay_Us (2);
    ADS1299_CS_HIGH();
    Delay_Us (10);
}

// static void ADS1299_WriteRegs (uint8_t start_reg, const uint8_t *buf, uint8_t len) {
//     uint8_t i;
//     if ((buf == 0) || (len == 0)) {
//         return;
//     }
//     ADS1299_CS_LOW();
//     Delay_Us (2);
//     /* ��ʼ�Ĵ�����ַ�� */
//     (void)ADS1299_SPI_TxRxByte ((uint8_t)(ADS1299_CMD_WREG | (start_reg & 0x1F)));
//     /* ����д�� len ���Ĵ������ڶ��ֽ�Ҫ���� len - 1�� */
//     (void)ADS1299_SPI_TxRxByte ((uint8_t)(len - 1));
//     for (i = 0; i < len; i++) {
//         (void)ADS1299_SPI_TxRxByte (buf[i]);
//     }
//     ADS1299_SPI_WaitBusy();
//     Delay_Us (2);
//     ADS1299_CS_HIGH();
//     Delay_Us (10);
// }

/* ����ģʽ���� */
static void ADS1299_StartContinuous (void) {
    /*
     * ���� START ���ţ����� ADS1299 ת����
     * ����� RDATAC���� ADS1299 ��������������ģʽ��
     */
    ADS1299_START_HIGH();
    Delay_Ms (2);
    ADS1299_SendCommand (ADS1299_CMD_RDATAC);
    Delay_Ms (2);
}

// static void ADS1299_StopContinuous (void) {
//     /*
//      * �ȷ��� SDATAC �˳�������ģʽ�������� START ֹͣת����
//      * �������Ҫ�޸ļĴ����������ȵ��ñ����������ٷ��� SDATAC��
//      */
//     ADS1299_SendCommand (ADS1299_CMD_SDATAC);
//     Delay_Ms (2);
//     ADS1299_START_LOW();
// }

/* DMA ���� */
void ADS1299_DMA_StartRead (void) {
    if (g_ads1299_spi_dma_busy != 0U) {
        g_ads1299_spi_dma_lost_count++;
        return;
    }

    g_ads1299_spi_dma_busy = 1U;

    DMA_Cmd (ADS1299_RX_DMA_CHANNEL, DISABLE);
    DMA_Cmd (ADS1299_TX_DMA_CHANNEL, DISABLE);

    DMA_ClearITPendingBit (ADS1299_DMA_INSTANCE, ADS1299_RX_DMA_TC_FLAG);
    DMA_ClearITPendingBit (ADS1299_DMA_INSTANCE, ADS1299_RX_DMA_TE_FLAG);
    DMA_ClearITPendingBit (ADS1299_DMA_INSTANCE, ADS1299_TX_DMA_TC_FLAG);
    DMA_ClearITPendingBit (ADS1299_DMA_INSTANCE, ADS1299_TX_DMA_TE_FLAG);

    ADS1299_SPI_ClearRxNE();

    DMA_SetCurrDataCounter (ADS1299_RX_DMA_CHANNEL, FRAME_SIZE);
    ADS1299_TX_DMA_CHANNEL->MADDR = (uint32_t)ads1299_dma_tx_dummy;
    DMA_SetCurrDataCounter (ADS1299_TX_DMA_CHANNEL, FRAME_SIZE);

    ADS1299_CS_LOW();
    Delay_Us (2);

    DMA_Cmd (ADS1299_RX_DMA_CHANNEL, ENABLE);
    DMA_Cmd (ADS1299_TX_DMA_CHANNEL, ENABLE);

    SPI_I2S_DMACmd (ADS1299_SPI_INSTANCE, SPI_I2S_DMAReq_Rx, ENABLE);
    SPI_I2S_DMACmd (ADS1299_SPI_INSTANCE, SPI_I2S_DMAReq_Tx, ENABLE);
}

static void ADS1299_DMA_StopAndReleaseCS (void) {

    SPI_I2S_DMACmd (ADS1299_SPI_INSTANCE, SPI_I2S_DMAReq_Rx, DISABLE);
    SPI_I2S_DMACmd (ADS1299_SPI_INSTANCE, SPI_I2S_DMAReq_Tx, DISABLE);

    DMA_Cmd (ADS1299_RX_DMA_CHANNEL, DISABLE);
    DMA_Cmd (ADS1299_TX_DMA_CHANNEL, DISABLE);

    ADS1299_SPI_WaitBusy();

    Delay_Us (2);
    ADS1299_CS_HIGH();

    g_ads1299_spi_dma_busy = 0U;
}

/* ���ݽ������� */
static int32_t ADS1299_SignExtend24 (uint32_t raw24) {
    /*
     * ADS1299 ͨ�������� 24 λ�����롣
     * �� bit23 Ϊ 1����ʾ��������Ҫ�Ѹ� 8 λ�� 1����չ�� 32 λ�з�������
     */
    raw24 &= 0x00FFFFFFUL;
    if ((raw24 & 0x00800000UL) != 0U) {
        raw24 |= 0xFF000000UL;
    }

    return (int32_t)raw24;
}

float ADS1299_CodeToVolt (int32_t code, float vref, float gain) {
    return (float)((code * vref) / (gain * ADS1299_FULL_SCALE_CODE));
}

float ADS1299_CodeToMicroVolt (int32_t code, float vref, float gain) {
    return ADS1299_CodeToVolt (code, vref, gain) * 1000000.0f;
}

void ADS1299_ParseRawFrame (const uint8_t *frame_buf, uint32_t *status, int32_t ch_data[ADS1299_CHANNEL_NUM]) {
    uint8_t i;
    uint8_t index;
    uint32_t raw24;

    if ((frame_buf == 0) || (ch_data == 0)) {
        return;
    }

    /* ǰ 3 �ֽ���״̬�֣�����ͨ���Ե����ݡ� */
    if (status != 0) {
        *status = ((uint32_t)frame_buf[0] << 16) |
                  ((uint32_t)frame_buf[1] << 8) |
                  ((uint32_t)frame_buf[2]);
    }

    /* ���� 24 �ֽ�Ϊ 8 ��ͨ����ÿͨ�� 3 �ֽڣ�24 bit �����룬���ֽ���ǰ�� */
    for (i = 0; i < ADS1299_CHANNEL_NUM; i++) {
        index = (uint8_t)(ADS1299_STATUS_BYTE_NUM + i * ADS1299_CHANNEL_BYTE_NUM);
        raw24 = ((uint32_t)frame_buf[index] << 16) |
                ((uint32_t)frame_buf[index + 1] << 8) |
                ((uint32_t)frame_buf[index + 2]);

        ch_data[i] = ADS1299_SignExtend24 (raw24);
    }
}

/* �ӻ��λ�������ȡһ֡���ݣ��ɹ����� 1�������ݷ��� 0 */
uint8_t ring_buffer_get_frame (uint8_t *dest) {
    if (head == tail) {
        return 0;
    }
    memcpy (dest, ring_buffer[tail], FRAME_SIZE);
    tail = (tail + 1) % RING_BUF_SIZE;
    return 1;
}

uint8_t ring_buffer_has_frame (void) {
    return (head != tail) ? 1u : 0u;
}

void DMA1_Channel1_IRQHandler (void) __attribute__ ((interrupt ("WCH-Interrupt-fast")));

void DMA1_Channel1_IRQHandler (void) {
    if (DMA_GetITStatus (ADS1299_DMA_INSTANCE, ADS1299_RX_DMA_TC_FLAG) != RESET) {
        DMA_ClearITPendingBit (ADS1299_DMA_INSTANCE, ADS1299_RX_DMA_TC_FLAG);

        ADS1299_DMA_StopAndReleaseCS();
        g_ads1299_spi_dma_ok_count++;

        uint32_t ct = ADS1299_RX_DMA_CHANNEL->CFGR & (1U << 16);
        const uint8_t *completed = (ct != 0) ? dma_buf0 : dma_buf1;

        memcpy (ring_buffer[head], completed, FRAME_SIZE);
        head = (head + 1) % RING_BUF_SIZE;
        if (head == tail) {
            tail = (tail + 1) % RING_BUF_SIZE;
        }
    }
    if (DMA_GetITStatus (ADS1299_DMA_INSTANCE, ADS1299_RX_DMA_TE_FLAG) != RESET) {
        DMA_ClearITPendingBit (ADS1299_DMA_INSTANCE, ADS1299_RX_DMA_TE_FLAG);

        ADS1299_DMA_StopAndReleaseCS();
        g_ads1299_spi_dma_error_count++;
    }
}

void DMA1_Channel2_IRQHandler (void) __attribute__ ((interrupt ("WCH-Interrupt-fast")));

void DMA1_Channel2_IRQHandler (void) {
    if (DMA_GetITStatus (ADS1299_DMA_INSTANCE, ADS1299_TX_DMA_TE_FLAG) != RESET) {
        DMA_ClearITPendingBit (ADS1299_DMA_INSTANCE, ADS1299_TX_DMA_TE_FLAG);

        ADS1299_DMA_StopAndReleaseCS();
        g_ads1299_spi_dma_error_count++;
    }
}

void ADS1299_EnterImpedanceMode (void) {
    ADS1299_SendCommand (ADS1299_CMD_SDATAC);
    Delay_Ms (10);
    ADS1299_START_LOW();
    Delay_Ms (10);

    ADS1299_WriteReg (ADS1299_REG_LOFF, 0x01);
    Delay_Ms (10);
    ADS1299_WriteReg (ADS1299_REG_LOFF_SENSP, 0xFF);
    Delay_Ms (10);
    ADS1299_WriteReg (ADS1299_REG_LOFF_SENSN, 0xFF);
    Delay_Ms (10);
    ADS1299_WriteReg (ADS1299_REG_LOFF_FLIP, 0x00);
    Delay_Ms (10);
    ADS1299_WriteReg (ADS1299_REG_CONFIG4, 0x82u);
    Delay_Ms (10);

    ADS1299_SendCommand (ADS1299_CMD_RDATAC);
    Delay_Ms (2);
    ADS1299_START_HIGH();
    Delay_Ms (100);
}

void ADS1299_ExitImpedanceMode (void) {
    ADS1299_SendCommand (ADS1299_CMD_SDATAC);
    Delay_Ms (10);
    ADS1299_START_LOW();
    Delay_Ms (10);

    ADS1299_WriteReg (ADS1299_REG_LOFF, 0x00);
    Delay_Ms (10);
    ADS1299_WriteReg (ADS1299_REG_LOFF_SENSP, 0x00);
    Delay_Ms (10);
    ADS1299_WriteReg (ADS1299_REG_LOFF_SENSN, 0x00);
    Delay_Ms (10);
    ADS1299_WriteReg (ADS1299_REG_CONFIG4, ADS1299_CFG4_SINGLE_SHOT);
    Delay_Ms (10);

    ADS1299_SendCommand (ADS1299_CMD_RDATAC);
    Delay_Ms (2);
    ADS1299_START_HIGH();
    Delay_Ms (100);
}

void ADS1299_MeasureImpedance (float out_kohm[ADS1299_CHANNEL_NUM]) {
    uint8_t frame[ADS1299_FRAME_BYTE_NUM];
    int32_t ch_data[ADS1299_CHANNEL_NUM];
    uint8_t n = ADS1299_IMPEDANCE_N_FRAMES;
    uint8_t ch, i;

    float goertzel_s[ADS1299_CHANNEL_NUM];
    float goertzel_c[ADS1299_CHANNEL_NUM];

    float fs = 250.0f;
    float f0 = 7.8f;
    float omega = 2.0f * 3.14159265f * f0 / fs;

    float i_na = ADS1299_IMPEDANCE_I_NA;

    for (ch = 0; ch < ADS1299_CHANNEL_NUM; ch++) {
        goertzel_s[ch] = 0.0f;
        goertzel_c[ch] = 0.0f;
    }

    for (i = 0; i < n; i++) {
        uint32_t wait_cnt = 0;
        while (ring_buffer_get_frame(frame) == 0) {
            wait_cnt++;
            if (wait_cnt > 500000UL) break;
        }
        ADS1299_ParseRawFrame(frame, 0, ch_data);

        float angle = omega * (float)i;
        float sin_val = sinf(angle);
        float cos_val = cosf(angle);

        for (ch = 0; ch < ADS1299_CHANNEL_NUM; ch++) {
            float v = ADS1299_CodeToVolt(ch_data[ch],
                        ADS1299_VREF_DEFAULT, ADS1299_GAIN_DEFAULT);
            goertzel_s[ch] += v * sin_val;
            goertzel_c[ch] += v * cos_val;
        }
    }

    float i_amp = i_na * 1e-9f;
    for (ch = 0; ch < ADS1299_CHANNEL_NUM; ch++) {
        float mag_sq = goertzel_s[ch] * goertzel_s[ch]
                      + goertzel_c[ch] * goertzel_c[ch];
        float mag = sqrtf(mag_sq) * 2.0f / (float)n;
        float z_ohm = mag / i_amp;
        out_kohm[ch] = z_ohm / 1000.0f;
    }
}

static uint8_t s_bias_connected = 1u;


uint8_t ADS1299_RecheckBias (void) {
    ADS1299_WriteReg (ADS1299_REG_CONFIG3, 0xE0u | ADS1299_BIAS_LOFF_SENS_BIT);
    Delay_Ms (10);
    s_bias_connected = (uint8_t)((ADS1299_ReadReg (ADS1299_REG_CONFIG3) & 0x01u) == 0u);
    ADS1299_WriteReg (ADS1299_REG_CONFIG3, ADS1299_CFG3_BIAS_REF_INT);
    Delay_Ms (10);
    return s_bias_connected;
}


uint8_t ADS1299_GetBiasStatus (void) {
    return s_bias_connected;
}
