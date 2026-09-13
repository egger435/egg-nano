// nano_uart.c —— USART1 逐行输入（115200 8N1）+ DWT 毫秒计时
//
// 协议：一行一条问题，收到 '\n' 或 '\r' 触发；支持退格；收到即回显（本地无回显的终端也能用）
// 计时：用 DWT 周期计数器（Cortex-M3 自带），不占用 SysTick（Delay.c 在读它）

#include <string.h>
#include "stm32f10x.h"
#include "nano_uart.h"

static volatile uint8_t  s_buf[NANO_RX_MAX];
static volatile uint16_t s_len;
static volatile uint8_t  s_ready;
static volatile uint8_t  s_prev_cr;      // 上一字节是 '\r'（用于吃掉 CRLF 的后半个）

void NanoUart_Init(void)
{
    GPIO_InitTypeDef  GPIO_InitStructure;
    USART_InitTypeDef USART_InitStructure;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_USART1, ENABLE);

    // PA9 = USART1_TX（复用推挽）
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    // PA10 = USART1_RX（浮空输入）
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    USART_InitStructure.USART_BaudRate = 115200;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(USART1, &USART_InitStructure);

    USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);
    NVIC_SetPriority(USART1_IRQn, 1);
    NVIC_EnableIRQ(USART1_IRQn);
    USART_Cmd(USART1, ENABLE);

    // DWT 周期计数器（计时用，59.6s 回绕，够用）
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

void NanoUart_SendByte(uint8_t b)
{
    while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
    USART_SendData(USART1, b);
}

void NanoUart_Send(const char *s)
{
    while (*s) NanoUart_SendByte((uint8_t)*s++);
}

void NanoUart_SendBytes(const uint8_t *p, int n)
{
    for (int i = 0; i < n; i++) NanoUart_SendByte(p[i]);
}

// 十进制整数（不用 printf，省 Flash）
void NanoUart_SendNum(uint32_t v)
{
    char tmp[12];
    int i = 0;
    if (v == 0) tmp[i++] = '0';
    while (v) { tmp[i++] = (char)('0' + v % 10); v /= 10; }
    while (i) NanoUart_SendByte((uint8_t)tmp[--i]);
}

// 十六进制对（诊断用）
void NanoUart_SendHex(const uint8_t *p, int n)
{
    static const char H[] = "0123456789abcdef";
    for (int i = 0; i < n; i++) {
        NanoUart_SendByte((uint8_t)H[p[i] >> 4]);
        NanoUart_SendByte((uint8_t)H[p[i] & 0x0F]);
        NanoUart_SendByte(' ');
    }
}

// USART1 接收中断：逐字节组行
// 说明：工程的 stm32f10x_it.c 只定义了内核异常处理，没有 USART1_IRQHandler，
//       而启动文件里 ISR 是 weak 的 → 这里强定义即可，不必改模板文件
void USART1_IRQHandler(void)
{
    if (USART_GetITStatus(USART1, USART_IT_RXNE) == RESET) return;

    uint8_t b = (uint8_t)(USART_ReceiveData(USART1) & 0xFF);

    if (b == '\r' || b == '\n')
    {
        // 串口助手常发 "\r\n"：后半个换行只忽略，不再回显/触发（否则每次回车空两行）
        if (b == '\n' && s_prev_cr)
        {
            s_prev_cr = 0;
            return;
        }
        s_prev_cr = (b == '\r') ? 1 : 0;
        if (s_len > 0 && !s_ready) s_ready = 1;
        NanoUart_SendByte('\r');
        NanoUart_SendByte('\n');
    }
    else if (b == 0x08 || b == 0x7F)                    // 退格
    {
        s_prev_cr = 0;
        if (s_len > 0)
        {
            s_len--;
            NanoUart_SendByte(0x08);
            NanoUart_SendByte(' ');
            NanoUart_SendByte(0x08);
        }
    }
    else if (s_len < NANO_RX_MAX - 1)
    {
        s_prev_cr = 0;
        s_buf[s_len++] = b;
        NanoUart_SendByte(b);                            // 回显
    }
}

int NanoUart_ReadLine(char *out, int max)
{
    int n = 0;
    if (!s_ready) return 0;
    __disable_irq();
    n = (int)s_len;
    if (n > max - 1) n = max - 1;
    for (int i = 0; i < n; i++) out[i] = (char)s_buf[i];
    out[n] = 0;
    s_len = 0;
    s_ready = 0;
    __enable_irq();
    return n;
}

uint32_t Nano_Millis(void)
{
    return DWT->CYCCNT / 72000u;      // 72MHz → 1ms = 72000 周期
}
