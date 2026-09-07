#include "stm32f10x.h"
#include "Delay.h"
#include "Motor.h"
#include "Key.h"
#include "DS1302.h"
#include "OLED.h"

/*
 * 定时控制电机 + OLED 时钟显示
 *
 * 功能：
 *   1. 每 24 小时在设定时刻（默认 08:00）自动启动电机运行 RUN_SECONDS 秒
 *   2. 0.96 寸 OLED（I2C 4 针模块）实时显示当前日期、时间、下次运行时刻、电机状态
 *   3. Key2(PB11)：立即手动试跑 KEY2_RUN_SECONDS 秒
 *   4. Key1(PB1)：短按触发小时 +1；长按 1.5s 切换改分钟模式，再用 Key2 加分钟
 *
 * TB6612 接线（电机不转优先查这四项）：
 *   PWMA -> PA2
 *   AIN1 -> PA4
 *   AIN2 -> PA5
 *   STBY -> PA3   ★改接到 PA3，由程序控制（也可直接接 3.3V）
 *   VCC  -> 3.3V
 *   GND  -> 与 MCU 共地
 *   VM   -> 外接 5V（必须！）
 *   电机 -> AO1 / AO2
 *
 * OLED 接线（软件 I2C，4 针模块）：
 *   SCL  -> PB8
 *   SDA  -> PB9
 *   VCC  -> 3.3V
 *   GND  -> GND
 *   （从机地址 0x78；模块背面 BS0/BS1 需焊在 I2C 模式）
 */

#define NEED_SET_TIME			1
#define SET_YEAR				2026
#define SET_MONTH				9
#define SET_DAY					7
#define SET_HOUR				7
#define SET_MINUTE				59
#define SET_SECOND				0
#define SET_WEEK				1

#define RUN_SECONDS				3		/* 定时自动启动的运行秒数 */
#define KEY2_RUN_SECONDS		3		/* 按 KEY2 手动试跑的运行秒数 */
#define MOTOR_SPEED				30		/* 电机转速 1~100（占空比 %），数值越小越慢 */
#define LOOP_MS					50


static uint8_t RunHour = 8;
static uint8_t RunMinute = 0;
static uint8_t MotorRunning;
static uint16_t RunSeconds;
volatile uint32_t TickMs;				/* 由 TIM3 递增的毫秒计数（全局，供 stm32f10x_it.c 引用） */
static uint32_t RunStartMs;				/* 本次电机启动时的毫秒 */
static uint8_t DoneDay, DoneMonth;
static uint16_t DoneYear;
static uint8_t EditMinuteMode;
static uint16_t Key1HoldMs;

/* 星期名，索引 1~7 对应 周一~周日 */
static const uint8_t *WeekNames[] =
{
	"", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"
};

/* TIM3 产生 1ms 中断，驱动 TickMs 毫秒计数（主循环是否忙碌都不影响计时） */
static void Tick_Init(void)
{
	TIM_TimeBaseInitTypeDef TIM_InitStructure;
	NVIC_InitTypeDef NVIC_InitStructure;

	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);

	TIM_InternalClockConfig(TIM3);
	TIM_InitStructure.TIM_ClockDivision = TIM_CKD_DIV1;
	TIM_InitStructure.TIM_CounterMode = TIM_CounterMode_Up;
	TIM_InitStructure.TIM_Period = 1000 - 1;		/* 72MHz/72=1MHz，1MHz 计数 1000 次 = 1ms */
	TIM_InitStructure.TIM_Prescaler = 72 - 1;
	TIM_InitStructure.TIM_RepetitionCounter = 0;
	TIM_TimeBaseInit(TIM3, &TIM_InitStructure);

	TIM_ITConfig(TIM3, TIM_IT_Update, ENABLE);

	NVIC_InitStructure.NVIC_IRQChannel = TIM3_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
	NVIC_Init(&NVIC_InitStructure);

	TIM_Cmd(TIM3, ENABLE);
}

static void BoardLed_Init(void)
{
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC, ENABLE);
	GPIO_InitTypeDef s;
	s.GPIO_Mode = GPIO_Mode_Out_PP;
	s.GPIO_Pin = GPIO_Pin_13;
	s.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOC, &s);
	GPIO_SetBits(GPIOC, GPIO_Pin_13);
}

static void BoardLed_Set(uint8_t on)
{
	if (on)
	{
		GPIO_ResetBits(GPIOC, GPIO_Pin_13);
	}
	else
	{
		GPIO_SetBits(GPIOC, GPIO_Pin_13);
	}
}

static void BoardLed_BlinkFast(uint8_t times)
{
	uint8_t i;
	for (i = 0; i < times; i++)
	{
		BoardLed_Set(1);
		Delay_ms(60);
		BoardLed_Set(0);
		Delay_ms(60);
	}
}

static void StartMotorRun(uint16_t seconds)
{
	Motor_SetSpeed(MOTOR_SPEED);	/* PWM 调速启动，转速为 MOTOR_SPEED% */
	MotorRunning = 1;
	RunSeconds = seconds;
	RunStartMs = TickMs;
	BoardLed_Set(1);			/* 运行期间灯常亮 */
}

static void StopMotorRun(void)
{
	Motor_Stop();				/* 停止 PWM、AIN 归零、STBY 拉低 */
	MotorRunning = 0;
	RunStartMs = 0;
	BoardLed_Set(0);
}

static void ApplySchedule(void)
{
	if (MotorRunning)
	{
		/* 按真实毫秒计时，不受主循环负载影响 */
		if ((uint32_t)(TickMs - RunStartMs) >= (uint32_t)RunSeconds * 1000u)
		{
			StopMotorRun();
			BoardLed_BlinkFast(2);
		}
		return;
	}

	if (TimeData.hour == RunHour &&
		TimeData.minute == RunMinute &&
		TimeData.second < 2)
	{
		if (!(DoneYear == TimeData.year &&
			  DoneMonth == TimeData.month &&
			  DoneDay == TimeData.day))
		{
			StartMotorRun(RUN_SECONDS);
			DoneYear = TimeData.year;
			DoneMonth = TimeData.month;
			DoneDay = TimeData.day;
		}
	}
}

/* 显示 2 位数字（不足 2 位补前导 0） */
static void OLED_Show2Num(uint8_t x, uint8_t y, uint8_t num, uint8_t size)
{
	OLED_ShowChar(x, y, '0' + num / 10, size, 1);
	OLED_ShowChar(x + size / 2, y, '0' + num % 10, size, 1);
}

/* 画一条直线（Bresenham） */
static void OLED_DrawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1)
{
	int16_t dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
	int16_t dy = (y1 > y0) ? (y1 - y0) : (y0 - y1);
	int16_t sx = (x0 < x1) ? 1 : -1;
	int16_t sy = (y0 < y1) ? 1 : -1;
	int16_t err = dx - dy;

	for (;;)
	{
		OLED_DrawPoint(x0, y0, 1);
		if (x0 == x1 && y0 == y1)
		{
			break;
		}
		int16_t e2 = 2 * err;
		if (e2 > -dy) { err -= dy; x0 += sx; }
		if (e2 < dx)  { err += dx; y0 += sy; }
	}
}

/* 填充椭圆 */
static void OLED_FillEllipse(int16_t cx, int16_t cy, int16_t rx, int16_t ry)
{
	int16_t x, y;
	int32_t rr = (int32_t)rx * rx * ry * ry;
	for (y = cy - ry; y <= cy + ry; y++)
	{
		for (x = cx - rx; x <= cx + rx; x++)
		{
			int32_t dx = x - cx, dy = y - cy;
			if (dx * dx * ry * ry + dy * dy * rx * rx <= rr)
			{
				OLED_DrawPoint(x, y, 1);
			}
		}
	}
}

/* 填充三角形（同侧判定） */
static void OLED_FillTri(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2)
{
	int16_t x, y;
	int16_t minx, maxx, miny, maxy;
	minx = x0; if (x1 < minx) minx = x1; if (x2 < minx) minx = x2;
	maxx = x0; if (x1 > maxx) maxx = x1; if (x2 > maxx) maxx = x2;
	miny = y0; if (y1 < miny) miny = y1; if (y2 < miny) miny = y2;
	maxy = y0; if (y1 > maxy) maxy = y1; if (y2 > maxy) maxy = y2;

	for (y = miny; y <= maxy; y++)
	{
		for (x = minx; x <= maxx; x++)
		{
			int32_t a = (int32_t)(x1 - x0) * (y - y0) - (int32_t)(y1 - y0) * (x - x0);
			int32_t b = (int32_t)(x2 - x1) * (y - y1) - (int32_t)(y2 - y1) * (x - x1);
			int32_t c = (int32_t)(x0 - x2) * (y - y2) - (int32_t)(y0 - y2) * (x - x2);
			if ((a >= 0 && b >= 0 && c >= 0) || (a <= 0 && b <= 0 && c <= 0))
			{
				OLED_DrawPoint(x, y, 1);
			}
		}
	}
}

/*
 * 小鸭子漂浮动画（电机运行 3 秒内播放）：
 *   天空：太阳 + 缓缓飘过的云
 *   水面：流动波纹，小鸭子随波轻轻起伏，周围泛起涟漪
 */
static void OLED_ShowDuck(void)
{
	uint8_t i, n;
	uint32_t ph;		/* 动画进度 ms */
	int16_t dy;			/* 鸭子起伏偏移 */
	int16_t x, y, dx2;
	int16_t r;			/* 涟漪半径 */
	int16_t cloudX;

	/* 直接清显存（不立刻刷新，最后统一上屏） */
	for (i = 0; i < 8; i++)
	{
		for (n = 0; n < 128; n++)
		{
			OLED_GRAM[n][i] = 0x00;
		}
	}

	ph = (uint32_t)(TickMs - RunStartMs);

	/* ---- 天空：太阳 ---- */
	OLED_FillEllipse(114, 10, 5, 5);			/* 太阳 */
	OLED_DrawLine(114, 2, 114, 4);				/* 光线 */
	OLED_DrawLine(114, 16, 114, 18);
	OLED_DrawLine(106, 10, 108, 10);
	OLED_DrawLine(120, 10, 122, 10);
	OLED_DrawLine(108, 4, 110, 6);
	OLED_DrawLine(118, 14, 120, 16);
	OLED_DrawLine(108, 16, 110, 14);
	OLED_DrawLine(118, 4, 120, 6);

	/* ---- 天空：飘过的云 ---- */
	cloudX = 10 + (int16_t)((ph / 80) % 110);	/* 云从左向右缓慢移动 */
	OLED_FillEllipse(cloudX, 8, 7, 3);
	OLED_FillEllipse(cloudX + 8, 7, 5, 3);
	OLED_FillEllipse(cloudX + 4, 4, 6, 4);
	OLED_FillTri(cloudX - 4, 11, cloudX + 14, 11, cloudX + 5, 14);

	/* ---- 水面线 ---- */
	OLED_DrawLine(0, 40, 127, 40);

	/* ---- 流动的水波纹（随 ph 平移，模拟水流） ---- */
	for (y = 44; y <= 60; y += 6)
	{
		int16_t shift = (int16_t)((ph / 150) % 12);
		for (x = shift - 12; x < 128; x += 16)
		{
			OLED_DrawLine(x, y, x + 6, y);
		}
	}

	/* ---- 涟漪：从鸭子周围一圈圈扩散 ---- */
	r = 3 + (int16_t)((ph / 180) % 4);
	for (y = 40; y <= 52; y++)
	{
		for (x = 40; x <= 80; x++)
		{
			dx2 = x - 60;
			int16_t dy2 = y - 46;
			int32_t d2 = dx2 * dx2 + dy2 * dy2;
			if (d2 >= (int32_t)(r - 1) * (r - 1) && d2 <= (int32_t)r * r + 2)
			{
				OLED_DrawPoint(x, y, 1);
			}
		}
	}

	/* ---- 小鸭子（随波起伏） ---- */
	dy = (int16_t)((ph / 200) % 3) - 1;			/* -1~1，上下起伏 */

	/* 尾巴（翘起） */
	OLED_FillTri(70, 42 + dy, 77, 37 + dy, 75, 45 + dy);

	/* 身体（浮在水面） */
	OLED_FillEllipse(58, 44 + dy, 13, 7);

	/* 翅膀 */
	OLED_FillEllipse(56, 44 + dy, 5, 3);

	/* 头 */
	OLED_FillEllipse(44, 32 + dy, 6, 5);

	/* 扁嘴 */
	OLED_FillTri(39, 31 + dy, 43, 29 + dy, 39, 34 + dy);

	/* 眼睛 */
	OLED_DrawPoint(42, 30 + dy, 1);

	/* 鸭子脚下拨出的水花（左、右交替） */
	if ((ph / 250) % 2)
	{
		OLED_DrawPoint(48, 50, 1);
		OLED_DrawPoint(50, 51, 1);
	}
	else
	{
		OLED_DrawPoint(68, 50, 1);
		OLED_DrawPoint(70, 51, 1);
	}
}

/*
 * 刷新 OLED 显示（直接写显存，最后统一 OLED_Refresh_Gram 上屏）：
 *   第 0 行：日期与星期   "2026-08-31 Mon"
 *   第 1 行：时间（大字号）"21:45:30"
 *   第 2 行：下次运行时刻  "Next:08:00 *M"
 *   第 3 行：电机状态      "Motor: IDLE" / "Motor: RUN!"
 */
static void OLED_Refresh(void)
{
	uint8_t buf[17];

	/* 电机运行时播放小鸭子漂浮动画 */
	if (MotorRunning)
	{
		OLED_ShowDuck();
		OLED_Refresh_Gram();
		return;
	}

	/* 日期：年-月-日 */
	OLED_ShowNumber(16, 0, TimeData.year, 4, 12);
	OLED_ShowChar(40, 0, '-', 12, 1);
	OLED_Show2Num(46, 0, TimeData.month, 12);
	OLED_ShowChar(58, 0, '-', 12, 1);
	OLED_Show2Num(64, 0, TimeData.day, 12);
	OLED_ShowString(82, 0, WeekNames[TimeData.week]);

	/* 时间：时:分:秒（8x16 大字号） */
	OLED_Show2Num(32, 12, TimeData.hour, 16);
	OLED_ShowChar(48, 12, ':', 16, 1);
	OLED_Show2Num(56, 12, TimeData.minute, 16);
	OLED_ShowChar(72, 12, ':', 16, 1);
	OLED_Show2Num(80, 12, TimeData.second, 16);

	/* 下次运行时刻 "Next:08:00"，改分钟模式时追加 "*M" */
	buf[0] = 'N'; buf[1] = 'e'; buf[2] = 'x'; buf[3] = 't'; buf[4] = ':';
	buf[5] = '0' + RunHour / 10;
	buf[6] = '0' + RunHour % 10;
	buf[7] = ':';
	buf[8] = '0' + RunMinute / 10;
	buf[9] = '0' + RunMinute % 10;
	buf[10] = '\0';
	OLED_ShowString(8, 30, buf);
	if (EditMinuteMode)
	{
		OLED_ShowString(96, 30, "*M");
	}

	/* 电机状态 */
	if (MotorRunning)
	{
		OLED_ShowString(8, 44, "Motor: RUN!");
	}
	else
	{
		OLED_ShowString(8, 44, "Motor: IDLE");
	}

	OLED_Refresh_Gram();
}

static void HandleKeys(void)
{
	static uint8_t k1Prev = 1, k2Prev = 1;
	uint8_t k1 = GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_1);
	uint8_t k2 = GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_11);

	if (k1 == 0)
	{
		if (Key1HoldMs < 30000)
		{
			Key1HoldMs += LOOP_MS;
		}
		if (Key1HoldMs == 1500)
		{
			EditMinuteMode = !EditMinuteMode;
			BoardLed_BlinkFast(EditMinuteMode ? 6 : 3);
		}
	}
	else
	{
		if (k1Prev == 0 && Key1HoldMs > 20 && Key1HoldMs < 1500)
		{
			RunHour = (RunHour + 1) % 24;
			BoardLed_BlinkFast(1);
		}
		Key1HoldMs = 0;
	}

	if (k2Prev == 0 && k2 != 0)
	{
		Delay_ms(20);
		if (EditMinuteMode)
		{
			RunMinute = (RunMinute + 1) % 60;
			BoardLed_BlinkFast(1);
		}
		else
		{
			/* 每次 Key2 都强制启动试跑 KEY2_RUN_SECONDS 秒 */
			BoardLed_BlinkFast(3);
			StartMotorRun(KEY2_RUN_SECONDS);
		}
	}

	k1Prev = k1;
	k2Prev = k2;
}

int main(void)
{
	uint8_t ledDiv = 0;

	BoardLed_Init();
	BoardLed_BlinkFast(5);

	Delay_ms(300);					/* 等待电源/屏稳定 */

	OLED_Init();
	Tick_Init();					/* TIM3 毫秒节拍，供电机精确计时 */

#if OLED_TEST_ONLY
	/* === OLED 诊断模式 === */
	/* 上电后板载灯先快闪 3 下，表示已进入诊断 */
	BoardLed_BlinkFast(3);

	while (1)
	{
		OLED_Fill(0xFF);			/* 全屏点亮 2 秒 */
		Delay_ms(2000);
		BoardLed_Set(1);
		Delay_ms(200);

		OLED_Display_Off();			/* 屏幕关闭 2 秒，彻底熄灭 */
		Delay_ms(2000);
		BoardLed_Set(0);
		Delay_ms(200);

		OLED_Display_On();			/* 重新开启，看是否还能点亮 */
	}
#endif

	OLED_Fill(0xFF);				/* 全屏点亮自检：亮 1 秒 */
	Delay_ms(1000);
	OLED_Clear();					/* 全屏熄灭：灭 1 秒 */
	Delay_ms(1000);

	Motor_Init();
	Key_Init();
	DS1302_Init();

#if NEED_SET_TIME
	DS1302_SetTime(SET_YEAR, SET_MONTH, SET_DAY,
				   SET_HOUR, SET_MINUTE, SET_SECOND, SET_WEEK);
	BoardLed_BlinkFast(8);
#endif

	/* 上电自动转 3 秒，不依赖按键 */
	StartMotorRun(3);
	Delay_ms(3000);
	StopMotorRun();
	BoardLed_BlinkFast(2);

	while (1)
	{
		DS1302_ReadTime();
		HandleKeys();
		ApplySchedule();
		OLED_Refresh();

		if (!MotorRunning)
		{
			if (++ledDiv >= (1000 / LOOP_MS))
			{
				ledDiv = 0;
				BoardLed_Set(GPIO_ReadOutputDataBit(GPIOC, GPIO_Pin_13) != 0);
			}
		}
		Delay_ms(LOOP_MS);
	}
}
