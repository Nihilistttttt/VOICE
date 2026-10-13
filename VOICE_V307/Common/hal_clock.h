#ifndef HAL_CLOCK_H
#define HAL_CLOCK_H

#include <stdint.h>
#include <stdbool.h>
#include "ch32v30x.h"

/*============================================================================
 * 统一外设时钟层  Hal/Clock
 *
 * 本文件是**所有外设时钟使能的唯一入口**。
 *
 * 【为什么要有这一层】
 *   重构前时钟 API 被拆在两处：
 *       GPIO 时钟     -> hal_gpio.c   (GPIO_ClockEnable)
 *       SPI/DMA/AFIO  -> Config.c     (SPI_ClockEnable / DMA_ClockEnable / AFIO_ClockEnable)
 *   而 Hal_GPIO_Init() 并不自动开时钟，它也无法顺手调用另一个文件里的函数，
 *   于是每个初始化点都要手写 5~10 行 ClockEnable（mic_hum.c:469-478 是典型）。
 *   漏写一行的表现是「引脚完全没反应，但编译没有任何警告」——极难排查。
 *
 *   收拢到本文件后：新增一种外设时钟只需改 s_clk_map[] 一处。
 *   原先承载 SPI/DMA/AFIO 三个函数的 Config.c 已随本次重构删除。
 *
 * 【用法】
 *   推荐（新代码）：
 *       Hal_Clock_Enable(HAL_CLK_GPIOB);
 *       Hal_Clock_Enable(HAL_CLK_SPI2);
 *       Hal_Clock_Disable(HAL_CLK_SDIO);      // 低功耗时关掉不用的外设
 *
 *   兼容层（保留给既有调用点，签名未变，不要在新代码里用）：
 *       GPIO_ClockEnable(uint8_t port_index)
 *       SPI_ClockEnable(SPI_TypeDef *SPIx)
 *       DMA_ClockEnable(DMA_TypeDef *DMAx)
 *       AFIO_ClockEnable(void)
 *============================================================================*/

/*============================================================================
 * 时钟单元编号
 *   **顺序即 s_clk_map[] 的下标，不要在中间插入或重排**，新增只能往后加。
 *   GPIOA~GPIOE 必须连续：GPIO_ClockEnable() 依赖 (HAL_CLK_GPIOA + port_index)。
 *============================================================================*/
typedef enum {
    HAL_CLK_AFIO = 0,

    HAL_CLK_GPIOA, HAL_CLK_GPIOB, HAL_CLK_GPIOC, HAL_CLK_GPIOD, HAL_CLK_GPIOE,

    HAL_CLK_SPI1, HAL_CLK_SPI2, HAL_CLK_SPI3,

    HAL_CLK_DMA1, HAL_CLK_DMA2,

    HAL_CLK_USART1, HAL_CLK_USART2, HAL_CLK_USART3,
    HAL_CLK_UART4, HAL_CLK_UART5, HAL_CLK_UART6, HAL_CLK_UART7, HAL_CLK_UART8,

    HAL_CLK_SDIO,
    HAL_CLK_USBFS,
    HAL_CLK_USBHS,

    HAL_CLK_COUNT
} Hal_Clk_t;

void Hal_Clock_Enable(Hal_Clk_t id);
void Hal_Clock_Disable(Hal_Clk_t id);

/*============================================================================
 * V307 端口数量：A~E 共 5 个，无 GPIOF。
 * hal_gpio.c 会用它做编译期一致性校验，两处必须相等。
 *============================================================================*/
#define HAL_GPIO_PORT_COUNT     5

/*============================================================================
 * 兼容层 —— 委托给 Hal_Clock_Enable()，签名与重构前完全一致
 *============================================================================*/
void GPIO_ClockEnable(uint8_t port_index);
void SPI_ClockEnable(SPI_TypeDef *SPIx);
void DMA_ClockEnable(DMA_TypeDef *DMAx);
void AFIO_ClockEnable(void);

#endif /* HAL_CLOCK_H */
