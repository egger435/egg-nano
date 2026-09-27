// nano_uart.h —— USART1（PA9=TX / PA10=RX）+ 逐行协议 + DWT 计时
#ifndef __NANO_UART_H
#define __NANO_UART_H

#include "stm32f10x.h"

#define NANO_RX_MAX 96

void     NanoUart_Init(void);
void     NanoUart_SendByte(uint8_t b);
void     NanoUart_Send(const char *s);
void     NanoUart_SendBytes(const uint8_t *p, int n);   
void     NanoUart_SendNum(uint32_t v);                  
void     NanoUart_SendHex(const uint8_t *p, int n);     
void     USART1_IRQHandler(void);                       
void     NanoUart_IrqHandler(void);                  
int      NanoUart_ReadLine(char *out, int max);      
uint32_t Nano_Millis(void); 

#endif
