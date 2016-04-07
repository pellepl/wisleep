/*
 * octet_spiflash.c
 *
 *  Created on: Mar 3, 2016
 *      Author: petera
 */

#ifndef _OCTET_SPIFLASH_C_
#define _OCTET_SPIFLASH_C_

#include "octet_spiflash.h"
#include "sdk_internal.h"

#define SPI_FLASH_TRANSACTION_BUFFER_SZ   64

// flash read area must be 4 byte aligned, and so must sdk_spi_flash_reads dst
sdk_SpiFlashOpResult octet_spif_read(uint32_t addr, uint8_t *dst, uint32_t len) {
  sdk_SpiFlashOpResult res = SPI_FLASH_RESULT_OK;
  uint8_t __attribute__((aligned(4))) tmp[4];
  // read unaligned prefix
  if (addr & 3) {
    res = sdk_spi_flash_read(addr & (~3), (uint32_t *)tmp, 4);
    uint8_t pre_len = 4 - (addr & 3);
    memcpy(dst, &tmp[addr & 3], pre_len);
    dst += pre_len;
    len -= pre_len;
    addr += pre_len;
  }
  // addr is now aligned
  // read aligned middle part
  uint32_t aligned_len = len & (~3);
  if (res == SPI_FLASH_RESULT_OK && aligned_len) {
    if ((intptr_t)dst & 3) {
      // read to unaligned mem, chunk it up via aligned buffer
      uint8_t __attribute__((aligned(4))) tmp[SPI_FLASH_TRANSACTION_BUFFER_SZ];
      len -= aligned_len;
      while (res == SPI_FLASH_RESULT_OK && aligned_len) {
        uint32_t chunk_len = aligned_len < SPI_FLASH_TRANSACTION_BUFFER_SZ ? aligned_len : SPI_FLASH_TRANSACTION_BUFFER_SZ;
        res = sdk_spi_flash_read(addr, (uint32_t *)tmp, chunk_len);
        memcpy(dst, tmp, chunk_len);
        dst += chunk_len;
        addr += chunk_len;
        aligned_len -= chunk_len;
      }

    } else {
      // read to aligned mem
      res = sdk_spi_flash_read(addr, (uint32_t *)dst, aligned_len);
      dst += aligned_len;
      len -= aligned_len;
      addr += aligned_len;
    }
  }
  // read unaligned postfix remainder
  if (res == SPI_FLASH_RESULT_OK && len) {
    res = sdk_spi_flash_read(addr, (uint32_t *)tmp, 4);
    memcpy(dst, tmp, len);
  }
  return res;
}

// flash write area must be 4 byte aligned, ram write buffer must
// be 4 byte aligned (sigh)
sdk_SpiFlashOpResult octet_spif_write(uint32_t addr, uint8_t *src, uint32_t len) {
  sdk_SpiFlashOpResult res = SPI_FLASH_RESULT_OK;
  uint8_t __attribute__((aligned(4))) tmp[SPI_FLASH_TRANSACTION_BUFFER_SZ];
  while (res == SPI_FLASH_RESULT_OK && len) {
    uint8_t addr_align = addr & 3;
    uint32_t chunk_len = len < (SPI_FLASH_TRANSACTION_BUFFER_SZ - addr_align) ? len : (SPI_FLASH_TRANSACTION_BUFFER_SZ - addr_align);
    memset(tmp, 0xff, SPI_FLASH_TRANSACTION_BUFFER_SZ);
    memcpy(&tmp[addr_align], src, chunk_len);
    uint32_t wr_len = (chunk_len + addr_align + 3) & (~3);
    res = sdk_spi_flash_write(addr & (~3), (uint32_t *)tmp, wr_len);
    src += chunk_len;
    len -= chunk_len;
    addr += chunk_len;
  }
  return res;
}



#endif /* SRC_ESP8266_OCTET_SPIFLASH_C_ */
