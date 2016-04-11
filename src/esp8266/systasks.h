/*
 * systasks.h
 *
 *  Created on: Apr 10, 2016
 *      Author: petera
 */

#ifndef _ESP8266_SYSTASKS_H_
#define _ESP8266_SYSTASKS_H_

#include "espressif/esp_common.h"
#include "esp/uart.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#define SYSTASK_AP_SCAN_FILENAME ".apscan"

typedef enum {
  SYS_FS_FORMAT = 1,
  SYS_FS_CHECK,
  SYS_WIFI_SCAN,
  SYS_TEST,
} systask_id;

void systask_init(void);
void systask_call(systask_id task_id, bool claim);

#endif /* _ESP8266_SYSTASKS_H_ */
