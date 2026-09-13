// nano_uart.h —— USART1（PA9=TX / PA10=RX）+ 逐行协议 + DWT 计时
#ifndef __NANO_UART_H
#define __NANO_UART_H

#include "stm32f10x.h"

#define NANO_RX_MAX 96

void     NanoUart_Init(void);
void     NanoUart_SendByte(uint8_t b);
void     NanoUart_Send(const char *s);
void     NanoUart_SendBytes(const uint8_t *p, int n);   // 原始字节（回复可能含任意字节）
void     NanoUart_SendNum(uint32_t v);                  // 十进制整数
void     NanoUart_SendHex(const uint8_t *p, int n);     // 十六进制对（诊断用）
void     USART1_IRQHandler(void);                       // 强定义，覆盖启动文件的 weak 默认
void     NanoUart_IrqHandler(void);                  // 由 stm32f10x_it.c 转调
int      NanoUart_ReadLine(char *out, int max);      // 有完整一行则返回长度，否则 0
uint32_t Nano_Millis(void);                          // DWT 周期计数换算的毫秒

#endif
