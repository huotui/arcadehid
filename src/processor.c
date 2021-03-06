/*
 * processor.c
 *
 *  Created on: Aug 1, 2012
 *      Author: petera
 */

#include "processor.h"
#include "system.h"
#include "gpio.h"
#include "gpio_map.h"
#include "usb_hw_config.h"

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


  /* PCLK1 = HCLK/1 */
  RCC_PCLK1Config(RCC_HCLK_Div1);

  RCC_APB1PeriphClockCmd(STM32_SYSTEM_TIMER_RCC, ENABLE);

  // usb
  RCC_USBCLKConfig(RCC_USBCLKSource_PLLCLK_1Div5);
  RCC_APB1PeriphClockCmd(RCC_APB1Periph_USB, ENABLE);
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
  NVIC_SetPriority(STM32_SYSTEM_TIMER_IRQn, NVIC_EncodePriority(prioGrp, 3, 0));
  NVIC_EnableIRQ(STM32_SYSTEM_TIMER_IRQn);

  // Config & enable uarts interrupt
#ifdef CONFIG_UART2
  NVIC_SetPriority(USART2_IRQn, NVIC_EncodePriority(prioGrp, 2, 0));
  NVIC_EnableIRQ(USART2_IRQn);
#endif

  // usb
  NVIC_SetPriority(USB_LP_CAN1_RX0_IRQn, NVIC_EncodePriority(prioGrp, 3, 0));
  NVIC_EnableIRQ(USB_LP_CAN1_RX0_IRQn);

  NVIC_SetPriority(USBWakeUp_IRQn, NVIC_EncodePriority(prioGrp, 2, 0));
  NVIC_EnableIRQ(USBWakeUp_IRQn);
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

static void GPIO_config() {
#ifndef CONFIG_HY_TEST_BOARD
  // disable jtag, only SWD enabled, free pin PB3
  GPIO_PinRemapConfig(GPIO_Remap_SWJ_JTAGDisable, ENABLE);
#endif

  const gpio_pin_map *led = GPIO_MAP_get_led_map();
  gpio_config_out(led->port, led->pin, CLK_50MHZ, PUSHPULL, NOPULL);

  const gpio_pin_map *in = GPIO_MAP_get_pin_map();
  int i;
  for (i = 0; i < APP_CONFIG_PINS; i++) {
    gpio_config(in[i].port, in[i].pin, CLK_2MHZ, IN, AF0, OPENDRAIN, PULLUP);
  }

  USB_Cable_Config(DISABLE);
}

// ifc

void PROC_base_init() {
  RCC_config();
  NVIC_config();
}

void PROC_periph_init() {
  DBGMCU_Config(STM32_SYSTEM_TIMER_DBGMCU, ENABLE);
  gpio_init();

#ifdef CONFIG_HY_TEST_BOARD
  gpio_config_out(PORTC, PIN13, CLK_50MHZ, PUSHPULL, NOPULL);
#else
  gpio_config(PORTB, PIN9, CLK_2MHZ, IN, AF0, OPENDRAIN, NOPULL);
#endif

  GPIO_config();
  UART2_config();
  TIM_config();
}

