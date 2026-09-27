#include "stm32f10x.h"
#include "Delay.h"

void Delay_us(uint32_t xus)
{
    SysTick->LOAD = 72 * xus;
    SysTick->VAL = 0x00;
    SysTick->CTRL = 0x00000005;        
    while (!(SysTick->CTRL & 0x00010000));
    SysTick->CTRL = 0x00000004;
}

void Delay_ms(uint32_t xms)
{
    while (xms--)
    {
        Delay_us(1000);
    }
}

void Delay_s(uint32_t xs)
{
    while (xs--)
    {
        Delay_ms(1000);
    }
}
