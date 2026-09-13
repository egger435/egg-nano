// IIC.c —— 软件 IIC 主机（结构参照 Mini-OLED）
// 本工程接线：SCL = PA6，SDA = PA5（开漏输出，模块自带上拉）
#include "stm32f10x.h"
#include "IIC.h"

#define IIC_SCL_Pin GPIO_Pin_6
#define IIC_SDA_Pin GPIO_Pin_5
#define IIC_Port    GPIOA

// 写 SCL 线
void IIC_W_SCL(uint8_t bitValue)
{
    GPIO_WriteBit(IIC_Port, IIC_SCL_Pin, (BitAction)bitValue);
}

// 写 SDA 线
void IIC_W_SDA(uint8_t bitValue)
{
    GPIO_WriteBit(IIC_Port, IIC_SDA_Pin, (BitAction)bitValue);
}

// 读 SDA 线
uint8_t IIC_R_SDA(void)
{
    return GPIO_ReadInputDataBit(IIC_Port, IIC_SDA_Pin);
}

// 初始化：开漏输出，两条线拉高释放总线
void IIC_Init(void)
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

    GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_InitStructure.GPIO_Pin = IIC_SCL_Pin | IIC_SDA_Pin;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_OD;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(IIC_Port, &GPIO_InitStructure);

    GPIO_SetBits(IIC_Port, IIC_SCL_Pin | IIC_SDA_Pin);
}

// 起始条件
void IIC_Start(void)
{
    IIC_W_SDA(1);
    IIC_W_SCL(1);
    IIC_W_SDA(0);   // SCL 高时 SDA 下降沿 = 起始
    IIC_W_SCL(0);
}

// 停止条件
void IIC_Stop(void)
{
    IIC_W_SDA(0);
    IIC_W_SCL(1);
    IIC_W_SDA(1);   // SCL 高时 SDA 上升沿 = 停止
}

// 发送一个字节（高位在前）
void IIC_SendByte(uint8_t byte)
{
    for (uint8_t i = 0; i < 8; i++)
    {
        IIC_W_SDA(byte & (0x80 >> i));
        IIC_W_SCL(1);
        IIC_W_SCL(0);
    }
}

// 接收一个字节
uint8_t IIC_ReceiveByte(void)
{
    uint8_t byte = 0x00;
    IIC_W_SDA(1);   // 主机释放 SDA
    for (uint8_t i = 0; i < 8; i++)
    {
        IIC_W_SCL(1);
        if (IIC_R_SDA() == 1)
        {
            byte |= 0x80 >> i;
        }
        IIC_W_SCL(0);
    }
    return byte;
}

// 主机发送应答位
void IIC_SendAck(uint8_t AckBit)
{
    IIC_W_SDA(AckBit);
    IIC_W_SCL(1);
    IIC_W_SCL(0);
}

// 主机接收应答位（0 = 从机应答）
uint8_t IIC_ReceiveAck(void)
{
    uint8_t ackBit;
    IIC_W_SDA(1);
    IIC_W_SCL(1);
    ackBit = IIC_R_SDA();
    IIC_W_SCL(0);
    return ackBit;
}
