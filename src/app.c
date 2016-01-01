/*
 * app.c
 *
 *  Created on: Jan 2, 2014
 *      Author: petera
 */

#include "app.h"
#include "taskq.h"
#include "miniutils.h"
#include "gpio.h"
#include "ws2812b_spi_stm32f1.h"

#ifdef CONFIG_I2C
task_mutex i2c_mutex = TASK_MUTEX_INIT;
#endif

void APP_init(void) {
  gpio_enable(PIN_LED);
  WS2812B_STM32F1_init(NULL);
}

void APP_shutdown(void) {
}
