#ifndef __OLED_H
#define __OLED_H

#include "stm32f10x.h"

/*
 * 软件 I2C 驱动（4 针 OLED 模块，SSD1306）：
 *   SCL -> PB8
 *   SDA -> PB9
 *   VCC -> 3.3V
 *   GND -> GND
 *
 * 从机地址 0x78（7 位地址 0x3C），SCL/SDA 开漏输出（模块板载上拉电阻）。
 * PB8/PB9 与 DS1302(PB12/13/14)、电机(PA2/3/4/5)、按键(PB1/PB11) 无冲突。
 */

#define OLED_CMD  0
#define OLED_DATA 1

/* 显存缓冲：所有绘制函数写显存，调用 OLED_Refresh_Gram 统一上屏 */
extern uint8_t OLED_GRAM[128][8];

void OLED_WR_Byte(uint8_t dat, uint8_t cmd);
void OLED_Display_On(void);
void OLED_Display_Off(void);
void OLED_Refresh_Gram(void);
void OLED_Init(void);
void OLED_Clear(void);
void OLED_Fill(uint8_t data);
void OLED_DrawPoint(uint8_t x, uint8_t y, uint8_t t);
void OLED_ShowChar(uint8_t x, uint8_t y, uint8_t chr, uint8_t size, uint8_t mode);
void OLED_ShowNumber(uint8_t x, uint8_t y, uint32_t num, uint8_t len, uint8_t size);
void OLED_ShowString(uint8_t x, uint8_t y, const uint8_t *p);

#endif
