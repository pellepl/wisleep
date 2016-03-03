/*
 * fs.h
 *
 *  Created on: Mar 3, 2016
 *      Author: petera
 */

#ifndef _FS_H_
#define _FS_H_

#include <stdint.h>
#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <esp8266.h>
#include <esp/uart.h>
#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"
#include "espressif/esp_common.h"
#include "../../spiffs/src/spiffs.h"

extern spiffs __spiffs__;

int32_t fs_mount(void);
#define fs_unmount(void) \
SPIFFS_unmount(&__spiffs__)
#define fs_creat(__path, __mode) \
SPIFFS_creat(&__spiffs__, __path, __mode)
#define fs_open(__path, __flags, __mode) \
SPIFFS_open(&__spiffs__, __path, __flags, __mode)
#define fs_open_by_dirent(__dirent, __flags, __mode) \
SPIFFS_open_by_dirent(&__spiffs__, __dirent, __flags, __mode)
#define fs_open_by_page(__page_ix, __flags, __mode) \
SPIFFS_open_by_page(&__spiffs__, __page_ix, __flags, __mode)
#define fs_read(__fh, __buf, __len) \
SPIFFS_read(&__spiffs__, __fh, __buf, __len)
#define fs_write(__fh, __buf, __len) \
SPIFFS_write(&__spiffs__, __fh, __buf, __len)
#define fs_lseek(__fh, __offs, __whence) \
SPIFFS_lseek(&__spiffs__, __fh, __offs, __whence)
#define fs_remove(__path) \
SPIFFS_remove(&__spiffs__, __path)
#define fs_fremove(__fh) \
SPIFFS_fremove(&__spiffs__, __fh)
#define fs_stat(__path, __stat) \
SPIFFS_stat(&__spiffs__, __path, __stat)
#define fs_fstat(__fh, __stat) \
SPIFFS_fstat(&__spiffs__, __fh, __stat)
#define fs_close(__fh) \
SPIFFS_close(&__spiffs__, __fh)
#define fs_rename(__old_path, __new_path) \
SPIFFS_rename(&__spiffs__, __old_path, __new_path)
#define fs_errno() \
SPIFFS_errno(&__spiffs__)
#define fs_clearerr() \
SPIFFS_clearerr(&__spiffs__)
#define fs_opendir(__path, __dir) \
SPIFFS_opendir(&__spiffs__, __path, __dir)
#define fs_closedir(__dirent) \
SPIFFS_closedir(&__spiffs__, __dirent)
#define fs_info(__total, __used) \
SPIFFS_info(&__spiffs__, __total, __used)
#define fs_format() \
SPIFFS_format(&__spiffs__)
#define fs_mounted() \
SPIFFS_mounted(&__spiffs__)
#define fs_gc_quick(__max_free_pages) \
SPIFFS_gc_quick(&__spiffs__, __max_free_pages)
#define fs_gc(__sz) \
SPIFFS_gc(&__spiffs__, __sz)
#define fs_tell(__fh) \
SPIFFS_tell(&__spiffs__, __fh)
#define fs_eof(__fh) \
SPIFFS_eof(&__spiffs__, __fh)
#define fs_vis() \
SPIFFS_vis(&__spiffs__)

#endif /* _FS_H_ */
