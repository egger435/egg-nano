// nano_ui.c —— OLED 排版（128x64）
//
// 版面：
//   行 0-7    页眉：左 "egg-nano"（6x8 ASCII），右 本轮时长+字节数 / 思考动画（右对齐）
//   行 12-59  正文 4 行 × 12px：
//               上半 = 用户问题（" > " 前缀 + 最多 2 行，超出截断）
//               下半 = 回答（占剩余行；回答行数超过可见行数时滚动显示最后几行）
//
// 中文用 12x12 子集字库（font_hz12，横向取模、bit15=最左像素），ASCII 用 6x8。
// 注意：12px 行不落在 SSD1306 页边界（一页 8 行）上，所以正文必须按绝对 y
// 逐点写帧缓冲；页眉正好占满第 0 页，可以直接用 OLED_ShowString。

#include <string.h>
#include "stm32f10x.h"
#include "OLED.h"
#include "font_hz12.h"
#include "nano_ui.h"

#define SCREEN_W     128
#define HEADER_PAGE  0            // 页眉 = 第 0 页（行 0-7）
#define TEXT_TOP     12           // 正文起始行
#define LINE_H       12           // 行距 = 字高
#define TEXT_LINES   4            // 正文可见行数（12*4 = 48 行，12..59）
#define MAX_Q_LINES  2            // 问题最多占几行
#define MAX_LINES    24           // 折行上限（问题≤63B、回答≤60B，最多约 20 行，够）
#define Q_PREFIX_PX  12           // "> " 前缀宽度 = 2 列 6x8
#define CJK_STEP     HZ12_W       // 13 = 12 格 + 1px 间隙
#define ASC_STEP     6

static uint8_t s_q_lines;         // 本轮问题占用行数（回答据此定位）

// UTF-8 解码：返回码点；尾部不完整（生成中的半个汉字）返回 0 且不推进
static uint16_t utf8_next(const char **p)
{
    const uint8_t *s = (const uint8_t *)*p;
    if (s[0] == 0) return 0;
    if (s[0] < 0x80) { *p += 1; return s[0]; }
    if ((s[0] & 0xE0) == 0xC0)
    {
        if (s[1] == 0) return 0;
        *p += 2;
        return (uint16_t)(((s[0] & 0x1F) << 6) | (s[1] & 0x3F));
    }
    if ((s[0] & 0xF0) == 0xE0)
    {
        if (s[1] == 0 || s[2] == 0) return 0;
        *p += 3;
        return (uint16_t)(((s[0] & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F));
    }
    *p += 1;                                        // 非法字节跳过
    return 0xFFFD;
}

static uint8_t glyph_w(uint16_t code)
{
    return (code < 0x80) ? ASC_STEP : CJK_STEP;
}

// ---- 帧缓冲逐点写入（页模型：每页 8 行，bit0 = 该页最上行）----
static void set_px(uint8_t x, uint8_t y)
{
    if (x < SCREEN_W && y < 64) OLED_Buffer[y >> 3][x] |= (uint8_t)(1u << (y & 7));
}

static void clear_rows(uint8_t y0, uint8_t y1)
{
    for (uint8_t y = y0; y < y1 && y < 64; y++)
        for (uint8_t x = 0; x < SCREEN_W; x++)
            OLED_Buffer[y >> 3][x] &= (uint8_t)~(1u << (y & 7));
}

// 只刷新一页（thinking 动画用：页眉一页 ~7ms，全屏 ~60ms，能省 90% I2C 开销）
static void flush_page(uint8_t page)
{
    OLED_SetCursor(0, page);
    OLED_WriteData(OLED_Buffer[page], 128);
}

// 12x12 汉字：横向取模，每行 2 字节、bit15 = 最左像素
static void draw_cjk(uint8_t x, uint8_t y, const uint8_t *bmp)
{
    for (uint8_t r = 0; r < HZ12_H; r++)
    {
        uint16_t bits = (uint16_t)(((uint16_t)bmp[2 * r] << 8) | bmp[2 * r + 1]);
        if (bits == 0) continue;
        for (uint8_t c = 0; c < HZ12_W; c++)
            if (bits & (0x8000u >> c)) set_px((uint8_t)(x + c), (uint8_t)(y + r));
    }
}

// 6x8 ASCII（字库纵向取模：每列 1 字节，bit0 在最上）
static void draw_ascii(uint8_t x, uint8_t y, uint16_t code)
{
    if (code < ' ' || code > '~') return;
    const uint8_t *f = OLED_F6x8[code - ' '];
    for (uint8_t c = 0; c < ASC_STEP; c++)
    {
        uint8_t col = f[c];
        for (uint8_t r = 0; r < 8; r++)
            if (col & (1u << r)) set_px((uint8_t)(x + c), (uint8_t)(y + r));
    }
}

// 缺字兜底：11x12 空心方框（留出间隙列，不与邻字相连）
static void draw_box(uint8_t x, uint8_t y)
{
    for (uint8_t c = 0; c < 11; c++)
    {
        set_px((uint8_t)(x + c), y);
        set_px((uint8_t)(x + c), (uint8_t)(y + HZ12_H - 1));
    }
    for (uint8_t r = 0; r < HZ12_H; r++)
    {
        set_px(x, (uint8_t)(y + r));
        set_px((uint8_t)(x + 10), (uint8_t)(y + r));
    }
}

// 画一个字：ASCII 画在 12px 行的上部，中文占满整行
static void draw_glyph(uint8_t x, uint8_t y, uint16_t code)
{
    if (code < 0x80) { draw_ascii(x, (uint8_t)(y + 2), code); return; }
    const uint8_t *bmp = Font_HZ12_Lookup(code);
    if (bmp) draw_cjk(x, y, bmp);
    else     draw_box(x, y);
}

// 按像素宽度折行：starts[] 记录每行起始指针（最多 cap 行），返回总行数。
// indent = 第一行左侧缩进（用于 "> " 前缀），后续行从 x=0 开始。
static int line_starts(const char *utf8, const char **starts, int cap, uint8_t indent)
{
    const char *p = utf8;
    int x = indent, n = 1;
    starts[0] = utf8;
    while (*p)
    {
        const char *q = p;
        uint16_t code = utf8_next(&q);
        if (code == 0) break;                        // 尾部半个汉字 → 到此为止
        uint8_t w = glyph_w(code);
        if (x + w > SCREEN_W)                        // 折行
        {
            x = 0;
            if (n < cap) starts[n] = p;
            n++;
        }
        x += w;
        p = q;
    }
    return n;
}

// 画一行文本（从 x0 开始，超宽截断）
static void draw_line(uint8_t y, const char *s, uint8_t x0)
{
    uint8_t cx = x0;
    while (*s)
    {
        const char *q = s;
        uint16_t code = utf8_next(&q);
        if (code == 0) break;
        uint8_t w = glyph_w(code);
        if (cx + w > SCREEN_W) break;
        draw_glyph(cx, y, code);
        cx += w;
        s = q;
    }
}

void NanoUI_Boot(void)
{
    OLED_Init();
    OLED_Clear();
    s_q_lines = 0;
    NanoUI_Header("egg-nano", "boot");
}

void NanoUI_Header(const char *logo, const char *right)
{
    uint8_t x;

    for (x = 0; x < SCREEN_W; x++) OLED_Buffer[HEADER_PAGE][x] = 0x00;
    if (logo) OLED_ShowString(0, HEADER_PAGE, (char *)logo);

    if (right)
    {
        int len = (int)strlen(right);
        int minx = logo ? (int)(strlen(logo) + 1) * ASC_STEP : 0;
        int rx = SCREEN_W - len * ASC_STEP;
        if (rx < minx) rx = minx;                    // 不与标识打架
        if (rx > SCREEN_W - len * ASC_STEP) rx = SCREEN_W - len * ASC_STEP;
        OLED_ShowString((uint8_t)rx, HEADER_PAGE, (char *)right);
    }
    OLED_Update();
}

// 思考动画：页眉右侧画 1~3 个点（phase 取模），只刷页眉一页（~7ms）
void NanoUI_Thinking(uint8_t phase)
{
    uint8_t n = (uint8_t)(phase % 3) + 1;
    uint8_t x = (uint8_t)(SCREEN_W - n * ASC_STEP);
    uint8_t i;

    for (i = 0; i < SCREEN_W; i++) OLED_Buffer[HEADER_PAGE][i] = 0x00;
    OLED_ShowString(0, HEADER_PAGE, "egg-nano");
    for (i = 0; i < n; i++)
        draw_ascii((uint8_t)(x + i * ASC_STEP), HEADER_PAGE * 8, '.');
    flush_page(HEADER_PAGE);
}

void NanoUI_Question(const char *utf8)
{
    const char *starts[MAX_LINES];
    int n;

    clear_rows(TEXT_TOP, 64);                        // 正文整块重画
    if (utf8[0] == 0)                                // 空输入：只留前缀
    {
        draw_ascii(0, TEXT_TOP, '>');
        s_q_lines = 0;
        OLED_Update();
        return;
    }
    draw_ascii(0, TEXT_TOP, '>');                    // "> " 前缀（12px 缩进）
    n = line_starts(utf8, starts, MAX_LINES, Q_PREFIX_PX);
    if (n > MAX_Q_LINES) n = MAX_Q_LINES;            // 问题占满上限就截断
    s_q_lines = (uint8_t)n;
    for (int i = 0; i < n; i++)
        draw_line((uint8_t)(TEXT_TOP + i * LINE_H), starts[i],
                  (i == 0) ? Q_PREFIX_PX : 0);
    OLED_Update();
}

void NanoUI_Answer(const char *utf8)
{
    const char *starts[MAX_LINES];
    int n = line_starts(utf8, starts, MAX_LINES, 0);
    int cap = TEXT_LINES - s_q_lines - 1;          // 问题和回答之间空一行
    int y0 = TEXT_TOP + (s_q_lines + 1) * LINE_H;
    int first = (n > cap) ? (n - cap) : 0;         // 行数超了就滚到最后 cap 行

    if (cap < 1) cap = 1;
    clear_rows((uint8_t)y0, 64);
    for (int i = first; i < n; i++)
        draw_line((uint8_t)(y0 + (i - first) * LINE_H), starts[i], 0);
    OLED_Update();
}
