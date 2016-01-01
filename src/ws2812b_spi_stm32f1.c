/*
 * ws2812b_spi_stm32f1.c
 *
 *  Created on: Dec 29, 2015
 *      Author: petera
 */

#include "system.h"
#include "gpio.h"
#include "ws2812b_spi_stm32f1.h"

#ifndef WS2812B_NBR_OF_LEDS
#define WS2812B_NBR_OF_LEDS 24
#endif

#define PREAMBLE                  16
#define CODED_BYTES_PER_RGB_BYTE  4

#define RGB_DATA_LEN \
  (3 * WS2812B_NBR_OF_LEDS * CODED_BYTES_PER_RGB_BYTE)


#define CODE0 0b1000
#define CODE1 0b1110

static u8_t rgb_data[PREAMBLE + RGB_DATA_LEN + 1];
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
  spi_conf.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_32; // APB2/32
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
  gpio_set(PORTB, PIN15, 0);

  memset(rgb_data, 0xff, sizeof(rgb_data));
  memset(rgb_data, 0x00, PREAMBLE-PREAMBLE/4);
  rgb_ix = PREAMBLE;
}

void ws2812b_stm32f1_codify(u8_t d) {
  if (rgb_ix >= PREAMBLE + RGB_DATA_LEN) return;
  int i;
  for (i = 0; i < 4; i++) {
    u8_t o = 0;
    u8_t b;
    b = d & 0b10000000;
    o |= (b ? CODE1 : CODE0) << 4;
    d <<= 1;
    b = d & 0b10000000;
    o |= (b ? CODE1 : CODE0);
    d <<= 1;

    rgb_data[rgb_ix++] = o;
  }
}

void WS2812B_STM32F1_set(u32_t rgb) {
  ws2812b_stm32f1_codify((rgb>>8) & 0xff);
  ws2812b_stm32f1_codify((rgb>>16) & 0xff);
  ws2812b_stm32f1_codify(rgb & 0xff);
}

void WS2812B_STM32F1_output(void) {
  gpio_config(PORTB, PIN15, CLK_50MHZ, AF, AF0, PUSHPULL, NOPULL);

  DMA1_Channel5->CCR &= (u16_t)(~DMA_CCR1_EN);

  DMA1_Channel5->CNDTR = RGB_DATA_LEN + PREAMBLE + 1;
  DMA1_Channel5->CMAR = (u32_t)(&rgb_data[0]);

  DMA1_Channel5->CCR |= DMA_CCR1_EN;
  SPI2->CR1 |= 0x0040;

  rgb_ix = 1;
}

void WS2812B_STM32F1_output_test_pattern(void) {
  int i;
  for (i = 0; i < RGB_DATA_LEN; i++) {
    rgb_data[PREAMBLE + i] = 0xaa;
  }
  gpio_config(PORTB, PIN15, CLK_50MHZ, AF, AF0, PUSHPULL, NOPULL);

  DMA1_Channel5->CCR &= (u16_t)(~DMA_CCR1_EN);

  DMA1_Channel5->CNDTR = RGB_DATA_LEN;
  DMA1_Channel5->CMAR = (u32_t)(&rgb_data[PREAMBLE]);

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
