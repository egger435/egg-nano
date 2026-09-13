// nano_ui.h —— OLED 排版（128x64）
//
// 版面：
//   行 0-7    页眉：左 "egg-nano" 标识，右 本轮时长与字节数（右对齐）
//   行 12-59  正文 4 行 × 12px：先用户问题（最多 2 行），下面接回答
#ifndef NANO_UI_H
#define NANO_UI_H

#include <stdint.h>

// 初始化屏幕 + 画开机页眉
void NanoUI_Boot(void);

// 页眉：左侧标识 logo，右侧状态 right（NULL = 只清掉右侧，不画）
void NanoUI_Header(const char *logo, const char *right);

// 思考动画：页眉右侧画 1~3 个点（phase 依次 0,1,2,...），只刷页眉一页
void NanoUI_Thinking(uint8_t phase);

// 正文上半：显示用户问题（" > " 前缀，超过 2 行截断）。会先清空整个正文区。
void NanoUI_Question(const char *utf8);

// 正文下半：显示回答（占用剩余行；行数不够时滚动显示最后几行）。
// 生成过程中每出一个完整字符调用一次即可。
void NanoUI_Answer(const char *utf8);

#endif // NANO_UI_H
