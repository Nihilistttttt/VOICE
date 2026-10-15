#ifndef __OLED_H
#define __OLED_H

#include <stdint.h>

void OLED_Init(void);
void OLED_Clear(void);
void OLED_Refresh(void);
void OLED_SetPixel(uint8_t x, uint8_t y, uint8_t on);
void OLED_ShowChar(uint8_t Line, uint8_t Column, char Char);
void OLED_ShowString(uint8_t Line, uint8_t Column, const char *String);
void OLED_ShowNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length);
void OLED_DrawWaveSplit(const uint16_t *data_a, const uint16_t *data_b,
                        uint8_t head, uint16_t scale,
                        uint8_t y_top, uint8_t height);

#endif