#include "stm32f10x.h"
#include "PWM.h"
#include "Motor.h"

/*
 * TB6612：
 *   PWMA -> PA2
 *   AIN1 -> PA4
 *   AIN2 -> PA5
 *   STBY -> PA3（由程序拉高使能；也可直接接 3.3V）
 *   VCC  -> 3.3V
 *   GND  -> 与 MCU 共地
 *   VM   -> 外接 5V
 *   AO1/AO2 -> 电机
 */

#define STBY_PIN	GPIO_Pin_3
#define AIN1_PIN	GPIO_Pin_4
#define AIN2_PIN	GPIO_Pin_5
#define PWMA_PIN	GPIO_Pin_2

static void Motor_GPIO_Out(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_InitStructure.GPIO_Pin = PWMA_PIN | STBY_PIN | AIN1_PIN | AIN2_PIN;
	GPIO_Init(GPIOA, &GPIO_InitStructure);
}

void Motor_Init(void)
{
	Motor_GPIO_Out();
	GPIO_ResetBits(GPIOA, STBY_PIN);
	GPIO_ResetBits(GPIOA, AIN1_PIN | AIN2_PIN | PWMA_PIN);
	PWM_Init();
	Motor_Stop();
}

void Motor_SetSpeed(int8_t Speed)
{
	if (Speed > 100)
	{
		Speed = 100;
	}
	if (Speed < -100)
	{
		Speed = -100;
	}

	if (Speed == 0)
	{
		Motor_Stop();
		return;
	}

	/* 恢复 PWM 脚 */
	PWM_Init();
	GPIO_SetBits(GPIOA, STBY_PIN);

	if (Speed > 0)
	{
		GPIO_SetBits(GPIOA, AIN1_PIN);
		GPIO_ResetBits(GPIOA, AIN2_PIN);
		PWM_SetCompare3((uint16_t)Speed);
	}
	else
	{
		GPIO_ResetBits(GPIOA, AIN1_PIN);
		GPIO_SetBits(GPIOA, AIN2_PIN);
		PWM_SetCompare3((uint16_t)(-Speed));
	}
}

void Motor_Stop(void)
{
	PWM_SetCompare3(0);
	GPIO_ResetBits(GPIOA, AIN1_PIN | AIN2_PIN | PWMA_PIN);
	GPIO_ResetBits(GPIOA, STBY_PIN);
}

/* 全速：不依赖定时器 PWM，便于排查 */
void Motor_ForceOn(void)
{
	Motor_GPIO_Out();
	GPIO_SetBits(GPIOA, STBY_PIN);		/* 使能驱动 */
	GPIO_SetBits(GPIOA, AIN1_PIN);		/* 正转 */
	GPIO_ResetBits(GPIOA, AIN2_PIN);
	GPIO_SetBits(GPIOA, PWMA_PIN);		/* PWMA=高=全速 */
}

void Motor_ForceOff(void)
{
	Motor_GPIO_Out();
	GPIO_ResetBits(GPIOA, PWMA_PIN | AIN1_PIN | AIN2_PIN | STBY_PIN);
}
