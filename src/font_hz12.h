// 12x12 中文子集点阵字库（303 字，7272 字节）
#ifndef FONT_HZ12_H
#define FONT_HZ12_H

#include <stdint.h>

#define HZ12_W      13      // 上屏水平步进（字形格 12 + 间隙）
#define HZ12_H      12
#define HZ12_COUNT  303

// 码点表
extern const uint16_t font_hz12_codes[HZ12_COUNT];
// 点阵表
extern const uint8_t  font_hz12_bitmaps[HZ12_COUNT][24];

// 查字模
const uint8_t *Font_HZ12_Lookup(uint16_t code);

#endif
