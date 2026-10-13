#include "joystick.h"
#include "ads1115.h"
#include "Config.h"
#include "debug.h"

void Joystick_Init(void)
{
    ADS1115_Init();

    GPIO_ClockEnable(HAL_GPIO_PORT(JOYSTICK_KEY_PIN_ENC));
    Hal_GPIO_Init(JOYSTICK_KEY_PIN_ENC, HAL_GPIO_MODE_INPUT_PU, HAL_GPIO_SPEED_HIGH, 0);
}

int16_t Joystick_GetX(void)
{
    return ADS1115_ReadChannel(JOYSTICK_X_CHANNEL);
}

int16_t Joystick_GetY(void)
{
    return ADS1115_ReadChannel(JOYSTICK_Y_CHANNEL);
}

uint8_t Joystick_GetButton(void)
{
    return Hal_GPIO_Read(JOYSTICK_KEY_PIN_ENC);
}

void Joystick_Read(int16_t *x, int16_t *y, uint8_t *btn)
{
    if (x) *x = Joystick_GetX();
    if (y) *y = Joystick_GetY();
    if (btn) *btn = Joystick_GetButton();
}
