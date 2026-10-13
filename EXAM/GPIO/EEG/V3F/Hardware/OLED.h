#ifndef __OLED_H
#define __OLED_H

typedef enum {
    I2C = 0,
    SPI
} OLED_Type;

void OLED_Init(void);
void OLED_Clear(OLED_Type Type);
void OLED_Clear_All(void);
void OLED_UpdateFull(OLED_Type Type);
void OLED_ShowChar(OLED_Type Type, uint8_t Line, uint8_t Column, char Char);
void OLED_ShowString(OLED_Type Type, uint8_t Line, uint8_t Column, char *String);
void OLED_ShowStringRight(OLED_Type Type, uint8_t Line, char *String);
void OLED_ShowNum(OLED_Type Type, uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length);
void OLED_ShowSignedNum(OLED_Type Type, uint8_t Line, uint8_t Column, int32_t Number, uint8_t Length);
void OLED_ShowHexNum(OLED_Type Type, uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length);
void OLED_ShowBinNum(OLED_Type Type, uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length);
void OLED_ClearArea(OLED_Type Type, uint8_t start_page, uint8_t end_page);
void OLED_DrawSpectrum(OLED_Type Type, uint8_t x_start, uint8_t *heights, uint8_t num_bins, uint8_t max_height);
void OLED_SetCol(OLED_Type Type, uint8_t col);
void OLED_DrawWaveformFast(OLED_Type Type, uint8_t *y_values);

#endif