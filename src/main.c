// main.c —— egg-nano：串口收问题, 片上 int8 推理, OLED 显示回复
// 接线：USART1 PA9(TX)/PA10(RX) 接 USB 串口；OLED 软件 IIC：SCL=PA6 / SDA=PA5

#include <string.h>
#include "stm32f10x.h"
#include "nano_model.h"
#include "nano_uart.h"
#include "nano_ui.h"

#define MAX_NEW     60
#define REPLY_MAX   72
#define PROMPT_MAX  128

static const uint8_t PROMPT_ASK[] = { 0xE9, 0x97, 0xAE, 0xEF, 0xBC, 0x9A };
static const uint8_t PROMPT_ANS[] = { 0x0A, 0xE7, 0xAD, 0x94, 0xEF, 0xBC, 0x9A };

static char s_reply[REPLY_MAX];
static int  s_reply_len;
static uint8_t s_top3[3];
static int  s_top3_n;
static uint8_t s_dbg_first;

// 每生成一个字节回调
static void on_byte(uint8_t b)
{
    if (!s_dbg_first)
    {
        s_top3_n = Nano_DebugTop(s_top3, 3);
        s_dbg_first = 1;
    }
    NanoUart_SendByte(b);
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

static uint32_t s_think_last;
static uint8_t  s_think_phase;

// 生成期间按时间门推进 ". .. ..." 动画
static void on_tick(void)
{
    uint32_t now = Nano_Millis();
    if (now - s_think_last < 300u) return;
    s_think_last = now;
    NanoUI_Thinking(s_think_phase);
    s_think_phase = (uint8_t)((s_think_phase + 1) % 3);
}

// 整数格式化
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
    NanoUI_Boot();

    for (;;)
    {
        int qlen = NanoUart_ReadLine(question, sizeof(question));
        if (qlen <= 0)
        {
            __WFI();
            continue;
        }

        // prompt
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
        NanoUI_Header("egg-nano", 0);
        NanoUI_Question(question);

        uint32_t t0 = Nano_Millis();
        NanoUart_Send("nano> ");
        int n = Nano_Generate(prompt, plen, reply, MAX_NEW, on_byte, on_tick);
        uint32_t dt = Nano_Millis() - t0;
        reply[n] = 0;

        NanoUI_Answer(s_reply);
        fmt_status(status, dt, n);
        NanoUI_Header("egg-nano", status);

        NanoUart_Send("\r\n[");         
        NanoUart_Send(status);
        NanoUart_Send("]\r\n");

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
