#include "hal_gpio.h"
#include "ch32v30x.h"

static GPIO_TypeDef *const s_gpio_ports[] = {
    GPIOA, GPIOB, GPIOC, GPIOD, GPIOE
};
#define NUM_PORTS (sizeof(s_gpio_ports) / sizeof(s_gpio_ports[0]))

static const uint32_t s_gpio_rcc[] = {
    RCC_APB2Periph_GPIOA, RCC_APB2Periph_GPIOB, RCC_APB2Periph_GPIOC,
    RCC_APB2Periph_GPIOD, RCC_APB2Periph_GPIOE
};

static void (*s_exti_callback[16])(void) = {NULL};

static inline uint8_t get_pin_num(uint32_t enc) { return (uint8_t)(enc & 0xFFFF); }
static inline uint8_t get_port_idx(uint32_t enc) { return (uint8_t)((enc >> 16) & 0xFF); }

void GPIO_ClockEnable(uint8_t port_index)
{
    if (port_index < NUM_PORTS) {
        RCC_APB2PeriphClockCmd(s_gpio_rcc[port_index], ENABLE);
    }
}

void Hal_GPIO_Init(uint32_t pin_enc, Hal_GPIO_Mode_t mode,
                   Hal_GPIO_Speed_t speed, uint8_t af_num)
{
    uint8_t port_idx = get_port_idx(pin_enc);
    uint8_t pin = get_pin_num(pin_enc);
    if (port_idx >= NUM_PORTS) return;

    GPIO_TypeDef *gpio = s_gpio_ports[port_idx];
    uint16_t pin_mask = 1UL << pin;
    GPIO_InitTypeDef init = {0};

    init.GPIO_Pin = pin_mask;
    init.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    init.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(gpio, &init);

    if (mode == HAL_GPIO_MODE_AF_PP || mode == HAL_GPIO_MODE_AF_OD || mode == HAL_GPIO_MODE_AF_INPUT) {

    }

    switch (mode) {
        case HAL_GPIO_MODE_INPUT:      init.GPIO_Mode = GPIO_Mode_IN_FLOATING; break;
        case HAL_GPIO_MODE_INPUT_PU:   init.GPIO_Mode = GPIO_Mode_IPU; break;
        case HAL_GPIO_MODE_INPUT_PD:   init.GPIO_Mode = GPIO_Mode_IPD; break;
        case HAL_GPIO_MODE_OUTPUT_PP:  init.GPIO_Mode = GPIO_Mode_Out_PP; break;
        case HAL_GPIO_MODE_OUTPUT_OD:  init.GPIO_Mode = GPIO_Mode_Out_OD; break;
        case HAL_GPIO_MODE_AF_PP:      init.GPIO_Mode = GPIO_Mode_AF_PP; break;
        case HAL_GPIO_MODE_AF_OD:      init.GPIO_Mode = GPIO_Mode_AF_OD; break;
        case HAL_GPIO_MODE_AF_INPUT:   init.GPIO_Mode = GPIO_Mode_IN_FLOATING; break;
    }

    switch (speed) {
        case HAL_GPIO_SPEED_LOW:        init.GPIO_Speed = GPIO_Speed_10MHz; break;
        case HAL_GPIO_SPEED_MEDIUM:     init.GPIO_Speed = GPIO_Speed_2MHz; break;
        case HAL_GPIO_SPEED_HIGH:       init.GPIO_Speed = GPIO_Speed_50MHz; break;
        default:                        init.GPIO_Speed = GPIO_Speed_50MHz; break;
    }

    GPIO_Init(gpio, &init);
}

void Hal_GPIO_Write(uint32_t pin_enc, uint8_t level)
{
    uint8_t port_idx = get_port_idx(pin_enc);
    uint8_t pin = get_pin_num(pin_enc);
    if (port_idx >= NUM_PORTS) return;
    uint16_t mask = 1UL << pin;
    if (level) {
        GPIO_SetBits(s_gpio_ports[port_idx], mask);
    } else {
        GPIO_ResetBits(s_gpio_ports[port_idx], mask);
    }
}

void Hal_GPIO_Set(uint32_t pin_enc) { Hal_GPIO_Write(pin_enc, 1); }
void Hal_GPIO_Reset(uint32_t pin_enc) { Hal_GPIO_Write(pin_enc, 0); }

void Hal_GPIO_Toggle(uint32_t pin_enc)
{
    uint8_t port_idx = get_port_idx(pin_enc);
    uint8_t pin = get_pin_num(pin_enc);
    if (port_idx >= NUM_PORTS) return;
    uint16_t mask = 1UL << pin;
    uint8_t current = (GPIO_ReadOutputDataBit(s_gpio_ports[port_idx], mask) != Bit_RESET);
    if (current) {
        GPIO_ResetBits(s_gpio_ports[port_idx], mask);
    } else {
        GPIO_SetBits(s_gpio_ports[port_idx], mask);
    }
}

uint8_t Hal_GPIO_Read(uint32_t pin_enc)
{
    uint8_t port_idx = get_port_idx(pin_enc);
    uint8_t pin = get_pin_num(pin_enc);
    if (port_idx >= NUM_PORTS) return 0;
    return (GPIO_ReadInputDataBit(s_gpio_ports[port_idx], 1UL << pin) != Bit_RESET) ? 1 : 0;
}

void Hal_GPIO_IRQ_Config(uint32_t pin_enc, Hal_GPIO_IRQ_Trigger_t trigger,
                         uint8_t preempt_priority, uint8_t sub_priority,
                         void (*callback)(void))
{
    uint8_t pin = get_pin_num(pin_enc);
    if (pin > 15) return;

    s_exti_callback[pin] = callback;
    GPIO_EXTILineConfig(get_port_idx(pin_enc), pin);

    EXTI_InitTypeDef exti_init = {0};
    exti_init.EXTI_Line = 1UL << pin;
    exti_init.EXTI_Mode = EXTI_Mode_Interrupt;
    exti_init.EXTI_LineCmd = ENABLE;

    switch (trigger) {
        case HAL_GPIO_IRQ_RISING:  exti_init.EXTI_Trigger = EXTI_Trigger_Rising; break;
        case HAL_GPIO_IRQ_FALLING: exti_init.EXTI_Trigger = EXTI_Trigger_Falling; break;
        case HAL_GPIO_IRQ_BOTH:    exti_init.EXTI_Trigger = EXTI_Trigger_Rising_Falling; break;
    }
    EXTI_Init(&exti_init);
    EXTI_ClearITPendingBit(1UL << pin);

    IRQn_Type irq_num;
    if (pin <= 4)       irq_num = (IRQn_Type)(EXTI0_IRQn + pin);
    else if (pin <= 9)  irq_num = EXTI9_5_IRQn;
    else                irq_num = EXTI15_10_IRQn;

    NVIC_SetPriority(irq_num, (preempt_priority << 7) | (sub_priority << 4));
    NVIC_EnableIRQ(irq_num);
}

void Hal_GPIO_IRQ_Enable(uint32_t pin_enc, bool enable)
{
    uint8_t pin = get_pin_num(pin_enc);
    if (pin > 15) return;
    uint32_t line = 1UL << pin;
    if (enable) {
        EXTI->INTENR |= line;
    } else {
        EXTI->INTENR &= ~line;
    }
}

void Hal_GPIO_IRQ_Disable(uint32_t pin_enc) { Hal_GPIO_IRQ_Enable(pin_enc, false); }

void Hal_GPIO_IRQ_ClearPending(uint32_t pin_enc)
{
    uint8_t pin = get_pin_num(pin_enc);
    if (pin <= 15) {
        EXTI_ClearITPendingBit(1UL << pin);
    }
}

void EXTI0_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void EXTI0_IRQHandler(void)
{
    if (s_exti_callback[0]) s_exti_callback[0]();
    EXTI_ClearITPendingBit(EXTI_Line0);
}

void EXTI1_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void EXTI1_IRQHandler(void)
{
    if (s_exti_callback[1]) s_exti_callback[1]();
    EXTI_ClearITPendingBit(EXTI_Line1);
}

void EXTI2_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void EXTI2_IRQHandler(void)
{
    if (s_exti_callback[2]) s_exti_callback[2]();
    EXTI_ClearITPendingBit(EXTI_Line2);
}

void EXTI3_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void EXTI3_IRQHandler(void)
{
    if (s_exti_callback[3]) s_exti_callback[3]();
    EXTI_ClearITPendingBit(EXTI_Line3);
}

void EXTI4_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void EXTI4_IRQHandler(void)
{
    if (s_exti_callback[4]) s_exti_callback[4]();
    EXTI_ClearITPendingBit(EXTI_Line4);
}

void EXTI9_5_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void EXTI9_5_IRQHandler(void)
{
    uint32_t pending = (EXTI->INTFR >> 5) & 0x1F;
    uint32_t enabled = (EXTI->INTENR >> 5) & 0x1F;
    uint32_t active = pending & enabled;
    for (int i = 5; i <= 9; i++) {
        if (active & (1UL << (i - 5))) {
            if (s_exti_callback[i]) s_exti_callback[i]();
            EXTI_ClearITPendingBit(1UL << i);
        }
    }
}

void EXTI15_10_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void EXTI15_10_IRQHandler(void)
{
    uint32_t pending = (EXTI->INTFR >> 10) & 0x3F;
    uint32_t enabled = (EXTI->INTENR >> 10) & 0x3F;
    uint32_t active = pending & enabled;
    for (int i = 10; i <= 15; i++) {
        if (active & (1UL << (i - 10))) {
            if (s_exti_callback[i]) s_exti_callback[i]();
            EXTI_ClearITPendingBit(1UL << i);
        }
    }
}