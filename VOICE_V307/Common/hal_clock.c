#include "hal_clock.h"

/*============================================================================
 * 统一外设时钟层 —— 实现
 *
 * 重构来源（只搬不改语义）：
 *   GPIO_ClockEnable()  <- 原 Common/hal_gpio.c
 *   SPI_ClockEnable()   <- 原 Common/Config.c
 *   DMA_ClockEnable()   <- 原 Common/Config.c
 *   AFIO_ClockEnable()  <- 原 Common/Config.c
 *
 * RCC 宏取值已对着 Peripheral/inc/ch32v30x_rcc.h 逐条核对：
 *   APB2: AFIO=0x001 GPIOA=0x004 GPIOB=0x008 GPIOC=0x010 GPIOD=0x020 GPIOE=0x040
 *         SPI1=0x1000 USART1=0x4000
 *   APB1: SPI2=0x4000 SPI3=0x8000 USART2=0x20000 USART3=0x40000
 *         UART4=0x80000 UART5=0x100000 UART6=0x40 UART7=0x80 UART8=0x100
 *   AHB : DMA1=0x1 DMA2=0x2 SDIO=0x400 USBHS=0x800 USBFS=0x1000
 *============================================================================*/

typedef enum {
    CLK_BUS_APB1 = 0,
    CLK_BUS_APB2,
    CLK_BUS_AHB
} clk_bus_t;

typedef struct {
    uint8_t  bus;      /* clk_bus_t */
    uint32_t mask;     /* RCC_xxxPeriph_yyy */
} clk_entry_t;

/* 下标必须与 Hal_Clk_t 一一对应 */
static const clk_entry_t s_clk_map[HAL_CLK_COUNT] = {
    [HAL_CLK_AFIO]    = { CLK_BUS_APB2, RCC_APB2Periph_AFIO   },

    [HAL_CLK_GPIOA]   = { CLK_BUS_APB2, RCC_APB2Periph_GPIOA  },
    [HAL_CLK_GPIOB]   = { CLK_BUS_APB2, RCC_APB2Periph_GPIOB  },
    [HAL_CLK_GPIOC]   = { CLK_BUS_APB2, RCC_APB2Periph_GPIOC  },
    [HAL_CLK_GPIOD]   = { CLK_BUS_APB2, RCC_APB2Periph_GPIOD  },
    [HAL_CLK_GPIOE]   = { CLK_BUS_APB2, RCC_APB2Periph_GPIOE  },

    [HAL_CLK_SPI1]    = { CLK_BUS_APB2, RCC_APB2Periph_SPI1   },
    [HAL_CLK_SPI2]    = { CLK_BUS_APB1, RCC_APB1Periph_SPI2   },
    [HAL_CLK_SPI3]    = { CLK_BUS_APB1, RCC_APB1Periph_SPI3   },

    [HAL_CLK_DMA1]    = { CLK_BUS_AHB,  RCC_AHBPeriph_DMA1    },
    [HAL_CLK_DMA2]    = { CLK_BUS_AHB,  RCC_AHBPeriph_DMA2    },

    [HAL_CLK_USART1]  = { CLK_BUS_APB2, RCC_APB2Periph_USART1 },
    [HAL_CLK_USART2]  = { CLK_BUS_APB1, RCC_APB1Periph_USART2 },
    [HAL_CLK_USART3]  = { CLK_BUS_APB1, RCC_APB1Periph_USART3 },
    [HAL_CLK_UART4]   = { CLK_BUS_APB1, RCC_APB1Periph_UART4  },
    [HAL_CLK_UART5]   = { CLK_BUS_APB1, RCC_APB1Periph_UART5  },
    [HAL_CLK_UART6]   = { CLK_BUS_APB1, RCC_APB1Periph_UART6  },
    [HAL_CLK_UART7]   = { CLK_BUS_APB1, RCC_APB1Periph_UART7  },
    [HAL_CLK_UART8]   = { CLK_BUS_APB1, RCC_APB1Periph_UART8  },

    [HAL_CLK_SDIO]    = { CLK_BUS_AHB,  RCC_AHBPeriph_SDIO    },
    [HAL_CLK_USBFS]   = { CLK_BUS_AHB,  RCC_AHBPeriph_USBFS   },
    [HAL_CLK_USBHS]   = { CLK_BUS_AHB,  RCC_AHBPeriph_USBHS   },
};

static void Clock_Cmd(const clk_entry_t *e, FunctionalState state)
{
    switch (e->bus) {
    case CLK_BUS_APB2: RCC_APB2PeriphClockCmd(e->mask, state); break;
    case CLK_BUS_APB1: RCC_APB1PeriphClockCmd(e->mask, state); break;
    case CLK_BUS_AHB:  RCC_AHBPeriphClockCmd (e->mask, state); break;
    default: break;
    }
}

void Hal_Clock_Enable(Hal_Clk_t id)
{
    if (id >= HAL_CLK_COUNT) return;
    Clock_Cmd(&s_clk_map[id], ENABLE);
}

void Hal_Clock_Disable(Hal_Clk_t id)
{
    if (id >= HAL_CLK_COUNT) return;
    Clock_Cmd(&s_clk_map[id], DISABLE);
}

/*============================================================================
 * 兼容层
 *   GPIO_ClockEnable 依赖 GPIOA~GPIOE 在 Hal_Clk_t 中连续排列。
 *   下面这行 typedef 就是那个前提的编译期校验：若两者不等会直接编译失败。
 *============================================================================*/
typedef char hal_clk_port_range_check[
    (HAL_CLK_GPIOE - HAL_CLK_GPIOA + 1 == HAL_GPIO_PORT_COUNT) ? 1 : -1 ];

void GPIO_ClockEnable(uint8_t port_index)
{
    if (port_index < HAL_GPIO_PORT_COUNT) {
        Hal_Clock_Enable((Hal_Clk_t)(HAL_CLK_GPIOA + port_index));
    }
}

void SPI_ClockEnable(SPI_TypeDef *SPIx)
{
    if      (SPIx == SPI1) Hal_Clock_Enable(HAL_CLK_SPI1);
    else if (SPIx == SPI2) Hal_Clock_Enable(HAL_CLK_SPI2);
    else if (SPIx == SPI3) Hal_Clock_Enable(HAL_CLK_SPI3);
}

void DMA_ClockEnable(DMA_TypeDef *DMAx)
{
    if      (DMAx == DMA1) Hal_Clock_Enable(HAL_CLK_DMA1);
    else if (DMAx == DMA2) Hal_Clock_Enable(HAL_CLK_DMA2);
}

void AFIO_ClockEnable(void)
{
    Hal_Clock_Enable(HAL_CLK_AFIO);
}
