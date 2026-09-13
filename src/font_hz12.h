// font_hz12.h —— 由 scripts/nano/make_oled_font.py 自动生成，勿手改！
// 12x12 中文子集点阵字库（303 字，7272 字节）
// 字符集 = SFT 语料答区 ∪ 实测生成输出 ∪ 常用标点
// 布局：横向取模：每行 2 字节、高位在前（bit15=最左像素），上屏时按绝对 y 逐点写入
#ifndef FONT_HZ12_H
#define FONT_HZ12_H

#include <stdint.h>

#define HZ12_W      13      // 上屏水平步进（字形格 12 + 间隙）
#define HZ12_H      12
#define HZ12_COUNT  303

// 码点表（升序，供二分查找）
extern const uint16_t font_hz12_codes[HZ12_COUNT];
// 点阵表：每字 24 字节
extern const uint8_t  font_hz12_bitmaps[HZ12_COUNT][24];

// 查字模；未收录返回 0（调用方画 □ 兜底）
const uint8_t *Font_HZ12_Lookup(uint16_t code);

#endif // FONT_HZ12_H
