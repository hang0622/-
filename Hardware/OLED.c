#include "OLED.h"
#include "oledfont.h"
#include "Delay.h"

/*
 * 软件 I2C 驱动（4 针模块：VCC / GND / SCL / SDA）：
 *   SCL -> PB8
 *   SDA -> PB9
 *   开漏输出（模块板载 4.7k 上拉电阻），从机地址 0x78（7 位地址 0x3C）
 *
 * 与商家 IIC 例程一致：每个字节独立执行
 *   Start -> 从机地址(0x78) -> 控制字(命令0x00/数据0x40) -> 数据 -> Stop
 *
 * 引脚无冲突：DS1302(PB12/13/14)、电机(PA2/3/4/5)、按键(PB1/PB11)、灯(PC13)
 */

#define OLED_W_SCL(x)	GPIO_WriteBit(GPIOB, GPIO_Pin_8, (BitAction)(x))
#define OLED_W_SDA(x)	GPIO_WriteBit(GPIOB, GPIO_Pin_9, (BitAction)(x))

/* 显存缓冲：行（页）维度的列索引 [列][页]，与 main.c 动画共用 */
uint8_t OLED_GRAM[128][8];

/* I2C 起始信号：SCL 高电平期间 SDA 由高拉低 */
static void OLED_I2C_Start(void)
{
	OLED_W_SDA(1);
	OLED_W_SCL(1);
	Delay_us(1);
	OLED_W_SDA(0);
	Delay_us(1);
	OLED_W_SCL(0);
	Delay_us(1);
}

/* I2C 停止信号：SCL 高电平期间 SDA 由低拉高 */
static void OLED_I2C_Stop(void)
{
	OLED_W_SDA(0);
	OLED_W_SCL(1);
	Delay_us(1);
	OLED_W_SDA(1);
	Delay_us(1);
}

/* I2C 发送一个字节（MSB 先行），末尾给一个时钟但不处理从机应答 */
static void OLED_I2C_SendByte(uint8_t Byte)
{
	uint8_t i;
	for (i = 0; i < 8; i++)
	{
		OLED_W_SDA(!!(Byte & (0x80 >> i)));
		Delay_us(1);
		OLED_W_SCL(1);
		Delay_us(1);
		OLED_W_SCL(0);
		Delay_us(1);
	}
	/* 额外一个时钟，跳过 ACK */
	OLED_W_SCL(1);
	Delay_us(1);
	OLED_W_SCL(0);
	Delay_us(1);
}

/* 底层字节发送：cmd=OLED_CMD(0) 发命令，cmd=OLED_DATA(1) 发数据 */
void OLED_WR_Byte(uint8_t dat, uint8_t cmd)
{
	OLED_I2C_Start();
	OLED_I2C_SendByte(0x78);				/* 从机地址（写） */
	OLED_I2C_SendByte(cmd ? 0x40 : 0x00);	/* 数据 / 命令控制字 */
	OLED_I2C_SendByte(dat);
	OLED_I2C_Stop();
}

void OLED_Refresh_Gram(void)
{
	uint8_t i, n;
	for (i = 0; i < 8; i++)
	{
		OLED_WR_Byte(0xb0 + i, OLED_CMD);
		OLED_WR_Byte(0x00, OLED_CMD);
		OLED_WR_Byte(0x10, OLED_CMD);
		for (n = 0; n < 128; n++)
		{
			OLED_WR_Byte(OLED_GRAM[n][i], OLED_DATA);
		}
	}
}

void OLED_Display_On(void)
{
	OLED_WR_Byte(0x8D, OLED_CMD);
	OLED_WR_Byte(0x14, OLED_CMD);
	OLED_WR_Byte(0xAF, OLED_CMD);
}

void OLED_Display_Off(void)
{
	OLED_WR_Byte(0x8D, OLED_CMD);
	OLED_WR_Byte(0x10, OLED_CMD);
	OLED_WR_Byte(0xAE, OLED_CMD);
}

void OLED_Clear(void)
{
	uint8_t i, n;
	for (i = 0; i < 8; i++)
	{
		for (n = 0; n < 128; n++)
		{
			OLED_GRAM[n][i] = 0x00;
		}
	}
	OLED_Refresh_Gram();
}

void OLED_Fill(uint8_t data)
{
	uint8_t i, n;
	for (i = 0; i < 8; i++)
	{
		for (n = 0; n < 128; n++)
		{
			OLED_GRAM[n][i] = data;
		}
	}
	OLED_Refresh_Gram();
}

void OLED_DrawPoint(uint8_t x, uint8_t y, uint8_t t)
{
	uint8_t pos, bx, temp;
	if (x > 127 || y > 63)
	{
		return;
	}
	pos = 7 - y / 8;
	bx = y % 8;
	temp = 1 << (7 - bx);
	if (t)
	{
		OLED_GRAM[x][pos] |= temp;
	}
	else
	{
		OLED_GRAM[x][pos] &= ~temp;
	}
}

void OLED_ShowChar(uint8_t x, uint8_t y, uint8_t chr, uint8_t size, uint8_t mode)
{
	uint8_t temp, t, t1;
	uint8_t y0 = y;
	chr = chr - ' ';
	for (t = 0; t < size; t++)
	{
		if (size == 12)
		{
			temp = oled_asc2_1206[chr][t];
		}
		else
		{
			temp = oled_asc2_1608[chr][t];
		}
		for (t1 = 0; t1 < 8; t1++)
		{
			if (temp & 0x80)
			{
				OLED_DrawPoint(x, y, mode);
			}
			else
			{
				OLED_DrawPoint(x, y, !mode);
			}
			temp <<= 1;
			y++;
			if ((y - y0) == size)
			{
				y = y0;
				x++;
				break;
			}
		}
	}
}

static uint32_t oled_pow(uint8_t m, uint8_t n)
{
	uint32_t result = 1;
	while (n--)
	{
		result *= m;
	}
	return result;
}

void OLED_ShowNumber(uint8_t x, uint8_t y, uint32_t num, uint8_t len, uint8_t size)
{
	uint8_t t, temp;
	uint8_t enshow = 0;
	for (t = 0; t < len; t++)
	{
		temp = (num / oled_pow(10, len - t - 1)) % 10;
		if (enshow == 0 && t < (len - 1))
		{
			if (temp == 0)
			{
				OLED_ShowChar(x + (size / 2) * t, y, ' ', size, 1);
				continue;
			}
			else
			{
				enshow = 1;
			}
		}
		OLED_ShowChar(x + (size / 2) * t, y, temp + '0', size, 1);
	}
}

void OLED_ShowString(uint8_t x, uint8_t y, const uint8_t *p)
{
#define MAX_CHAR_POSX 122
#define MAX_CHAR_POSY 58
	while (*p != '\0')
	{
		if (x > MAX_CHAR_POSX)
		{
			x = 0;
			y += 16;
		}
		if (y > MAX_CHAR_POSY)
		{
			y = x = 0;
			OLED_Clear();
		}
		OLED_ShowChar(x, y, *p, 12, 1);
		x += 8;
		p++;
	}
}

void OLED_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;

	/* 上电等待电源/屏稳定 */
	Delay_ms(100);

	/* SCL=PB8、SDA=PB9：开漏输出（模块板载上拉） */
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_OD;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_8 | GPIO_Pin_9;
	GPIO_Init(GPIOB, &GPIO_InitStructure);

	OLED_W_SCL(1);
	OLED_W_SDA(1);

	/* 初始化序列与 4-1 I2C 例程一致（0xC8：页 7 显示在顶部，配合 OLED_DrawPoint） */
	OLED_WR_Byte(0xAE, OLED_CMD);	/* 关闭显示 */
	OLED_WR_Byte(0xD5, OLED_CMD);	/* 时钟分频比/振荡器频率 */
	OLED_WR_Byte(0x80, OLED_CMD);
	OLED_WR_Byte(0xA8, OLED_CMD);	/* 多路复用率 1/64 */
	OLED_WR_Byte(0x3F, OLED_CMD);
	OLED_WR_Byte(0xD3, OLED_CMD);	/* 显示偏移 */
	OLED_WR_Byte(0x00, OLED_CMD);
	OLED_WR_Byte(0x40, OLED_CMD);	/* 显示起始行 */
	OLED_WR_Byte(0xA1, OLED_CMD);	/* SEG 映射（左右正常） */
	OLED_WR_Byte(0xC0, OLED_CMD);	/* COM 扫描方向（上下正常） */
	OLED_WR_Byte(0xDA, OLED_CMD);	/* COM 引脚硬件配置 */
	OLED_WR_Byte(0x12, OLED_CMD);
	OLED_WR_Byte(0x81, OLED_CMD);	/* 对比度 */
	OLED_WR_Byte(0xCF, OLED_CMD);
	OLED_WR_Byte(0xD9, OLED_CMD);	/* 预充电周期 */
	OLED_WR_Byte(0xF1, OLED_CMD);
	OLED_WR_Byte(0xDB, OLED_CMD);	/* VCOMH 取消选择级别 */
	OLED_WR_Byte(0x30, OLED_CMD);
	OLED_WR_Byte(0xA4, OLED_CMD);	/* 关闭整屏显示 */
	OLED_WR_Byte(0xA6, OLED_CMD);	/* 正常显示（非反色） */
	OLED_WR_Byte(0x8D, OLED_CMD);	/* 电荷泵 */
	OLED_WR_Byte(0x14, OLED_CMD);
	OLED_WR_Byte(0xAF, OLED_CMD);	/* 开启显示 */

	OLED_Clear();
}
