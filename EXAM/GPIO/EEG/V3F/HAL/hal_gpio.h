#ifndef HAL_GPIO_H
#define HAL_GPIO_H

#include <stdint.h>
#include <stdbool.h>

/* 引脚编码：高8位端口索引(0=A,1=B,2=C,3=D,4=E,5=F)，低16位引脚号(0-15) */
#define HAL_GPIO_PIN(port, pin)  (((port) << 16) | (pin))
#define HAL_GPIO_PORT(pin_enc)   ((pin_enc) >> 16)
#define HAL_GPIO_PIN_NUM(pin_enc) ((pin_enc) & 0xFFFF)

typedef enum {
    HAL_GPIO_MODE_INPUT,        // 浮空输入
    HAL_GPIO_MODE_INPUT_PU,     // 上拉输入
    HAL_GPIO_MODE_INPUT_PD,     // 下拉输入
    HAL_GPIO_MODE_OUTPUT_PP,    // 推挽输出
    HAL_GPIO_MODE_OUTPUT_OD,    // 开漏输出
    HAL_GPIO_MODE_AF_PP,        // 复用推挽
    HAL_GPIO_MODE_AF_OD,        // 复用开漏
    HAL_GPIO_MODE_AF_INPUT      // 复用输入（如 SPI_MISO）
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

/* 时钟使能 */
void GPIO_ClockEnable(uint8_t port_index);

/* 引脚初始化 */
void Hal_GPIO_Init(uint32_t pin_enc, Hal_GPIO_Mode_t mode,
                   Hal_GPIO_Speed_t speed, uint8_t af_num);

/* 读写操作 */
void Hal_GPIO_Write(uint32_t pin_enc, uint8_t level);
void Hal_GPIO_Set(uint32_t pin_enc);
void Hal_GPIO_Reset(uint32_t pin_enc);
void Hal_GPIO_Toggle(uint32_t pin_enc);
uint8_t Hal_GPIO_Read(uint32_t pin_enc);

/* 中断配置 */
void Hal_GPIO_IRQ_Config(uint32_t pin_enc, Hal_GPIO_IRQ_Trigger_t trigger,
                         uint8_t preempt_priority, uint8_t sub_priority,
                         void (*callback)(void));
void Hal_GPIO_IRQ_Enable(uint32_t pin_enc, bool enable);
void Hal_GPIO_IRQ_Disable(uint32_t pin_enc);
void Hal_GPIO_IRQ_ClearPending(uint32_t pin_enc);

#endif /* HAL_GPIO_H */