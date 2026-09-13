// main.c —— egg-nano：串口收问题 → 片上 int8 推理 → OLED 显示回复
//
// 接线：USART1 PA9(TX)/PA10(RX) 接 USB 串口；OLED 软件 IIC：SCL=PA6 / SDA=PA5
// 流程：等一行输入（回车结束）→ 拼 prompt「问：<问题>\n答：」→ 贪心生成（≤60 字节，遇 0xFF 停）
//       → 边生成边刷 OLED（每个完整字符刷一次）→ 回复也逐字节打到串口
// 说明：prompt 只用最后 48 字节（与 PC 端训练/验收时 ctx[-48:] 完全一致）
//
// 串口时序（便于上位机自同步）：
//   回显(问题+CRLF) → "nano> " → 回复正文(逐字节) → CRLF + "[耗时 字数B]" + CRLF
//   → 诊断行 dbg ...（时钟 / prompt 原始字节 / 首字节 logits top3 / 输出十六进制）

#include <string.h>
#include "stm32f10x.h"
#include "nano_model.h"
#include "nano_uart.h"
#include "nano_ui.h"

#define MAX_NEW     60
#define REPLY_MAX   72
#define PROMPT_MAX  128

// 「问：」「\n答：」的 UTF-8 字节（写成字节数组，避免依赖源码字符集）
static const uint8_t PROMPT_ASK[] = { 0xE9, 0x97, 0xAE, 0xEF, 0xBC, 0x9A };
static const uint8_t PROMPT_ANS[] = { 0x0A, 0xE7, 0xAD, 0x94, 0xEF, 0xBC, 0x9A };

static char s_reply[REPLY_MAX];
static int  s_reply_len;
static uint8_t s_top3[3];          // 首字节 logits top-3（诊断）
static int  s_top3_n;
static uint8_t s_dbg_first;        // 首字节已取样

// 每生成一个字节回调：串口逐字流出，OLED 只在"完整字符"后刷屏（UTF-8 续字节 10xxxxxx 不刷）
static void on_byte(uint8_t b)
{
    if (!s_dbg_first)              // 第一次前向后立刻取 top-3（此时 logits 对应首字节）
    {
        s_top3_n = Nano_DebugTop(s_top3, 3);
        s_dbg_first = 1;
    }
    NanoUart_SendByte(b);                     // 串口同步流出（体感和 OLED 一致）
    if (s_reply_len < REPLY_MAX - 1)
    {
        s_reply[s_reply_len++] = (char)b;
        s_reply[s_reply_len] = 0;
        if ((b & 0xC0) != 0x80)
        {
            NanoUI_Answer(s_reply);
        }
    }
}

static uint32_t s_think_last;      // 思考动画时间门（每 300ms 推进一帧）
static uint8_t  s_think_phase;

// 生成期间的心跳（模型每位置回调一次，~20~40ms）：按时间门推进 ". .. ..." 动画
static void on_tick(void)
{
    uint32_t now = Nano_Millis();
    if (now - s_think_last < 300u) return;
    s_think_last = now;
    NanoUI_Thinking(s_think_phase);
    s_think_phase = (uint8_t)((s_think_phase + 1) % 3);
}

// 极简整数格式化（不用 printf，省 Flash）
static void fmt_status(char *buf, uint32_t ms, int bytes)
{
    char tmp[12];
    int i = 0, k = 0;

    buf[k++] = (char)('0' + (ms / 10000) % 10);
    buf[k++] = (char)('0' + (ms / 1000) % 10);
    buf[k++] = '.';
    buf[k++] = (char)('0' + (ms / 100) % 10);
    buf[k++] = 's';
    buf[k++] = ' ';

    if (bytes == 0) tmp[i++] = '0';
    while (bytes > 0) { tmp[i++] = (char)('0' + bytes % 10); bytes /= 10; }
    while (i > 0) buf[k++] = tmp[--i];
    buf[k++] = 'B';
    buf[k] = 0;
}

int main(void)
{
    char question[64];
    uint8_t prompt[PROMPT_MAX];
    uint8_t reply[MAX_NEW];
    char status[24];

    NanoUart_Init();
    NanoUI_Boot();                     // 页眉：左标识 egg-nano

    for (;;)
    {
        int qlen = NanoUart_ReadLine(question, sizeof(question));
        if (qlen <= 0)
        {
            __WFI();                       // 等下一个串口中断
            continue;
        }

        // prompt = 「问：」+ 问题 + 「\n答：」
        int plen = 0;
        memcpy(prompt, PROMPT_ASK, sizeof(PROMPT_ASK));
        plen += sizeof(PROMPT_ASK);
        for (int i = 0; i < qlen && plen < PROMPT_MAX - (int)sizeof(PROMPT_ANS); i++)
            prompt[plen++] = (uint8_t)question[i];
        memcpy(prompt + plen, PROMPT_ANS, sizeof(PROMPT_ANS));
        plen += sizeof(PROMPT_ANS);

        s_dbg_first = 0;
        s_top3_n = 0;
        s_reply_len = 0;
        s_reply[0] = 0;
        s_think_last = 0;            // 思考动画从头开始
        s_think_phase = 0;
        NanoUI_Header("egg-nano", 0);      // 清掉上一轮的状态
        NanoUI_Question(question);         // 正文上半：用户问题（带 "> " 前缀）

        uint32_t t0 = Nano_Millis();
        NanoUart_Send("nano> ");
        int n = Nano_Generate(prompt, plen, reply, MAX_NEW, on_byte, on_tick);
        uint32_t dt = Nano_Millis() - t0;
        reply[n] = 0;

        NanoUI_Answer(s_reply);
        fmt_status(status, dt, n);
        NanoUI_Header("egg-nano", status); // 页眉右侧：本轮时长 + 字节数

        NanoUart_Send("\r\n[");         // 回复正文已在 on_byte 里逐字流出
        NanoUart_Send(status);
        NanoUart_Send("]\r\n");

        // ---- 诊断行（都在状态行之后，不干扰上位机解析）----
        RCC_ClocksTypeDef clk;
        RCC_GetClocksFreq(&clk);
        NanoUart_Send("dbg clk sysclk=");
        NanoUart_SendNum(clk.SYSCLK_Frequency);
        NanoUart_Send(" hclk=");
        NanoUart_SendNum(clk.HCLK_Frequency);
        NanoUart_Send(" pclk2=");
        NanoUart_SendNum(clk.PCLK2_Frequency);
        NanoUart_Send("\r\ndbg prompt[");
        NanoUart_SendNum((uint32_t)plen);
        NanoUart_Send("]=");
        NanoUart_SendHex(prompt, plen);
        NanoUart_Send("\r\ndbg top3=");
        for (int i = 0; i < s_top3_n; i++)
        {
            NanoUart_SendNum(s_top3[i]);
            NanoUart_Send(" ");
        }
        NanoUart_Send("\r\ndbg hex=");
        NanoUart_SendHex(reply, n);
        NanoUart_Send("\r\n");
    }
}
