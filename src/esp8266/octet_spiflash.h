/*
 * octet_spiflash.h
 *
 *  Created on: Mar 3, 2016
 *      Author: petera
 */

#ifndef _OCTET_SPIFLASH_H_
#define _OCTET_SPIFLASH_H_

#include <stdint.h>
#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <esp8266.h>
#include <esp/uart.h>
#include <stdio.h>
#include "espressif/spi_flash.h"

sdk_SpiFlashOpResult octet_spif_read(uint32_t addr, uint8_t *dst, uint32_t len);
sdk_SpiFlashOpResult octet_spif_write(uint32_t addr, uint8_t *src, uint32_t len);

#endif /* _OCTET_SPIFLASH_H_ */
