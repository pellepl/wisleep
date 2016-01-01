/*
 * ws2812b_spi_stm32f1.h
 *
 *  Created on: Dec 30, 2015
 *      Author: petera
 */

#ifndef WS2812B_SPI_STM32F1_H_
#define WS2812B_SPI_STM32F1_H_

#include "system.h"

void WS2812B_STM32F1_output(void);
void WS2812B_STM32F1_output_test_pattern(void);
void WS2812B_STM32F1_set(u32_t rgb);
void WS2812B_STM32F1_init(void (* callback)(bool error));

#endif /* WS2812B_SPI_STM32F1_H_ */
