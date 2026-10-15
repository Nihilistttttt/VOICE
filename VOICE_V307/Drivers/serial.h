#ifndef __SERIAL_H__
#define __SERIAL_H__

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "ch32v30x.h"
#include "hal_gpio.h"

/*============================================================================
 * 端口配置（引脚依据 hardware/电路.txt 网表核实）
 *   HC-05(U11): TXD→CH32_RX3(PD9) RXD→CH32_TX3(PD8) KEY=PB15 LED=PB14
 *   USART3 需 Full 重映射到 PD8/PD9（默认 PB10/PB11 与 I2C2/ADS1115 冲突）
 *   ESP8266(U10): E_* 信号未连 MCU（硬件孤岛），不建端口
 *============================================================================*/

/* ---- DEBUG 口 = USART1（PA9/PA10，默认映射）---- */
#define SERIAL_DEBUG_USART_INSTANCE   USART1
#define SERIAL_DEBUG_BAUDRATE         2000000
#define SERIAL_DEBUG_TX_PORT          0      /* GPIOA */
#define SERIAL_DEBUG_RX_PORT          0
#define SERIAL_DEBUG_TX_PIN_ENC       HAL_GPIO_PIN(0, 9)
#define SERIAL_DEBUG_RX_PIN_ENC       HAL_GPIO_PIN(0, 10)
#define SERIAL_DEBUG_RX_DMA_CHANNEL   DMA1_Channel5   /* USART1_RX */
#define SERIAL_DEBUG_DMA_INSTANCE     DMA1
#define SERIAL_DEBUG_USART_IRQn       USART1_IRQn

/* ---- HC05 口 = USART3（Full 重映射 PD8/PD9）---- */
#define SERIAL_HC05_USART_INSTANCE    USART3
#define SERIAL_HC05_BAUDRATE          9600            /* HC-05 出厂波特率 */
#define SERIAL_HC05_TX_PORT           3               /* GPIOD */
#define SERIAL_HC05_RX_PORT           3
#define SERIAL_HC05_TX_PIN_ENC        HAL_GPIO_PIN(3, 8)
#define SERIAL_HC05_RX_PIN_ENC        HAL_GPIO_PIN(3, 9)
#define SERIAL_HC05_RX_DMA_CHANNEL    DMA1_Channel3   /* USART3_RX */
#define SERIAL_HC05_TX_DMA_CHANNEL    DMA1_Channel2   /* USART3_TX */
#define SERIAL_HC05_DMA_INSTANCE      DMA1
#define SERIAL_HC05_USART_IRQn        USART3_IRQn
#define SERIAL_HC05_TX_DMA_IRQn       DMA1_Channel2_IRQn
#define SERIAL_HC05_TX_DMA_TC_FLAG    DMA1_IT_TC2
#define SERIAL_HC05_TX_DMA_TE_FLAG    DMA1_IT_TE2

/* 缓冲区大小 */
#define SERIAL_DEBUG_RX_BUF_SIZE      128
#define SERIAL_HC05_RX_BUF_SIZE       256
#define SERIAL_HC05_TX_RING_SIZE      256

/*============================================================================
 * 多端口串口底座（移植自 EEG 项目 V3F/Hardware/Serial.c，按 CH32V307 改写）
 *
 * 端口与引脚（依据 hardware/电路.txt 网表核实）:
 *   SERIAL_PORT_DEBUG : USART1  TX=PA9  RX=PA10   2 Mbps
 *                       PC(CH340) ↔ MCU 由 U4 模拟开关 CTRL0 选择：
 *                       烧录态=CH340↔ESP8266 / 工作态=CH340↔MCU
 *   SERIAL_PORT_HC05  : USART3  TX=PD8  RX=PD9    9600（Full 重映射）
 *                       HC-05(U11): TXD→CH32_RX3(PD9) RXD→CH32_TX3(PD8)
 *                       KEY=PB15 LED=PB14（网表核实，不是串口脚）
 *
 *   ★ ESP8266(U10) 的 E_RXD0/E_TXD0/E_RST/E_EN 在 U15 上没有任何引脚
 *     —— 硬件上是孤岛（只接了 LDO2 的 3V3P 电与启动上拉），
 *     因此本底座不提供 ESP8266 端口。
 *
 * RX: DMA 搬运 + IDLE 线中断断帧 → 双缓冲交付，主循环轮询 GetDataPacket
 * TX: DEBUG 口阻塞发送（DMA1_Ch4 已被 V2 的 I2S2 占用，且 2M 波特率
 *     阻塞发送 100 字节仅 0.5ms）；HC05 口走 DMA + 环形缓冲
 *============================================================================*/

typedef enum {
    SERIAL_PORT_DEBUG = 0,   /* USART1  PA9/PA10   2Mbps  调试与命令菜单 */
    SERIAL_PORT_HC05  = 1    /* USART3  PD8/PD9    9600   HC-05 蓝牙 */
} Serial_Port;

/*============================================================================
 * 总开关 SERIAL_ENABLE：0 = serial.c 编译为空（整体退出镜像）
 *
 * 为什么需要它：USART1/3_IRQHandler 是被**中断向量表**引用的，
 * --gc-sections 回收不掉（数据引用，非代码引用）—— 只要 serial.c
 * 参与编译，IRQ 处理函数及其调用链就占着 Flash，即使没人调用。
 * 未接线的端口置 0，向量表回落到启动文件的弱默认 Handler。
 *============================================================================*/
#define SERIAL_ENABLE   0

/* printf 缓时发送缓冲区大小 */
#define TX_BUF_SIZE  512

/* 控制块结构 */
typedef struct {
    USART_TypeDef*       UARTx;
    uint32_t             BaudRate;

    /* RX DMA */
    DMA_Channel_TypeDef* RxDMA_Channel;
    uint8_t*             RxPacket;      /* DMA 直写缓冲 */
    uint8_t*             RxPacketBuf;   /* 交付缓冲（GetDataPacket 给出）*/
    volatile uint16_t    RxLength;
    volatile uint8_t     DataReady;
    uint16_t             RxBufSize;

    /* TX DMA（HC05 口用；DEBUG 口为 NULL = 阻塞发送）*/
    DMA_Channel_TypeDef* TxDMA_Channel;
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

/* DMA 发送函数（HC05 口；DEBUG 口内部退化为阻塞发送）*/
uint16_t Serial_SendArray_DMA(Serial_Port port, const uint8_t *array, uint16_t len);

/* printf（HC05 走 DMA 环形缓冲；DEBUG 走阻塞发送）*/
void Serial_Printf(Serial_Port port, const char *format, ...);
void Serial_Dbg_Printf(const char *format, ...);

/* 接收查询 */
uint8_t Serial_IsDataReady(Serial_Port port);
uint16_t Serial_GetDataPacket(Serial_Port port, uint8_t **buf);

/* TX 状态 */
uint16_t Serial_TxFreeBytes(Serial_Port port);
uint8_t Serial_IsTxBusy(Serial_Port port);

#endif /* __SERIAL_H__ */
