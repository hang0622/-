#include "stm32f10x.h"
#include "DS1302.h"
#include "Delay.h"

TIMEData_t TimeData;
static uint8_t ReadBuf[7];

#define CE_L		GPIO_ResetBits(DS1302_CE_PORT, DS1302_CE_PIN)
#define CE_H		GPIO_SetBits(DS1302_CE_PORT, DS1302_CE_PIN)
#define DATA_L		GPIO_ResetBits(DS1302_DATA_PORT, DS1302_DATA_PIN)
#define DATA_H		GPIO_SetBits(DS1302_DATA_PORT, DS1302_DATA_PIN)
#define SCLK_L		GPIO_ResetBits(DS1302_SCLK_PORT, DS1302_SCLK_PIN)
#define SCLK_H		GPIO_SetBits(DS1302_SCLK_PORT, DS1302_SCLK_PIN)

static uint8_t DecToBcd(uint8_t dec)
{
	return ((dec / 10) << 4) | (dec % 10);
}

static uint8_t BcdToDec(uint8_t bcd)
{
	return (bcd >> 4) * 10 + (bcd & 0x0F);
}

static void DS1302_DataOut(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;
	GPIO_InitStructure.GPIO_Pin = DS1302_DATA_PIN;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
	GPIO_Init(DS1302_DATA_PORT, &GPIO_InitStructure);
}

static void DS1302_DataIn(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;
	GPIO_InitStructure.GPIO_Pin = DS1302_DATA_PIN;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
	GPIO_Init(DS1302_DATA_PORT, &GPIO_InitStructure);
}

static void DS1302_WriteByte(uint8_t data)
{
	uint8_t i;
	SCLK_L;
	DS1302_DataOut();
	for (i = 0; i < 8; i++)
	{
		SCLK_L;
		if (data & 0x01)
		{
			DATA_H;
		}
		else
		{
			DATA_L;
		}
		SCLK_H;
		data >>= 1;
	}
}

static void DS1302_WriteReg(uint8_t address, uint8_t data)
{
	CE_L;
	SCLK_L;
	Delay_us(1);
	CE_H;
	Delay_us(3);
	DS1302_WriteByte(address);
	DS1302_WriteByte(data);
	CE_L;
	SCLK_L;
	Delay_us(3);
}

static uint8_t DS1302_ReadReg(uint8_t address)
{
	uint8_t i;
	uint8_t data = 0;
	CE_L;
	SCLK_L;
	Delay_us(3);
	CE_H;
	Delay_us(3);
	DS1302_WriteByte(address);
	DS1302_DataIn();
	Delay_us(3);
	for (i = 0; i < 8; i++)
	{
		Delay_us(3);
		data >>= 1;
		SCLK_H;
		Delay_us(5);
		SCLK_L;
		Delay_us(30);
		if (GPIO_ReadInputDataBit(DS1302_DATA_PORT, DS1302_DATA_PIN))
		{
			data |= 0x80;
		}
	}
	Delay_us(2);
	CE_L;
	DS1302_DataOut();
	DATA_L;
	return data;
}

void DS1302_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;

	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);

	GPIO_InitStructure.GPIO_Pin = DS1302_CE_PIN | DS1302_SCLK_PIN | DS1302_DATA_PIN;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
	GPIO_Init(GPIOB, &GPIO_InitStructure);

	CE_L;
	SCLK_L;
	DATA_L;
}

void DS1302_SetTime(uint16_t year, uint8_t month, uint8_t day,
					uint8_t hour, uint8_t minute, uint8_t second, uint8_t week)
{
	DS1302_WriteReg(0x8E, 0x00);					/* 关闭写保护 */
	DS1302_WriteReg(0x80, DecToBcd(second));
	DS1302_WriteReg(0x82, DecToBcd(minute));
	DS1302_WriteReg(0x84, DecToBcd(hour));
	DS1302_WriteReg(0x86, DecToBcd(day));
	DS1302_WriteReg(0x88, DecToBcd(month));
	DS1302_WriteReg(0x8A, week);
	DS1302_WriteReg(0x8C, DecToBcd((uint8_t)(year % 100)));
	DS1302_WriteReg(0x8E, 0x80);					/* 开启写保护 */
}

void DS1302_ReadTime(void)
{
	ReadBuf[0] = DS1302_ReadReg(0x81);				/* 秒 */
	ReadBuf[1] = DS1302_ReadReg(0x83);				/* 分 */
	ReadBuf[2] = DS1302_ReadReg(0x85);				/* 时 */
	ReadBuf[3] = DS1302_ReadReg(0x87);				/* 日 */
	ReadBuf[4] = DS1302_ReadReg(0x89);				/* 月 */
	ReadBuf[5] = DS1302_ReadReg(0x8B);				/* 星期 */
	ReadBuf[6] = DS1302_ReadReg(0x8D);				/* 年 */

	TimeData.second = BcdToDec(ReadBuf[0] & 0x7F);
	TimeData.minute = BcdToDec(ReadBuf[1]);
	TimeData.hour = BcdToDec(ReadBuf[2] & 0x3F);
	TimeData.day = BcdToDec(ReadBuf[3]);
	TimeData.month = BcdToDec(ReadBuf[4]);
	TimeData.week = ReadBuf[5] & 0x07;
	TimeData.year = BcdToDec(ReadBuf[6]) + 2000;
}
