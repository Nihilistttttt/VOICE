#ifndef __CONFIG_H
#define __CONFIG_H

#include "ch32h417.h"
#include "hal_gpio.h"

/*============================================================================
 * Board Version Selection
 *============================================================================*/
#if !defined(BOARD_V1) && !defined(BOARD_V2)
#define BOARD_V1
#endif

/*============================================================================
 * BOARD V1  (VOICE 默认板)
 *   USART1 (DEBUG) : PA9(TX)  / PA10(RX)  AF7  2Mbaud
 *   USART2 (CMD)   : PD5(TX)  / PD6(RX)   AF7  2Mbaud
 *============================================================================*/
#if defined(BOARD_V1)

#ifdef __SERIAL_H
/* Serial DEBUG (USART1) */
#define SERIAL_DEBUG_USART_INSTANCE    USART1
#define SERIAL_DEBUG_BAUDRATE          2000000
#define SERIAL_DEBUG_TX_PORT           0
#define SERIAL_DEBUG_TX_PIN            9
#define SERIAL_DEBUG_TX_PIN_ENC        HAL_GPIO_PIN(SERIAL_DEBUG_TX_PORT, SERIAL_DEBUG_TX_PIN)
#define SERIAL_DEBUG_TX_AF             GPIO_AF7
#define SERIAL_DEBUG_RX_PORT           0
#define SERIAL_DEBUG_RX_PIN            10
#define SERIAL_DEBUG_RX_PIN_ENC        HAL_GPIO_PIN(SERIAL_DEBUG_RX_PORT, SERIAL_DEBUG_RX_PIN)
#define SERIAL_DEBUG_RX_AF             GPIO_AF7
#define SERIAL_DEBUG_DMA_INSTANCE      DMA1
#define SERIAL_DEBUG_TX_DMA_CHANNEL    DMA1_Channel4
#define SERIAL_DEBUG_TX_DMAMUX_CHANNEL DMA_MuxChannel4
#define SERIAL_DEBUG_TX_DMA_REQUEST    85U
#define SERIAL_DEBUG_RX_DMA_CHANNEL    DMA1_Channel6
#define SERIAL_DEBUG_RX_DMAMUX_CHANNEL DMA_MuxChannel6
#define SERIAL_DEBUG_RX_DMA_REQUEST    86U
#define SERIAL_DEBUG_TX_DMA_TC_FLAG    DMA1_IT_TC4
#define SERIAL_DEBUG_TX_DMA_TE_FLAG    DMA1_IT_TE4
#define SERIAL_DEBUG_USART_IRQn        USART1_IRQn
#define SERIAL_DEBUG_TX_DMA_IRQn       DMA1_Channel4_IRQn
#define SERIAL_DEBUG_RX_BUF_SIZE       1024
#define SERIAL_DEBUG_TX_RING_SIZE      2048

/* Serial CMD (USART2) */
#define SERIAL_WIFI_USART_INSTANCE      USART2
#define SERIAL_WIFI_BAUDRATE            2000000
#define SERIAL_WIFI_TX_PORT             3
#define SERIAL_WIFI_TX_PIN              5
#define SERIAL_WIFI_TX_PIN_ENC          HAL_GPIO_PIN(SERIAL_WIFI_TX_PORT, SERIAL_WIFI_TX_PIN)
#define SERIAL_WIFI_TX_AF               GPIO_AF7
#define SERIAL_WIFI_RX_PORT             3
#define SERIAL_WIFI_RX_PIN              6
#define SERIAL_WIFI_RX_PIN_ENC          HAL_GPIO_PIN(SERIAL_WIFI_RX_PORT, SERIAL_WIFI_RX_PIN)
#define SERIAL_WIFI_RX_AF               GPIO_AF7
#define SERIAL_WIFI_DMA_INSTANCE       DMA1
#define SERIAL_WIFI_TX_DMA_CHANNEL      DMA1_Channel5
#define SERIAL_WIFI_TX_DMAMUX_CHANNEL   DMA_MuxChannel5
#define SERIAL_WIFI_TX_DMA_REQUEST      87U
#define SERIAL_WIFI_RX_DMA_CHANNEL      DMA1_Channel7
#define SERIAL_WIFI_RX_DMAMUX_CHANNEL   DMA_MuxChannel7
#define SERIAL_WIFI_RX_DMA_REQUEST      88U
#define SERIAL_WIFI_TX_DMA_TC_FLAG      DMA1_IT_TC5
#define SERIAL_WIFI_TX_DMA_TE_FLAG      DMA1_IT_TE5
#define SERIAL_WIFI_USART_IRQn          USART2_IRQn
#define SERIAL_WIFI_TX_DMA_IRQn         DMA1_Channel5_IRQn
#define SERIAL_WIFI_RX_BUF_SIZE         2048
#define SERIAL_WIFI_TX_RING_SIZE        8192
#endif /* __SERIAL_H */

#endif /* BOARD_V1 */

/*============================================================================
 * BOARD V2 (备用)
 *   USART1 (DEBUG) : PB6(TX) / PB7(RX)  AF7  2Mbaud
 *   USART2 (CMD)   : PA2(TX) / PA3(RX)  AF7  2Mbaud
 *============================================================================*/
#if defined(BOARD_V2)

#ifdef __SERIAL_H
#define SERIAL_DEBUG_USART_INSTANCE    USART1
#define SERIAL_DEBUG_BAUDRATE          2000000
#define SERIAL_DEBUG_TX_PORT           1
#define SERIAL_DEBUG_TX_PIN            6
#define SERIAL_DEBUG_TX_PIN_ENC        HAL_GPIO_PIN(SERIAL_DEBUG_TX_PORT, SERIAL_DEBUG_TX_PIN)
#define SERIAL_DEBUG_TX_AF             GPIO_AF7
#define SERIAL_DEBUG_RX_PORT           1
#define SERIAL_DEBUG_RX_PIN            7
#define SERIAL_DEBUG_RX_PIN_ENC        HAL_GPIO_PIN(SERIAL_DEBUG_RX_PORT, SERIAL_DEBUG_RX_PIN)
#define SERIAL_DEBUG_RX_AF             GPIO_AF7
#define SERIAL_DEBUG_DMA_INSTANCE      DMA1
#define SERIAL_DEBUG_TX_DMA_CHANNEL    DMA1_Channel4
#define SERIAL_DEBUG_TX_DMAMUX_CHANNEL DMA_MuxChannel4
#define SERIAL_DEBUG_TX_DMA_REQUEST    85U
#define SERIAL_DEBUG_RX_DMA_CHANNEL    DMA1_Channel6
#define SERIAL_DEBUG_RX_DMAMUX_CHANNEL DMA_MuxChannel6
#define SERIAL_DEBUG_RX_DMA_REQUEST    86U
#define SERIAL_DEBUG_TX_DMA_TC_FLAG    DMA1_IT_TC4
#define SERIAL_DEBUG_TX_DMA_TE_FLAG    DMA1_IT_TE4
#define SERIAL_DEBUG_USART_IRQn        USART1_IRQn
#define SERIAL_DEBUG_TX_DMA_IRQn       DMA1_Channel4_IRQn
#define SERIAL_DEBUG_RX_BUF_SIZE       1024
#define SERIAL_DEBUG_TX_RING_SIZE      2048

#define SERIAL_WIFI_USART_INSTANCE      USART2
#define SERIAL_WIFI_BAUDRATE            2000000
#define SERIAL_WIFI_TX_PORT             0
#define SERIAL_WIFI_TX_PIN              2
#define SERIAL_WIFI_TX_PIN_ENC          HAL_GPIO_PIN(SERIAL_WIFI_TX_PORT, SERIAL_WIFI_TX_PIN)
#define SERIAL_WIFI_TX_AF               GPIO_AF7
#define SERIAL_WIFI_RX_PORT             0
#define SERIAL_WIFI_RX_PIN              3
#define SERIAL_WIFI_RX_PIN_ENC          HAL_GPIO_PIN(SERIAL_WIFI_RX_PORT, SERIAL_WIFI_RX_PIN)
#define SERIAL_WIFI_RX_AF               GPIO_AF7
#define SERIAL_WIFI_DMA_INSTANCE       DMA1
#define SERIAL_WIFI_TX_DMA_CHANNEL      DMA1_Channel5
#define SERIAL_WIFI_TX_DMAMUX_CHANNEL   DMA_MuxChannel5
#define SERIAL_WIFI_TX_DMA_REQUEST      87U
#define SERIAL_WIFI_RX_DMA_CHANNEL      DMA1_Channel7
#define SERIAL_WIFI_RX_DMAMUX_CHANNEL   DMA_MuxChannel7
#define SERIAL_WIFI_RX_DMA_REQUEST      88U
#define SERIAL_WIFI_TX_DMA_TC_FLAG      DMA1_IT_TC5
#define SERIAL_WIFI_TX_DMA_TE_FLAG      DMA1_IT_TE5
#define SERIAL_WIFI_USART_IRQn          USART2_IRQn
#define SERIAL_WIFI_TX_DMA_IRQn         DMA1_Channel5_IRQn
#define SERIAL_WIFI_RX_BUF_SIZE         2048
#define SERIAL_WIFI_TX_RING_SIZE        8192
#endif /* __SERIAL_H */

#endif /* BOARD_V2 */

/*============================================================================
 * I2S2 INMP441@2引脚 (板子无关)
 *   WS=PB12(AF5) CK=PB13(AF5) SD=PC1(AF5)
 *============================================================================*/
#define I2S2_WS_PIN_ENC    HAL_GPIO_PIN(1, 12)
#define I2S2_CK_PIN_ENC    HAL_GPIO_PIN(1, 13)
#define I2S2_SD_PIN_ENC    HAL_GPIO_PIN(2, 1)
#define I2S2_GPIO_AF       GPIO_AF5

/*============================================================================
 * I2S3 INMP441@2引脚 (板子无关)
 *   WS=PA4(AF6) CK=PC10(AF6) SD=PC12(AF6)
 *============================================================================*/
#define I2S3_WS_PIN_ENC    HAL_GPIO_PIN(0, 4)
#define I2S3_CK_PIN_ENC    HAL_GPIO_PIN(2, 10)
#define I2S3_SD_PIN_ENC    HAL_GPIO_PIN(2, 12)
#define I2S3_GPIO_AF       GPIO_AF6

/*============================================================================
 * Common (board-independent) helper functions
 *============================================================================*/
void USART_ClockEnable(USART_TypeDef *USARTx);
void DMA_ClockEnable(DMA_TypeDef *DMAx);
void AFIO_ClockEnable(void);

#endif /* __CONFIG_H */
