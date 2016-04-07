/*
 * spiffs_config.h
 *
 *  Created on: Mar 3, 2016
 *      Author: petera
 */

#ifndef _SPIFFS_CONFIG_H_
#define _SPIFFS_CONFIG_H_

#include <stdio.h>
#include <stdint.h>
#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

typedef uint8_t  u8_t;
typedef int8_t   s8_t;
typedef uint16_t u16_t;
typedef int16_t  s16_t;
typedef uint32_t u32_t;
typedef int32_t  s32_t;

#define SPIFFS_DBG(...)       //printf("FS\t"__VA_ARGS__)
#define SPIFFS_GC_DBG(...)    //printf("FSGC\t"__VA_ARGS__)
#define SPIFFS_CACHE_DBG(...) //printf("FSCA\t"__VA_ARGS__)
#define SPIFFS_CHECK_DBG(...) //printf("FSCH\t"__VA_ARGS__)

#define SPIFFS_BUFFER_HELP              0
#define SPIFFS_CACHE                    1
#define SPIFFS_CACHE_WR                 1
#define SPIFFS_CACHE_STATS              0
#define SPIFFS_PAGE_CHECK               1
#define SPIFFS_GC_MAX_RUNS              5
#define SPIFFS_GC_STATS                 0
#define SPIFFS_OBJ_NAME_LEN             (32)
#define SPIFFS_COPY_BUFFER_STACK        (128)
#define SPIFFS_USE_MAGIC                (1)
#define SPIFFS_USE_MAGIC_LENGTH         (1)
#define SPIFFS_LOCK(fs)
#define SPIFFS_UNLOCK(fs)
#define SPIFFS_SINGLETON                0
#define SPIFFS_GC_HEUR_W_DELET          (5)
#define SPIFFS_GC_HEUR_W_USED           (-1)
#define SPIFFS_GC_HEUR_W_ERASE_AGE      (50)
#define SPIFFS_ALIGNED_OBJECT_INDEX_TABLES       1
#define SPIFFS_HAL_CALLBACK_EXTRA       1
#define SPIFFS_FILEHDL_OFFSET           1
#define SPIFFS_FILEHDL_OFFSET_NUM       0x1000
#define SPIFFS_READ_ONLY                0
#define SPIFFS_TEST_VISUALISATION       1

#define spiffs_printf(...)              printf(__VA_ARGS__)

#define SPIFFS_TEST_VIS_FREE_STR          "_"
#define SPIFFS_TEST_VIS_DELE_STR          "/"
#define SPIFFS_TEST_VIS_INDX_STR(id)      "i"
#define SPIFFS_TEST_VIS_DATA_STR(id)      "d"

// Block index type. Make sure the size of this type can hold
// the highest number of all blocks - i.e. spiffs_file_system_size / log_block_size
typedef u16_t spiffs_block_ix;
// Page index type. Make sure the size of this type can hold
// the highest page number of all pages - i.e. spiffs_file_system_size / log_page_size
typedef u16_t spiffs_page_ix;
// Object id type - most significant bit is reserved for index flag. Make sure the
// size of this type can hold the highest object id on a full system,
// i.e. 2 + (spiffs_file_system_size / (2*log_page_size))*2
typedef u16_t spiffs_obj_id;
// Object span index type. Make sure the size of this type can
// hold the largest possible span index on the system -
// i.e. (spiffs_file_system_size / log_page_size) - 1
typedef u16_t spiffs_span_ix;

#endif /* SPIFFS_CONFIG_H_ */
