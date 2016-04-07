/*
 * fs.c
 *
 *  Created on: Mar 3, 2016
 *      Author: petera
 */

#include "../../spiffs/src/spiffs.h"
#include "fs.h"
#include "octet_spiflash.h"

#define FS (&__spiffs__)

#define FS_SLACK_END_SECTORS      8
#define FS_PAGE_SZ                128
#define FS_BLOCK_SZ               32768
#define FS_DESCRIPTORS            3
#define FS_CACHE_PAGES            4

spiffs __spiffs__;
static uint32_t _fs_work[2 * FS_PAGE_SZ / sizeof(uint32_t)];
static uint32_t _fs_desc[FS_DESCRIPTORS * 40 / sizeof(uint32_t)];
static uint32_t _fs_cache[FS_CACHE_PAGES * (FS_PAGE_SZ + 40) / sizeof(uint32_t)];
static uint32_t old_perc = 999;

static void _spiffs_check_cb_f(spiffs *fs, spiffs_check_type type, spiffs_check_report report,
    u32_t arg1, u32_t arg2) {
  uint32_t perc = arg1 * 100 / 256;
  if (report == SPIFFS_CHECK_PROGRESS && old_perc != perc) {
      old_perc = perc;
      printf("CHECK REPORT: ");
      switch(type) {
      case SPIFFS_CHECK_LOOKUP:
        printf("LU "); break;
      case SPIFFS_CHECK_INDEX:
        printf("IX "); break;
      case SPIFFS_CHECK_PAGE:
        printf("PA "); break;
      }
      printf("%i%%\n", perc);
  }
  if (report != SPIFFS_CHECK_PROGRESS) {
    printf("   check: ");
    switch (type) {
    case SPIFFS_CHECK_INDEX:
      printf("INDEX  "); break;
    case SPIFFS_CHECK_LOOKUP:
      printf("LOOKUP "); break;
    case SPIFFS_CHECK_PAGE:
      printf("PAGE   "); break;
    default:
      printf("????   "); break;
    }
    if (report == SPIFFS_CHECK_ERROR) {
      printf("ERROR %i", arg1);
    } else if (report == SPIFFS_CHECK_DELETE_BAD_FILE) {
      printf("DELETE BAD FILE %04x", arg1);
    } else if (report == SPIFFS_CHECK_DELETE_ORPHANED_INDEX) {
      printf("DELETE ORPHANED INDEX %04x", arg1);
    } else if (report == SPIFFS_CHECK_DELETE_PAGE) {
      printf("DELETE PAGE %04x", arg1);
    } else if (report == SPIFFS_CHECK_FIX_INDEX) {
      printf("FIX INDEX %04x:%04x", arg1, arg2);
    } else if (report == SPIFFS_CHECK_FIX_LOOKUP) {
      printf("FIX INDEX %04x:%04x", arg1, arg2);
    } else {
      printf("??");
    }
    printf("\n");
  }
}


static s32_t _spiffs_hal_read(spiffs *fs, u32_t addr, u32_t size, u8_t *dst) {
  (void)fs;
  sdk_SpiFlashOpResult res = octet_spif_read(addr, dst, size);
  return res == SPI_FLASH_RESULT_OK ? SPIFFS_OK : -1;
}

static s32_t _spiffs_hal_write(spiffs *fs, u32_t addr, u32_t size, u8_t *src) {
  (void)fs;
  sdk_SpiFlashOpResult res = octet_spif_write(addr, src, size);
  return res == SPI_FLASH_RESULT_OK ? SPIFFS_OK : -1;
}

static s32_t _spiffs_hal_erase(spiffs *fs, u32_t addr, u32_t size) {
  (void)fs;
  // feed wdog
  WDT.FEED = WDT_FEED_MAGIC;
  sdk_SpiFlashOpResult res = sdk_spi_flash_erase_sector(addr / SPI_FLASH_SEC_SIZE);
  return res == SPI_FLASH_RESULT_OK ? SPIFFS_OK : -1;
}

int32_t fs_mount(void) {
  int32_t res;
  const uint32_t fs_size =
      (sdk_flashchip.chip_size / 8 - SPI_FLASH_SEC_SIZE * FS_SLACK_END_SECTORS) & ~(FS_BLOCK_SZ-1);
  const uint32_t fs_addr =
      (sdk_flashchip.chip_size - fs_size - SPI_FLASH_SEC_SIZE * FS_SLACK_END_SECTORS)  & ~(FS_BLOCK_SZ-1);
  spiffs_config cfg = {
      .hal_read_f = _spiffs_hal_read,
      .hal_write_f = _spiffs_hal_write,
      .hal_erase_f = _spiffs_hal_erase,
      .phys_size = fs_size,
      .phys_addr = fs_addr,
      .phys_erase_block = SPI_FLASH_SEC_SIZE,
      .log_block_size = FS_BLOCK_SZ,
      .log_page_size = FS_PAGE_SZ,
      .fh_ix_offset = SPIFFS_FILEHDL_OFFSET_NUM
  };
  printf("mounting fs @ 0x%08x, %i kbytes\n", fs_addr, fs_size / 1024);
  res = SPIFFS_mount(FS,
      &cfg, (uint8_t *)_fs_work,
      (uint8_t *)_fs_desc, sizeof(_fs_desc),
      (uint8_t *)_fs_cache, sizeof(_fs_cache),
      _spiffs_check_cb_f);
  if (res != SPIFFS_OK && SPIFFS_errno(FS) == SPIFFS_ERR_NOT_A_FS) {
    printf("fs format\n");
    SPIFFS_clearerr(FS);
    res = SPIFFS_format(FS);
    if (res == SPIFFS_OK) {
      printf("remount\n");
      res = SPIFFS_mount(FS,
          &cfg, (uint8_t *)_fs_work,
          (uint8_t *)_fs_desc, sizeof(_fs_desc),
          (uint8_t *)_fs_cache, sizeof(_fs_cache),
          _spiffs_check_cb_f);
    }
  }
  if (res != SPIFFS_OK) {
    printf("err fs mount %i\n", res);
  } else {
    uint32_t total, used;
    SPIFFS_info(FS, &total, &used);
    printf("mounted fs: total %i kbytes, used %i kbytes\n", total / 1024, used / 1024);
  }
  printf("mount result:%i\n", SPIFFS_errno(FS));

  return res;
}
