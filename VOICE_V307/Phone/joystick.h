#ifndef __JOYSTICK_H__
#define __JOYSTICK_H__

#include <stdint.h>


#define JOYSTICK_DEADZONE  500

void Joystick_Init(void);
int16_t Joystick_GetX(void);
int16_t Joystick_GetY(void);
uint8_t Joystick_GetButton(void);
void Joystick_Read(int16_t *x, int16_t *y, uint8_t *btn);

#endif