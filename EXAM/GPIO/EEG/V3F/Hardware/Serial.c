#include "Serial.h"
#include "Config.h"          // �����������ú�
/* ========== RX ������ ========== */
static uint8_t USART_Debug_RxPacket[SERIAL_DEBUG_RX_BUF_SIZE];
static uint8_t USART_Debug_RxPacketBuf[SERIAL_DEBUG_RX_BUF_SIZE];

static uint8_t USART_Wifi_RxPacket[SERIAL_WIFI_RX_BUF_SIZE];
static uint8_t USART_Wifi_RxPacketBuf[SERIAL_WIFI_RX_BUF_SIZE];

/* ========== TX ���λ����� ========== */
static uint8_t USART_Debug_TxRing[SERIAL_DEBUG_TX_RING_SIZE];
static uint8_t USART_Wifi_TxRing[SERIAL_WIFI_TX_RING_SIZE];


/* ========== ���ƿ� ========== */
Serial_CtrlBlock Serial_Ctrl[2] = {
    [SERIAL_PORT_DEBUG] = {
        .UARTx          = SERIAL_DEBUG_USART_INSTANCE,
        .BaudRate       = SERIAL_DEBUG_BAUDRATE,

        .RxDMA_Channel  = SERIAL_DEBUG_RX_DMA_CHANNEL,
        .RxDMAMUX_Channel = SERIAL_DEBUG_RX_DMAMUX_CHANNEL,
        .RxDMA_Request  = SERIAL_DEBUG_RX_DMA_REQUEST,
        .RxPacket       = USART_Debug_RxPacket,
        .RxPacketBuf    = USART_Debug_RxPacketBuf,
        .RxBufSize      = SERIAL_DEBUG_RX_BUF_SIZE,
        .RxLength       = 0,
        .DataReady      = 0,

        .TxDMA_Channel  = SERIAL_DEBUG_TX_DMA_CHANNEL,
        .TxDMAMUX_Channel = SERIAL_DEBUG_TX_DMAMUX_CHANNEL,
        .TxDMA_Request  = SERIAL_DEBUG_TX_DMA_REQUEST,
        .TxRing         = USART_Debug_TxRing,
        .TxRingSize     = SERIAL_DEBUG_TX_RING_SIZE,
        .TxHead         = 0,
        .TxTail         = 0,
        .TxDmaLen       = 0,
        .TxBusy         = 0,
        .TxDropCount    = 0,
        .TxCompleteCallback = NULL
    },
    [SERIAL_PORT_WIFI] = {
        .UARTx          = SERIAL_WIFI_USART_INSTANCE,
        .BaudRate       = SERIAL_WIFI_BAUDRATE,

        .RxDMA_Channel  = SERIAL_WIFI_RX_DMA_CHANNEL,
        .RxDMAMUX_Channel = SERIAL_WIFI_RX_DMAMUX_CHANNEL,
        .RxDMA_Request  = SERIAL_WIFI_RX_DMA_REQUEST,
        .RxPacket       = USART_Wifi_RxPacket,
        .RxPacketBuf    = USART_Wifi_RxPacketBuf,
        .RxBufSize      = SERIAL_WIFI_RX_BUF_SIZE,
        .RxLength       = 0,
        .DataReady      = 0,

        .TxDMA_Channel  = SERIAL_WIFI_TX_DMA_CHANNEL,
        .TxDMAMUX_Channel = SERIAL_WIFI_TX_DMAMUX_CHANNEL,
        .TxDMA_Request  = SERIAL_WIFI_TX_DMA_REQUEST,
        .TxRing         = USART_Wifi_TxRing,
        .TxRingSize     = SERIAL_WIFI_TX_RING_SIZE,
        .TxHead         = 0,
        .TxTail         = 0,
        .TxDmaLen       = 0,
        .TxBusy         = 0,
        .TxDropCount    = 0,
        .TxCompleteCallback = NULL
    }
};

/* ========== ˽�к������� ========== */
static void Serial_Clock_Init(Serial_Port port);
static void Serial_GPIO_Init(Serial_Port port);
static void Serial_USART_Init(Serial_Port port);
static void Serial_RX_DMA_Init(Serial_Port port);
static void Serial_TX_DMA_Init(Serial_Port port);
static void Serial_NVIC_Init(Serial_Port port);
static void Serial_CMD_Init(Serial_Port port);

static uint16_t Serial_TxUsedRaw(Serial_CtrlBlock *p);
static void Serial_TxKick(Serial_Port port);
static void Serial_TxDMA_Done(Serial_Port port);

static void USART_IRQ_Handler(Serial_Port port);

/* ========== ʱ�ӳ�ʼ�� ========== */
static void Serial_Clock_Init(Serial_Port port)
{
    /* ʹ�� AFIO ʱ�� */
    AFIO_ClockEnable();
    if (port == SERIAL_PORT_DEBUG) {
        USART_ClockEnable(SERIAL_DEBUG_USART_INSTANCE);
        GPIO_ClockEnable(SERIAL_DEBUG_TX_PORT);
        GPIO_ClockEnable(SERIAL_DEBUG_RX_PORT);
        DMA_ClockEnable(SERIAL_DEBUG_DMA_INSTANCE);
    } else {
        USART_ClockEnable(SERIAL_WIFI_USART_INSTANCE);
        GPIO_ClockEnable(SERIAL_WIFI_TX_PORT);
        GPIO_ClockEnable(SERIAL_WIFI_RX_PORT);
        DMA_ClockEnable(SERIAL_WIFI_DMA_INSTANCE);
    }
}

/* ========== GPIO ��ʼ����ʹ�� Hal_GPIO�� ========== */
static void Serial_GPIO_Init(Serial_Port port)
{
    if (port == SERIAL_PORT_DEBUG) {
        /* TX ���ţ��������� */
        Hal_GPIO_Init(SERIAL_DEBUG_TX_PIN_ENC, HAL_GPIO_MODE_AF_PP,
                      HAL_GPIO_SPEED_VERY_HIGH, SERIAL_DEBUG_TX_AF);
        /* RX ���ţ��������� */
        Hal_GPIO_Init(SERIAL_DEBUG_RX_PIN_ENC, HAL_GPIO_MODE_AF_PP,
                      HAL_GPIO_SPEED_VERY_HIGH, SERIAL_DEBUG_RX_AF);
    } else {
        Hal_GPIO_Init(SERIAL_WIFI_TX_PIN_ENC, HAL_GPIO_MODE_AF_PP,
                      HAL_GPIO_SPEED_VERY_HIGH, SERIAL_WIFI_TX_AF);
        Hal_GPIO_Init(SERIAL_WIFI_RX_PIN_ENC, HAL_GPIO_MODE_AF_PP,
                      HAL_GPIO_SPEED_VERY_HIGH, SERIAL_WIFI_RX_AF);
    }
}

/* ========== USART ��ʼ�� ========== */
static void Serial_USART_Init(Serial_Port port)
{
    USART_InitTypeDef s = {0};
    Serial_CtrlBlock *p = &Serial_Ctrl[port];

    s.USART_BaudRate = p->BaudRate;
    s.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    s.USART_Mode = USART_Mode_Tx | USART_Mode_Rx;
    s.USART_Parity = USART_Parity_No;
    s.USART_StopBits = USART_StopBits_1;
    s.USART_WordLength = USART_WordLength_8b;

    USART_Init(p->UARTx, &s);
}

/* ========== RX DMA ��ʼ�� ========== */
static void Serial_RX_DMA_Init(Serial_Port port)
{
    Serial_CtrlBlock *p = &Serial_Ctrl[port];
    DMA_InitTypeDef d = {0};

    DMA_Cmd(p->RxDMA_Channel, DISABLE);

    d.DMA_PeripheralBaseAddr = (uint32_t)&(p->UARTx->DATAR);
    d.DMA_Memory0BaseAddr = (uint32_t)p->RxPacket;
    d.DMA_DIR = DMA_DIR_PeripheralSRC;
    d.DMA_BufferSize = p->RxBufSize;
    d.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    d.DMA_MemoryInc = DMA_MemoryInc_Enable;
    d.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
    d.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
    d.DMA_Mode = DMA_Mode_Normal;
    d.DMA_M2M = DMA_M2M_Disable;
    d.DMA_Priority = DMA_Priority_VeryHigh;

    DMA_Init(p->RxDMA_Channel, &d);
    DMA_MuxChannelConfig(p->RxDMAMUX_Channel, p->RxDMA_Request);
}

/* ========== TX DMA ��ʼ�� ========== */
static void Serial_TX_DMA_Init(Serial_Port port)
{
    Serial_CtrlBlock *p = &Serial_Ctrl[port];
    DMA_InitTypeDef d = {0};

    DMA_Cmd(p->TxDMA_Channel, DISABLE);

    d.DMA_PeripheralBaseAddr = (uint32_t)&(p->UARTx->DATAR);
    d.DMA_Memory0BaseAddr = (uint32_t)p->TxRing;
    d.DMA_DIR = DMA_DIR_PeripheralDST;
    d.DMA_BufferSize = 1;
    d.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    d.DMA_MemoryInc = DMA_MemoryInc_Enable;
    d.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
    d.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
    d.DMA_Mode = DMA_Mode_Normal;
    d.DMA_M2M = DMA_M2M_Disable;
    d.DMA_Priority = DMA_Priority_High;

    DMA_Init(p->TxDMA_Channel, &d);
    DMA_MuxChannelConfig(p->TxDMAMUX_Channel, p->TxDMA_Request);

    DMA_ITConfig(p->TxDMA_Channel, DMA_IT_TC, ENABLE);
    DMA_ITConfig(p->TxDMA_Channel, DMA_IT_TE, ENABLE);
}

/* ========== NVIC ��ʼ�� ========== */
static void Serial_NVIC_Init(Serial_Port port)
{
    USART_ITConfig(Serial_Ctrl[port].UARTx, USART_IT_IDLE, ENABLE);

    if (port == SERIAL_PORT_DEBUG) {
        NVIC_SetPriority(SERIAL_DEBUG_USART_IRQn, (1 << 7) | (2 << 4));
        NVIC_EnableIRQ(SERIAL_DEBUG_USART_IRQn);
        NVIC_SetPriority(SERIAL_DEBUG_TX_DMA_IRQn, (1 << 7) | (3 << 4));
        NVIC_EnableIRQ(SERIAL_DEBUG_TX_DMA_IRQn);
    } else {
        NVIC_SetPriority(SERIAL_WIFI_USART_IRQn, (1 << 7) | (2 << 4));
        NVIC_EnableIRQ(SERIAL_WIFI_USART_IRQn);
        NVIC_SetPriority(SERIAL_WIFI_TX_DMA_IRQn, (1 << 7) | (3 << 4));
        NVIC_EnableIRQ(SERIAL_WIFI_TX_DMA_IRQn);
    }
}


/* ========== ���� USART + RX DMA ========== */
static void Serial_CMD_Init(Serial_Port port)
{
    Serial_CtrlBlock *p = &Serial_Ctrl[port];

    DMA_Cmd(p->RxDMA_Channel, ENABLE);
    USART_DMACmd(p->UARTx, USART_DMAReq_Rx, ENABLE);

    USART_DMACmd(p->UARTx, USART_DMAReq_Tx, ENABLE);

    USART_Cmd(p->UARTx, ENABLE);
}

/* ========== ������ʼ�� ========== */
void Serial_Init(Serial_Port port)
{
    Serial_Clock_Init(port);
    Serial_GPIO_Init(port);
    Serial_USART_Init(port);
    Serial_RX_DMA_Init(port);
    Serial_TX_DMA_Init(port);
    Serial_NVIC_Init(port);
    Serial_CMD_Init(port);
}

/* ========== �������ͺ��������� ========== */
void Serial_SendByte(Serial_Port port, uint8_t byte)
{
    USART_SendData(Serial_Ctrl[port].UARTx, byte);
    while (USART_GetFlagStatus(Serial_Ctrl[port].UARTx, USART_FLAG_TXE) == RESET);
}

void Serial_SendArray(Serial_Port port, const uint8_t *array, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++) {
        Serial_SendByte(port, array[i]);
    }
}

void Serial_SendString(Serial_Port port, const char *str)
{
    while (*str) {
        Serial_SendByte(port, (uint8_t)*str++);
    }
}

void Serial_SendNumber(Serial_Port port, uint32_t number, uint8_t len)
{
    uint32_t pow = 1;

    for (uint8_t i = 1; i < len; i++) {
        pow *= 10;
    }

    for (uint8_t i = 0; i < len; i++) {
        Serial_SendByte(port, (uint8_t)(number / pow % 10 + '0'));
        pow /= 10;
    }
}

/* ========== TX ring ʹ���� ========== */
static uint16_t Serial_TxUsedRaw(Serial_CtrlBlock *p)
{
    if (p->TxHead >= p->TxTail) {
        return (uint16_t)(p->TxHead - p->TxTail);
    }

    return (uint16_t)(p->TxRingSize - p->TxTail + p->TxHead);
}

uint16_t Serial_TxFreeBytes(Serial_Port port)
{
    Serial_CtrlBlock *p = &Serial_Ctrl[port];

    __disable_irq();
    uint16_t used = Serial_TxUsedRaw(p);
    uint16_t free_bytes = (uint16_t)(p->TxRingSize - used - 1);
    __enable_irq();

    return free_bytes;
}

uint8_t Serial_IsTxBusy(Serial_Port port)
{
    return Serial_Ctrl[port].TxBusy;
}

/* ========== ����һ�� DMA ���� ========== */
static void Serial_TxKick(Serial_Port port)
{
    Serial_CtrlBlock *p = &Serial_Ctrl[port];

    if (p->TxBusy) {
        return;
    }

    if (p->TxHead == p->TxTail) {
        return;
    }

    uint16_t len;

    if (p->TxHead > p->TxTail) {
        len = (uint16_t)(p->TxHead - p->TxTail);
    } else {
        len = (uint16_t)(p->TxRingSize - p->TxTail);
    }

    p->TxDmaLen = len;
    p->TxBusy = 1;

    DMA_Cmd(p->TxDMA_Channel, DISABLE);


    p->TxDMA_Channel->MADDR = (uint32_t)&p->TxRing[p->TxTail];

    DMA_SetCurrDataCounter(p->TxDMA_Channel, len);

    if (port == SERIAL_PORT_DEBUG) {
        DMA_ClearITPendingBit(SERIAL_DEBUG_DMA_INSTANCE, SERIAL_DEBUG_TX_DMA_TC_FLAG);
        DMA_ClearITPendingBit(SERIAL_DEBUG_DMA_INSTANCE, SERIAL_DEBUG_TX_DMA_TE_FLAG);
    } else {
        DMA_ClearITPendingBit(SERIAL_WIFI_DMA_INSTANCE, SERIAL_WIFI_TX_DMA_TC_FLAG);
        DMA_ClearITPendingBit(SERIAL_WIFI_DMA_INSTANCE, SERIAL_WIFI_TX_DMA_TE_FLAG);
    }

    DMA_Cmd(p->TxDMA_Channel, ENABLE);
}

/* ========== DMA ���������� ========== */
uint16_t Serial_SendArray_DMA(Serial_Port port, const uint8_t *array, uint16_t len)
{
    Serial_CtrlBlock *p = &Serial_Ctrl[port];

    if (array == NULL || len == 0) {
        return 0;
    }

    __disable_irq();

    uint16_t used = Serial_TxUsedRaw(p);
    uint16_t free_bytes = (uint16_t)(p->TxRingSize - used - 1);

    if (len > free_bytes) {
        p->TxDropCount++;
        __enable_irq();
        return 0;
    }

    for (uint16_t i = 0; i < len; i++) {
        p->TxRing[p->TxHead++] = array[i];

        if (p->TxHead >= p->TxRingSize) {
            p->TxHead = 0;
        }
    }

    Serial_TxKick(port);

    __enable_irq();

    return len;
}

/* ========== printf��Ĭ���� DMA ========== */
void Serial_Printf(Serial_Port port, const char *format, ...)
{
    char buf[TX_BUF_SIZE];
    va_list arg;

    va_start(arg, format);
    int len = vsnprintf(buf, sizeof(buf), format, arg);
    va_end(arg);

    if (len <= 0) {
        return;
    }

    if (len > (int)sizeof(buf)) {
        len = (int)sizeof(buf);
    }

    (void)Serial_SendArray_DMA(port, (const uint8_t *)buf, (uint16_t)len);
}

void Serial_Dbg_Printf(const char *format, ...)
{
    char buf[TX_BUF_SIZE];
    va_list arg;

    va_start(arg, format);
    int len = vsnprintf(buf, sizeof(buf), format, arg);
    va_end(arg);

    if (len <= 0) {
        return;
    }

    if (len > (int)sizeof(buf)) {
        len = (int)sizeof(buf);
    }

    (void)Serial_SendArray_DMA(SERIAL_PORT_DEBUG, (const uint8_t *)buf, (uint16_t)len);
}

/* ========== ���ղ�ѯ ========== */
uint8_t Serial_IsDataReady(Serial_Port port)
{
    return Serial_Ctrl[port].DataReady;
}

uint16_t Serial_GetDataPacket(Serial_Port port, uint8_t **buf)
{
    Serial_CtrlBlock *p = &Serial_Ctrl[port];

    if (p->DataReady) {
        *buf = p->RxPacketBuf;
        p->DataReady = 0;
        return p->RxLength;
    }

    return 0;
}

/* ========== USART IDLE �жϴ��� ========== */
static void USART_IRQ_Handler(Serial_Port port)
{
    Serial_CtrlBlock *p = &Serial_Ctrl[port];

    if (USART_GetITStatus(p->UARTx, USART_IT_IDLE) == SET) {
        volatile uint32_t temp;

        temp = p->UARTx->STATR;
        temp = p->UARTx->DATAR;
        (void)temp;

        DMA_Cmd(p->RxDMA_Channel, DISABLE);

        uint16_t remaining = DMA_GetCurrDataCounter(p->RxDMA_Channel);
        uint16_t len = (uint16_t)(p->RxBufSize - remaining);

        if (len > 0 && len <= p->RxBufSize) {
            memcpy(p->RxPacketBuf, p->RxPacket, len);
            p->RxLength = len;
            p->DataReady = 1;
        }

        DMA_SetCurrDataCounter(p->RxDMA_Channel, p->RxBufSize);

        /*
         * ������ DMA_Channel_TypeDef û�� MADDR �ֶΣ�����ҲҪͬ���޸ġ�
         */
        p->RxDMA_Channel->MADDR = (uint32_t)p->RxPacket;

        DMA_Cmd(p->RxDMA_Channel, ENABLE);
    }
}

/* ========== TX DMA ��ɴ��� ========== */
static void Serial_TxDMA_Done(Serial_Port port)
{
    Serial_CtrlBlock *p = &Serial_Ctrl[port];

    DMA_Cmd(p->TxDMA_Channel, DISABLE);

    p->TxTail += p->TxDmaLen;

    if (p->TxTail >= p->TxRingSize) {
        p->TxTail = (uint16_t)(p->TxTail - p->TxRingSize);
    }

    p->TxDmaLen = 0;
    p->TxBusy = 0;

    Serial_TxKick(port);
}

/* ========== USART �жϺ��� ========== */
void USART1_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void USART1_IRQHandler(void)
{
    USART_IRQ_Handler(SERIAL_PORT_DEBUG);
}

void USART2_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void USART2_IRQHandler(void)
{
    USART_IRQ_Handler(SERIAL_PORT_WIFI);
}

/* ========== DMA TX �жϺ�����ʹ�����ú꣩ ========== */
void DMA1_Channel4_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void DMA1_Channel4_IRQHandler(void)
{
    if (DMA_GetITStatus(SERIAL_DEBUG_DMA_INSTANCE, SERIAL_DEBUG_TX_DMA_TC_FLAG) != RESET) {
        DMA_ClearITPendingBit(SERIAL_DEBUG_DMA_INSTANCE, SERIAL_DEBUG_TX_DMA_TC_FLAG);
        Serial_TxDMA_Done(SERIAL_PORT_DEBUG);
    }
    if (DMA_GetITStatus(SERIAL_DEBUG_DMA_INSTANCE, SERIAL_DEBUG_TX_DMA_TE_FLAG) != RESET) {
        DMA_ClearITPendingBit(SERIAL_DEBUG_DMA_INSTANCE, SERIAL_DEBUG_TX_DMA_TE_FLAG);
        DMA_Cmd(Serial_Ctrl[SERIAL_PORT_DEBUG].TxDMA_Channel, DISABLE);
        Serial_Ctrl[SERIAL_PORT_DEBUG].TxBusy = 0;
        Serial_Ctrl[SERIAL_PORT_DEBUG].TxDmaLen = 0;
    }
}

void DMA1_Channel5_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void DMA1_Channel5_IRQHandler(void)
{
    if (DMA_GetITStatus(SERIAL_WIFI_DMA_INSTANCE, SERIAL_WIFI_TX_DMA_TC_FLAG) != RESET) {
        DMA_ClearITPendingBit(SERIAL_WIFI_DMA_INSTANCE, SERIAL_WIFI_TX_DMA_TC_FLAG);
        Serial_TxDMA_Done(SERIAL_PORT_WIFI);
    }
    if (DMA_GetITStatus(SERIAL_WIFI_DMA_INSTANCE, SERIAL_WIFI_TX_DMA_TE_FLAG) != RESET) {
        DMA_ClearITPendingBit(SERIAL_WIFI_DMA_INSTANCE, SERIAL_WIFI_TX_DMA_TE_FLAG);
        DMA_Cmd(Serial_Ctrl[SERIAL_PORT_WIFI].TxDMA_Channel, DISABLE);
        Serial_Ctrl[SERIAL_PORT_WIFI].TxBusy = 0;
        Serial_Ctrl[SERIAL_PORT_WIFI].TxDmaLen = 0;
    }
}