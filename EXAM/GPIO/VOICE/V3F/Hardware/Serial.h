#ifndef __SERIAL_H
#define __SERIAL_H

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "ch32h417.h"

typedef enum {
    SERIAL_PORT_DEBUG = 0,
    SERIAL_PORT_WIFI  = 1
} Serial_Port;

#define TX_BUF_SIZE  512

typedef struct {
    USART_TypeDef       *UARTx;
    uint32_t             BaudRate;

    DMA_Channel_TypeDef *RxDMA_Channel;
    uint8_t              RxDMAMUX_Channel;
    uint8_t              RxDMA_Request;
    uint8_t             *RxPacket;
    uint8_t             *RxPacketBuf;
    volatile uint16_t    RxLength;
    volatile uint8_t     DataReady;
    uint16_t             RxBufSize;

    DMA_Channel_TypeDef *TxDMA_Channel;
    uint8_t              TxDMAMUX_Channel;
    uint8_t              TxDMA_Request;
    uint8_t             *TxRing;
    uint16_t             TxRingSize;
    volatile uint16_t    TxHead;
    volatile uint16_t    TxTail;
    volatile uint16_t    TxDmaLen;
    volatile uint8_t     TxBusy;
    volatile uint32_t    TxDropCount;

    void (*TxCompleteCallback)(void);
} Serial_CtrlBlock;

extern Serial_CtrlBlock Serial_Ctrl[2];

void Serial_Init(Serial_Port port);

void Serial_SendByte(Serial_Port port, uint8_t byte);
void Serial_SendArray(Serial_Port port, const uint8_t *array, uint16_t len);
void Serial_SendString(Serial_Port port, const char *str);
void Serial_SendNumber(Serial_Port port, uint32_t number, uint8_t len);

uint16_t Serial_SendArray_DMA(Serial_Port port, const uint8_t *array, uint16_t len);
uint16_t Serial_TxFreeBytes(Serial_Port port);
uint8_t  Serial_IsTxBusy(Serial_Port port);

void Serial_Printf(Serial_Port port, const char *format, ...);
void Serial_Dbg_Printf(const char *format, ...);

uint8_t  Serial_IsDataReady(Serial_Port port);
uint16_t Serial_GetDataPacket(Serial_Port port, uint8_t **buf);

#endif /* __SERIAL_H */