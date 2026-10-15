#include "serial.h"
#include "Config.h"
#include "debug.h"
#include "ch32v30x_conf.h"   /* DMA_InitTypeDef / USART_InitTypeDef 等 */

#if SERIAL_ENABLE

/*============================================================================
 * 多端口串口底座 —— 移植自 EEG 项目 V3F/Hardware/Serial.c
 * 按 CH32V307 改写的差异（与 EEG/CH32H417 原版对照）:
 *   1. 无 DMAMUX（H417 有，V307 是 F1 式 DMA，请求映射由硬件通道决定）
 *   2. DMA 通道按 V307 请求表: USART1_RX=DMA1_Ch5, USART3_RX=Ch3,
 *      USART3_TX=Ch2（DEBUG 口 TX 不用 DMA，见下）
 *   3. HC05 口需 USART3 Full 重映射到 PD8/PD9
 *      （默认映射 PB10/PB11 与 I2C2/ADS1115 冲突，硬件特意走重映射脚）
 *   4. DEBUG 口 TX 改阻塞发送: DMA1_Ch4 已被 V2 的 I2S2 RX 占用
 *      （mic_hum.c），且 2Mbps 阻塞发送 100 字节仅约 0.5ms
 *   5. ESP8266 不建端口: 网表核实其 UART/控制脚未连 MCU（硬件孤岛）
 *============================================================================*/

/* ========== RX 双缓冲 ========== */
static uint8_t USART_Debug_RxPacket[SERIAL_DEBUG_RX_BUF_SIZE];
static uint8_t USART_Debug_RxPacketBuf[SERIAL_DEBUG_RX_BUF_SIZE];

static uint8_t USART_HC05_RxPacket[SERIAL_HC05_RX_BUF_SIZE];
static uint8_t USART_HC05_RxPacketBuf[SERIAL_HC05_RX_BUF_SIZE];

/* ========== TX 环形缓冲（仅 HC05 用） ========== */
static uint8_t USART_HC05_TxRing[SERIAL_HC05_TX_RING_SIZE];

/* ========== 控制块 ========== */
Serial_CtrlBlock Serial_Ctrl[2] = {
    [SERIAL_PORT_DEBUG] = {
        .UARTx          = SERIAL_DEBUG_USART_INSTANCE,
        .BaudRate       = SERIAL_DEBUG_BAUDRATE,

        .RxDMA_Channel  = SERIAL_DEBUG_RX_DMA_CHANNEL,
        .RxPacket       = USART_Debug_RxPacket,
        .RxPacketBuf    = USART_Debug_RxPacketBuf,
        .RxBufSize      = SERIAL_DEBUG_RX_BUF_SIZE,
        .RxLength       = 0,
        .DataReady      = 0,

        .TxDMA_Channel  = NULL,           /* DEBUG 口阻塞发送，不用 DMA */
        .TxRing         = NULL,
        .TxRingSize     = 0,
        .TxHead         = 0,
        .TxTail         = 0,
        .TxDmaLen       = 0,
        .TxBusy         = 0,
        .TxDropCount    = 0,
        .TxCompleteCallback = NULL
    },
    [SERIAL_PORT_HC05] = {
        .UARTx          = SERIAL_HC05_USART_INSTANCE,
        .BaudRate       = SERIAL_HC05_BAUDRATE,

        .RxDMA_Channel  = SERIAL_HC05_RX_DMA_CHANNEL,
        .RxPacket       = USART_HC05_RxPacket,
        .RxPacketBuf    = USART_HC05_RxPacketBuf,
        .RxBufSize      = SERIAL_HC05_RX_BUF_SIZE,
        .RxLength       = 0,
        .DataReady      = 0,

        .TxDMA_Channel  = SERIAL_HC05_TX_DMA_CHANNEL,
        .TxRing         = USART_HC05_TxRing,
        .TxRingSize     = SERIAL_HC05_TX_RING_SIZE,
        .TxHead         = 0,
        .TxTail         = 0,
        .TxDmaLen       = 0,
        .TxBusy         = 0,
        .TxDropCount    = 0,
        .TxCompleteCallback = NULL
    }
};

/* ========== 私有函数前置声明 ========== */
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

/* ========== 时钟初始化 ========== */
static void Serial_Clock_Init(Serial_Port port)
{
    AFIO_ClockEnable();                    /* HC05 口需 USART3 重映射 */
    if (port == SERIAL_PORT_DEBUG) {
        USART_ClockEnable(SERIAL_DEBUG_USART_INSTANCE);
        GPIO_ClockEnable(SERIAL_DEBUG_TX_PORT);
        GPIO_ClockEnable(SERIAL_DEBUG_RX_PORT);
        DMA_ClockEnable(SERIAL_DEBUG_DMA_INSTANCE);
    } else {
        USART_ClockEnable(SERIAL_HC05_USART_INSTANCE);
        GPIO_ClockEnable(SERIAL_HC05_TX_PORT);
        GPIO_ClockEnable(SERIAL_HC05_RX_PORT);
        DMA_ClockEnable(SERIAL_HC05_DMA_INSTANCE);
    }
}

/* ========== GPIO 初始化（使用 Hal_GPIO） ========== */
static void Serial_GPIO_Init(Serial_Port port)
{
    if (port == SERIAL_PORT_DEBUG) {
        /* TX 引脚：复用推挽 */
        Hal_GPIO_Init(SERIAL_DEBUG_TX_PIN_ENC, HAL_GPIO_MODE_AF_PP,
                      HAL_GPIO_SPEED_VERY_HIGH, 0);
        /* RX 引脚：浮空输入 */
        Hal_GPIO_Init(SERIAL_DEBUG_RX_PIN_ENC, HAL_GPIO_MODE_INPUT,
                      HAL_GPIO_SPEED_VERY_HIGH, 0);
    } else {
        Hal_GPIO_Init(SERIAL_HC05_TX_PIN_ENC, HAL_GPIO_MODE_AF_PP,
                      HAL_GPIO_SPEED_VERY_HIGH, 0);
        Hal_GPIO_Init(SERIAL_HC05_RX_PIN_ENC, HAL_GPIO_MODE_INPUT,
                      HAL_GPIO_SPEED_VERY_HIGH, 0);
    }
}

/* ========== USART 初始化 ========== */
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

/* ========== RX DMA 初始化 ========== */
static void Serial_RX_DMA_Init(Serial_Port port)
{
    Serial_CtrlBlock *p = &Serial_Ctrl[port];
    DMA_InitTypeDef d = {0};

    DMA_Cmd(p->RxDMA_Channel, DISABLE);

    d.DMA_PeripheralBaseAddr = (uint32_t)&(p->UARTx->DATAR);
    d.DMA_MemoryBaseAddr    = (uint32_t)p->RxPacket;
    d.DMA_DIR                = DMA_DIR_PeripheralSRC;
    d.DMA_BufferSize         = p->RxBufSize;
    d.DMA_PeripheralInc      = DMA_PeripheralInc_Disable;
    d.DMA_MemoryInc          = DMA_MemoryInc_Enable;
    d.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
    d.DMA_MemoryDataSize     = DMA_MemoryDataSize_Byte;
    d.DMA_Mode               = DMA_Mode_Normal;
    d.DMA_M2M                = DMA_M2M_Disable;
    d.DMA_Priority           = DMA_Priority_VeryHigh;

    DMA_Init(p->RxDMA_Channel, &d);
}

/* ========== TX DMA 初始化（仅 HC05 口） ========== */
static void Serial_TX_DMA_Init(Serial_Port port)
{
    if (port != SERIAL_PORT_HC05) {
        return;                             /* DEBUG 口阻塞发送，无 TX DMA */
    }

    Serial_CtrlBlock *p = &Serial_Ctrl[port];
    DMA_InitTypeDef d = {0};

    DMA_Cmd(p->TxDMA_Channel, DISABLE);

    d.DMA_PeripheralBaseAddr = (uint32_t)&(p->UARTx->DATAR);
    d.DMA_MemoryBaseAddr    = (uint32_t)p->TxRing;
    d.DMA_DIR                = DMA_DIR_PeripheralDST;
    d.DMA_BufferSize         = 1;
    d.DMA_PeripheralInc      = DMA_PeripheralInc_Disable;
    d.DMA_MemoryInc          = DMA_MemoryInc_Enable;
    d.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
    d.DMA_MemoryDataSize     = DMA_MemoryDataSize_Byte;
    d.DMA_Mode               = DMA_Mode_Normal;
    d.DMA_M2M                = DMA_M2M_Disable;
    d.DMA_Priority           = DMA_Priority_High;

    DMA_Init(p->TxDMA_Channel, &d);

    DMA_ITConfig(p->TxDMA_Channel, DMA_IT_TC, ENABLE);
    DMA_ITConfig(p->TxDMA_Channel, DMA_IT_TE, ENABLE);
}

/* ========== NVIC 初始化 ========== */
static void Serial_NVIC_Init(Serial_Port port)
{
    USART_ITConfig(Serial_Ctrl[port].UARTx, USART_IT_IDLE, ENABLE);

    if (port == SERIAL_PORT_DEBUG) {
        NVIC_SetPriority(SERIAL_DEBUG_USART_IRQn, (1 << 7) | (2 << 4));
        NVIC_EnableIRQ(SERIAL_DEBUG_USART_IRQn);
    } else {
        NVIC_SetPriority(SERIAL_HC05_USART_IRQn, (1 << 7) | (2 << 4));
        NVIC_EnableIRQ(SERIAL_HC05_USART_IRQn);
        NVIC_SetPriority(SERIAL_HC05_TX_DMA_IRQn, (1 << 7) | (3 << 4));
        NVIC_EnableIRQ(SERIAL_HC05_TX_DMA_IRQn);
    }
}

/* ========== 启动 USART + RX DMA ========== */
static void Serial_CMD_Init(Serial_Port port)
{
    Serial_CtrlBlock *p = &Serial_Ctrl[port];

    DMA_Cmd(p->RxDMA_Channel, ENABLE);
    USART_DMACmd(p->UARTx, USART_DMAReq_Rx, ENABLE);

    if (port == SERIAL_PORT_HC05) {
        USART_DMACmd(p->UARTx, USART_DMAReq_Tx, ENABLE);
    }

    USART_Cmd(p->UARTx, ENABLE);
}

/* ========== 总初始化 ========== */
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

/* ========== 发送（阻塞式） ========== */
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

/* ========== TX ring 使用量 ========== */
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

/* ========== 拉起一次 DMA 发送（仅 HC05） ========== */
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

    DMA_ClearFlag(DMA1_FLAG_TC2);
    DMA_ClearFlag(DMA1_FLAG_TE2);

    DMA_Cmd(p->TxDMA_Channel, ENABLE);
}

/* ========== DMA 发送（HC05 走 DMA；DEBUG 阻塞） ========== */
uint16_t Serial_SendArray_DMA(Serial_Port port, const uint8_t *array, uint16_t len)
{
    if (array == NULL || len == 0) {
        return 0;
    }

    if (port == SERIAL_PORT_DEBUG) {
        /* DEBUG 口阻塞发送: DMA1_Ch4 已被 V2 的 I2S2 RX 占用，
         * 且 2Mbps 下 100 字节阻塞仅约 0.5ms */
        Serial_SendArray(port, array, len);
        return len;
    }

    __disable_irq();

    uint16_t used = Serial_TxUsedRaw(&Serial_Ctrl[port]);
    uint16_t free_bytes = (uint16_t)(Serial_Ctrl[port].TxRingSize - used - 1);

    if (len > free_bytes) {
        Serial_Ctrl[port].TxDropCount++;
        __enable_irq();
        return 0;
    }

    for (uint16_t i = 0; i < len; i++) {
        Serial_Ctrl[port].TxRing[Serial_Ctrl[port].TxHead++] = array[i];

        if (Serial_Ctrl[port].TxHead >= Serial_Ctrl[port].TxRingSize) {
            Serial_Ctrl[port].TxHead = 0;
        }
    }

    Serial_TxKick(port);

    __enable_irq();

    return len;
}

/* ========== printf（默认走 DMA） ========== */
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

/* ========== 接收查询 ========== */
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

/* ========== USART IDLE 中断处理 ========== */
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
         * CH32V307 的 DMA_Channel_TypeDef 没有 MADDR 段名差异，
         * 这里也要同步修改基地址
         */
        p->RxDMA_Channel->MADDR = (uint32_t)p->RxPacket;

        DMA_Cmd(p->RxDMA_Channel, ENABLE);
    }
}

/* ========== TX DMA 完成处理 ========== */
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

/* ========== USART 中断入口 ========== */
void USART1_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void USART1_IRQHandler(void)
{
    USART_IRQ_Handler(SERIAL_PORT_DEBUG);
}

void USART3_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void USART3_IRQHandler(void)
{
    USART_IRQ_Handler(SERIAL_PORT_HC05);
}

/* ========== DMA TX 完成中断（HC05 = DMA1_Ch2） ========== */
void DMA1_Channel2_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void DMA1_Channel2_IRQHandler(void)
{
    if (DMA_GetITStatus(SERIAL_HC05_TX_DMA_TC_FLAG) != RESET) {
        DMA_ClearITPendingBit(SERIAL_HC05_TX_DMA_TC_FLAG);
        Serial_TxDMA_Done(SERIAL_PORT_HC05);
    }
    if (DMA_GetITStatus(SERIAL_HC05_TX_DMA_TE_FLAG) != RESET) {
        DMA_ClearITPendingBit(SERIAL_HC05_TX_DMA_TE_FLAG);
        DMA_Cmd(Serial_Ctrl[SERIAL_PORT_HC05].TxDMA_Channel, DISABLE);
        Serial_Ctrl[SERIAL_PORT_HC05].TxBusy = 0;
        Serial_Ctrl[SERIAL_PORT_HC05].TxDmaLen = 0;
    }
}

#endif /* SERIAL_ENABLE */
