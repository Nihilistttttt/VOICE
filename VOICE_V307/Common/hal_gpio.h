#ifndef HAL_GPIO_H
#define HAL_GPIO_H

#include <stdint.h>
#include <stdbool.h>
#include "ch32v30x.h"      /* SPI_TypeDef / DMA_TypeDef：下面四个时钟包装函数的参数类型 */

#define HAL_GPIO_PIN(port, pin)    (((port) << 16) | (pin))
#define HAL_GPIO_PORT(pin_enc)     ((pin_enc) >> 16)
#define HAL_GPIO_PIN_NUM(pin_enc)  ((pin_enc) & 0xFFFF)

typedef enum {
    HAL_GPIO_MODE_INPUT,
    HAL_GPIO_MODE_INPUT_PU,
    HAL_GPIO_MODE_INPUT_PD,
    HAL_GPIO_MODE_OUTPUT_PP,
    HAL_GPIO_MODE_OUTPUT_OD,
    HAL_GPIO_MODE_AF_PP,
    HAL_GPIO_MODE_AF_OD,
    HAL_GPIO_MODE_AF_INPUT
} Hal_GPIO_Mode_t;

typedef enum {
    HAL_GPIO_SPEED_LOW,
    HAL_GPIO_SPEED_MEDIUM,
    HAL_GPIO_SPEED_HIGH,
    HAL_GPIO_SPEED_VERY_HIGH
} Hal_GPIO_Speed_t;

typedef enum {
    HAL_GPIO_IRQ_RISING,
    HAL_GPIO_IRQ_FALLING,
    HAL_GPIO_IRQ_BOTH
} Hal_GPIO_IRQ_Trigger_t;

/* ---- 外设时钟使能 ------------------------------------------------------
 * GPIO_ClockEnable 声明与实现都在本文件（与 GPIO 操作放一起）。
 * SPI/DMA/AFIO_ClockEnable 声明在 Config.h，实现在 Config.c。
 *
 * ⚠ Hal_GPIO_Init 不自动开时钟 —— 用前必须先使能对应端口时钟。 */
void GPIO_ClockEnable(uint8_t port_index);

void Hal_GPIO_Init(uint32_t pin_enc, Hal_GPIO_Mode_t mode,
                   Hal_GPIO_Speed_t speed, uint8_t af_num);

void Hal_GPIO_Write(uint32_t pin_enc, uint8_t level);
void Hal_GPIO_Set(uint32_t pin_enc);
void Hal_GPIO_Reset(uint32_t pin_enc);
void Hal_GPIO_Toggle(uint32_t pin_enc);
uint8_t Hal_GPIO_Read(uint32_t pin_enc);

void Hal_GPIO_IRQ_Config(uint32_t pin_enc, Hal_GPIO_IRQ_Trigger_t trigger,
                         uint8_t preempt_priority, uint8_t sub_priority,
                         void (*callback)(void));
void Hal_GPIO_IRQ_Enable(uint32_t pin_enc, bool enable);
void Hal_GPIO_IRQ_Disable(uint32_t pin_enc);
void Hal_GPIO_IRQ_ClearPending(uint32_t pin_enc);

#endif /* HAL_GPIO_H */