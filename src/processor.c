/*
 * processor.c
 *
 *  Created on: Aug 1, 2012
 *      Author: petera
 */

#include "processor.h"
#include "system.h"
#include "gpio.h"
#include "app.h"

static void RCC_config() {
  RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
  RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
  RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC, ENABLE);
  RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE);

#ifdef CONFIG_UART1
  RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1, ENABLE);
#endif
#ifdef CONFIG_UART2
  RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);
#endif
#ifdef CONFIG_UART3
  RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART3, ENABLE);
#endif
#ifdef CONFIG_UART4
  RCC_APB1PeriphClockCmd(RCC_APB1Periph_UART4, ENABLE);
#endif

  RCC_APB1PeriphClockCmd(RCC_APB1Periph_BKP, ENABLE);
  RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR, ENABLE);

  RCC_APB1PeriphClockCmd(STM32_SYSTEM_TIMER_RCC, ENABLE);

  /* PCLK1 = HCLK/1 */
  RCC_PCLK1Config(RCC_HCLK_Div1);

  // transducer pwm timer
  RCC_APB2PeriphClockCmd(RCC_APB2Periph_TIM1, ENABLE);
}

static void NVIC_config(void)
{
  // STM32 7 6 5 4 3 2 1 0
  //       I I I I X X X X
  //
  // priogrp 4 =>
  // STM32 7 6 5 4 3 2 1 0
  //       P P P S X X X X
  // preempt prio 0..7
  // subprio      0..1

  // Configure the NVIC Preemption Priority Bits
  // use 3 bits for preemption and 1 bit for  subgroup
  u8_t prioGrp = 8 - __NVIC_PRIO_BITS;
  // use 4 bits for preemption and 0 subgroups
  //u8_t prioGrp = 8 - __NVIC_PRIO_BITS - 1;
  NVIC_SetPriorityGrouping(prioGrp);


  // Config systick interrupt
  NVIC_SetPriority(SysTick_IRQn, NVIC_EncodePriority(prioGrp, 1, 1));

  // Config pendsv interrupt, lowest
  NVIC_SetPriority(PendSV_IRQn, NVIC_EncodePriority(prioGrp, 7, 1));

  // Config & enable TIM interrupt
  NVIC_SetPriority(STM32_SYSTEM_TIMER_IRQn, NVIC_EncodePriority(prioGrp, 1, 0));
  NVIC_EnableIRQ(STM32_SYSTEM_TIMER_IRQn);

  // Config & enable uarts interrupt
#ifdef CONFIG_UART2
  NVIC_SetPriority(USART2_IRQn, NVIC_EncodePriority(prioGrp, 2, 0));
  NVIC_EnableIRQ(USART2_IRQn);
#endif
}

static void UART2_config() {
#ifdef CONFIG_UART2
  gpio_config(PORTA, PIN2, CLK_50MHZ, AF, AF0, PUSHPULL, NOPULL);
  gpio_config(PORTA, PIN3, CLK_50MHZ, IN, AF0, OPENDRAIN, NOPULL);
#endif
}

static void TIM_config() {
  TIM_TimeBaseInitTypeDef  TIM_TimeBaseStructure;

  u16_t prescaler = 0;

  /* Time base configuration */
  TIM_TimeBaseStructure.TIM_Period = SYS_CPU_FREQ/SYS_MAIN_TIMER_FREQ;
  TIM_TimeBaseStructure.TIM_Prescaler = 0;
  TIM_TimeBaseStructure.TIM_ClockDivision = 0;
  TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;

  TIM_TimeBaseInit(STM32_SYSTEM_TIMER, &TIM_TimeBaseStructure);

  /* Prescaler configuration */
  TIM_PrescalerConfig(STM32_SYSTEM_TIMER, prescaler, TIM_PSCReloadMode_Immediate);

  /* TIM IT enable */
  TIM_ITConfig(STM32_SYSTEM_TIMER, TIM_IT_Update, ENABLE);

  /* TIM enable counter */
  TIM_Cmd(STM32_SYSTEM_TIMER, ENABLE);
}

static void TRANS_init(void) {
  // transducer output
  gpio_config(PIN_TRANS_POS, CLK_50MHZ, AF, AF0, PUSHPULL, NOPULL);
  //gpio_config(PIN_TRANS_NEG, CLK_50MHZ, AF, AF0, PUSHPULL, NOPULL);
  gpio_config(PIN_TRANS_NEG, CLK_50MHZ, OUT, AF0, PUSHPULL, NOPULL);
  gpio_disable(PIN_TRANS_NEG);

  // go for 40 kHz
  // 72000000/40000=1880

  const u16_t period = (SYS_CPU_FREQ / CONFIG_TRANSDUCER_FREQ);
  TIM_TimeBaseInitTypeDef  TIM_TimeBaseStructure;
  TIM_TimeBaseStructure.TIM_Period =  period - 1;
  TIM_TimeBaseStructure.TIM_Prescaler = 1-1;
  TIM_TimeBaseStructure.TIM_ClockDivision = 0;
  TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;
  TIM_TimeBaseStructure.TIM_RepetitionCounter = 0;
  TIM_TimeBaseInit(TIM1, &TIM_TimeBaseStructure);

  TIM_OCInitTypeDef  TIM_OCInitStructure;
  TIM_OCInitStructure.TIM_OCMode = TIM_OCMode_PWM1;
  TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable;
  TIM_OCInitStructure.TIM_OCIdleState = TIM_OCIdleState_Set;


  TIM_OCInitStructure.TIM_OCPolarity = TIM_OCPolarity_High;
  TIM_OCInitStructure.TIM_Pulse = period / 2;
  TIM_OC1Init(TIM1, &TIM_OCInitStructure);
  TIM_OC1PreloadConfig(TIM1, TIM_OCPreload_Enable);

  TIM_OCInitStructure.TIM_OCPolarity = TIM_OCPolarity_Low;
  TIM_OCInitStructure.TIM_Pulse = period / 2;
  TIM_OC2Init(TIM1, &TIM_OCInitStructure);
  TIM_OC2PreloadConfig(TIM1, TIM_OCPreload_Enable);

  TIM_ARRPreloadConfig(TIM1, ENABLE);

  TIM_Cmd(TIM1, DISABLE);
  TIM_CtrlPWMOutputs(TIM1, ENABLE);
}

void PROC_base_init() {
  RCC_config();
  NVIC_config();
  TIM_config();
}

void PROC_periph_init() {
  DBGMCU_Config(STM32_SYSTEM_TIMER_DBGMCU, ENABLE);

  // led
  gpio_config(PORTC, PIN13, CLK_50MHZ, OUT, AF0, PUSHPULL, NOPULL);

  UART2_config();

  TRANS_init();
}

