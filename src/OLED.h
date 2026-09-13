// OLED.h —— SSD1306 128x64 驱动接口（驱动实现来自 Mini-OLED，本文件补齐原型声明）
#ifndef __OLED_H
#define __OLED_H

#include "stm32f10x.h"

// 6x8 ASCII 字模（95 个可打印字符，来自 Mini-OLED）
extern const uint8_t OLED_F6x8[95][6];
// 显示缓存：8 页 × 128 列，每字节 8 个纵向像素（bit0 在上）
extern uint8_t OLED_Buffer[8][128];

void OLED_Init(void);
void OLED_Clear(void);
void OLED_Update(void);

void OLED_WriteCmd(uint8_t cmd);
void OLED_WriteData(uint8_t *data, uint8_t count);
void OLED_SetCursor(uint8_t x, uint8_t page);

void OLED_ShowChar(uint8_t x, uint8_t page, char ch);
void OLED_ShowString(uint8_t x, uint8_t page, const char *str);
void OLED_ShowNum(uint8_t x, uint8_t page, uint32_t num, uint8_t len);
void OLED_DrawPoint(uint8_t x, uint8_t y, uint8_t state);
void OLED_DrawLine(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2);
uint32_t OLED_Pow(uint32_t x, uint32_t y);

#endif
