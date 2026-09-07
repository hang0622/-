#ifndef __DS1302_H
#define __DS1302_H

#include "stm32f10x.h"

/*
 * DS1302 引脚（避开电机 PA2/PA4/PA5 与 OLED PB8/PB9）
 *   CE   -> PB12
 *   I/O  -> PB13
 *   SCLK -> PB14
 */
#define DS1302_CE_PORT			GPIOB
#define DS1302_CE_PIN			GPIO_Pin_12
#define DS1302_DATA_PORT		GPIOB
#define DS1302_DATA_PIN			GPIO_Pin_13
#define DS1302_SCLK_PORT		GPIOB
#define DS1302_SCLK_PIN			GPIO_Pin_14

typedef struct
{
	uint16_t year;
	uint8_t month;
	uint8_t day;
	uint8_t hour;
	uint8_t minute;
	uint8_t second;
	uint8_t week;
} TIMEData_t;

extern TIMEData_t TimeData;

void DS1302_Init(void);
void DS1302_SetTime(uint16_t year, uint8_t month, uint8_t day,
					uint8_t hour, uint8_t minute, uint8_t second, uint8_t week);
void DS1302_ReadTime(void);

#endif
