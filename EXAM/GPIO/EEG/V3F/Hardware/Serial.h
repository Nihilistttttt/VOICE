#ifndef __SERIAL_H
#define __SERIAL_H

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "ch32h417.h"

/* 串口端口选择，抽象化底层 */
typedef enum {
    SERIAL_PORT_DEBUG = 0,
    SERIAL_PORT_WIFI  = 1
} Serial_Port;

/* printf 缓时发送缓冲区大小 */
#define TX_BUF_SIZE  512

/* 控制块结构 */
typedef struct {
    USART_TypeDef*       UARTx;
    uint32_t             BaudRate;

    /* RX DMA */
    DMA_Channel_TypeDef* RxDMA_Channel;
    uint8_t              RxDMAMUX_Channel;
    uint8_t              RxDMA_Request;
    uint8_t*             RxPacket;
    uint8_t*             RxPacketBuf;
    volatile uint16_t    RxLength;
    volatile uint8_t     DataReady;
    uint16_t             RxBufSize;

    /* TX DMA */
    DMA_Channel_TypeDef* TxDMA_Channel;
    uint8_t              TxDMAMUX_Channel;
    uint8_t              TxDMA_Request;
    uint8_t*             TxRing;
    uint16_t             TxRingSize;
    volatile uint16_t    TxHead;
    volatile uint16_t    TxTail;
    volatile uint16_t    TxDmaLen;
    volatile uint8_t     TxBusy;
    volatile uint32_t    TxDropCount;

    void (*TxCompleteCallback)(void);
} Serial_CtrlBlock;

/* 全局控制块数组（外部定义在 .c 文件中） */
extern Serial_CtrlBlock Serial_Ctrl[2];

/* 初始化函数 */
void Serial_Init(Serial_Port port);

/* 发送函数（阻塞式） */
void Serial_SendByte(Serial_Port port, uint8_t byte);
void Serial_SendArray(Serial_Port port, const uint8_t *array, uint16_t len);
void Serial_SendString(Serial_Port port, const char *str);
void Serial_SendNumber(Serial_Port port, uint32_t number, uint8_t len);

/* DMA 发送函数 */
uint16_t Serial_SendArray_DMA(Serial_Port port, const uint8_t *array, uint16_t len);
uint16_t Serial_TxFreeBytes(Serial_Port port);
uint8_t  Serial_IsTxBusy(Serial_Port port);

/* 格式化打印（DMA） */
void Serial_Printf(Serial_Port port, const char *format, ...);

/* 快捷调试打印（自动选 DEBUG 端口） */
void Serial_Dbg_Printf(const char *format, ...);

/* 接收查询 */
uint8_t  Serial_IsDataReady(Serial_Port port);
uint16_t Serial_GetDataPacket(Serial_Port port, uint8_t **buf);

#endif /* __SERIAL_H */
