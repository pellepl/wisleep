/*
 * ws2812b_spi_stm32f1.c
 *
 *  Created on: Dec 29, 2015
 *      Author: petera
 */


/*
 * WS2812B HW PROTOCOL
 *   RESET
 *     -_________-
 *      > 50       us
 *   0 CODE
 *     ---________
 *     0.4  0.85   us  +-0.15us
 *   1 CODE
 *     -------____
 *      0.8   0.45 us  +-0.15us
 *
 * period: 0.8 + 0.45 +- 0.3us = 1.25 +- 0.3 us => 781250 > 800000 > 819672 Hz
 *
 * STM32
 * SPI2 bus is on APB1 clock = CPU/2 = 36 MHz
 * Divide codes into 3 steps => 0.8MHz * 3 = 2.4 MHz
 *   36/2.4 = 15 optimal divider, use 16
 *
 *
 */


#include "system.h"
#include "gpio.h"
#include "ws2812b_spi_stm32f1.h"

#ifndef WS2812B_NBR_OF_LEDS
#define WS2812B_NBR_OF_LEDS 24
#endif

#define RESET_ZEROES              16
#define RESET_ONES                0
#define RESET_LEN                 (RESET_ZEROES + RESET_ONES)

#define CODED_BYTES_PER_RGB_BYTE  3

#define RGB_DATA_LEN \
  (3 * WS2812B_NBR_OF_LEDS * CODED_BYTES_PER_RGB_BYTE)

#define CODE0 0b100
#define CODE1 0b110

static u8_t rgb_data[RESET_LEN + RGB_DATA_LEN + RESET_LEN + 1];
static u32_t rgb_ix;
static void (* _cb)(bool error);

void WS2812B_STM32F1_init(void (* callback)(bool error)) {
  RCC_APB1PeriphClockCmd(RCC_APB1Periph_SPI2, ENABLE);
  RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);
  _cb = callback;

  // config spi & dma
  SPI_InitTypeDef  spi_conf;
  DMA_InitTypeDef  dma_conf;

  // SPI2_MASTER configuration
  spi_conf.SPI_Direction = SPI_Direction_1Line_Tx;
  spi_conf.SPI_Mode = SPI_Mode_Master;
  spi_conf.SPI_DataSize = SPI_DataSize_8b;
  spi_conf.SPI_CPOL = SPI_CPOL_High;
  spi_conf.SPI_CPHA = SPI_CPHA_1Edge;
  spi_conf.SPI_NSS = SPI_NSS_Soft;
  spi_conf.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_16; // APB1/x
  spi_conf.SPI_FirstBit = SPI_FirstBit_MSB;
  spi_conf.SPI_CRCPolynomial = 7;
  SPI_Init(SPI2, &spi_conf);

  // DataRegister offset = 0x0c = SPI2_BASE + 0x0c
  dma_conf.DMA_PeripheralBaseAddr = (uint32_t)(SPI2_BASE + 0x0c);
  dma_conf.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
  dma_conf.DMA_MemoryInc = DMA_MemoryInc_Enable;
  dma_conf.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
  dma_conf.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
  dma_conf.DMA_Mode = DMA_Mode_Normal;
  dma_conf.DMA_Priority = DMA_Priority_VeryHigh;
  dma_conf.DMA_M2M = DMA_M2M_Disable;

  // Configure SPI DMA tx
  DMA_DeInit(DMA1_Channel5);
  dma_conf.DMA_DIR = DMA_DIR_PeripheralDST;
  dma_conf.DMA_BufferSize = 0;
  DMA_Init(DMA1_Channel5, &dma_conf);

  // Enable SPI_MASTER DMA Tx request
  SPI_I2S_DMACmd(SPI2, SPI_I2S_DMAReq_Tx , ENABLE);

  // Enable dma transfer complete and transfer error interrupts
  DMA_ITConfig(DMA1_Channel5, DMA_IT_TC | DMA_IT_TE, ENABLE);

  // config io
  // pin B15 WS2812B as output high
  gpio_config(PORTB, PIN15, CLK_50MHZ, OUT, AF0, PUSHPULL, NOPULL);
  gpio_disable(PORTB, PIN15);

  memset(rgb_data, 0x00, sizeof(rgb_data));

  memset(rgb_data, 0x00, RESET_ZEROES);
  memset(&rgb_data[RESET_LEN + RGB_DATA_LEN], 0x00, RESET_ZEROES);

  rgb_ix = RESET_LEN;
}

void ws2812b_stm32f1_codify(u8_t d) {
  if (rgb_ix >= RESET_LEN + RGB_DATA_LEN) return;
  //012345670123456701234567
  //00_11_22_33_44_55_66_77_
  int i;
  u32_t o = 0;
  for (i = 0; i < 8; i++) {
    o <<= 3;
    o |= d & 0b10000000 ? CODE1 : CODE0;
    d <<= 1;
  }
  rgb_data[rgb_ix++] = (o >> 16) & 0xff;
  rgb_data[rgb_ix++] = (o >> 8) & 0xff;
  rgb_data[rgb_ix++] = o & 0xff;
}

void WS2812B_STM32F1_set(u32_t rgb) {
  //grb
  ws2812b_stm32f1_codify((rgb>>8) & 0xff);
  ws2812b_stm32f1_codify((rgb>>16) & 0xff);
  ws2812b_stm32f1_codify(rgb & 0xff);
}

void WS2812B_STM32F1_output(void) {
  gpio_config(PORTB, PIN15, CLK_50MHZ, AF, AF0, PUSHPULL, NOPULL);

  DMA1_Channel5->CCR &= (u16_t)(~DMA_CCR1_EN);

  DMA1_Channel5->CNDTR = RESET_LEN + RGB_DATA_LEN + RESET_LEN + 1;
  DMA1_Channel5->CMAR = (u32_t)(&rgb_data[0]);

  DMA1_Channel5->CCR |= DMA_CCR1_EN;
  SPI2->CR1 |= 0x0040;

  rgb_ix = 1;
}

void WS2812B_STM32F1_output_test_pattern(void) {
  int i;
  for (i = 0; i < RGB_DATA_LEN; i++) {
    rgb_data[RESET_LEN + i] = 0xaa;
  }
  gpio_config(PORTB, PIN15, CLK_50MHZ, AF, AF0, PUSHPULL, NOPULL);

  DMA1_Channel5->CCR &= (u16_t)(~DMA_CCR1_EN);

  DMA1_Channel5->CNDTR = RGB_DATA_LEN;
  DMA1_Channel5->CMAR = (u32_t)(&rgb_data[RESET_LEN]);

  DMA1_Channel5->CCR |= DMA_CCR1_EN;
  SPI2->CR1 |= 0x0040;

  rgb_ix = 1;
}

void DMA1_Channel5_IRQHandler() {
  bool do_call = FALSE;
  bool err = FALSE;
  if (DMA_GetITStatus(DMA1_IT_TC5)) {
    do_call = TRUE;
    DMA_ClearITPendingBit(DMA1_IT_TC5);
  }
  if (DMA_GetITStatus(DMA1_IT_TE5)) {
    do_call = TRUE;
    err = TRUE;
    DMA_ClearITPendingBit(DMA1_IT_TE5);
  }
  if (do_call && _cb) {
    _cb(err);
  }
}
