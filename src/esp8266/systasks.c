/*
 * systasks.c
 *
 *  Created on: Apr 10, 2016
 *      Author: petera
 */

#include "systasks.h"
#include "fs.h"
#include "server.h"

static xQueueHandle sysq;

static void systask_task(void *pvParameters) {
  while (1) {
    systask_id id;
    if (xQueueReceive(sysq, &id, portMAX_DELAY)) {
      printf("systask %i\n", id);
      switch(id) {
      case SYS_FS_FORMAT:
        fs_format();
        break;
      case SYS_FS_CHECK:
        fs_check();
        break;
      case SYS_TEST:
      {
        uint32_t progress;
        server_claim_busy();
        for (progress = 0; progress <= 100; progress++) {
          server_set_busy_status("Test busy", progress);
          vTaskDelay(50/portTICK_RATE_MS);
        }

        server_release_busy();
      }
      break;
      default:
        printf("warn: unknown systask id\n");
        break;
      }
    }
  }
}

void systask_init(void) {
  sysq = xQueueCreate(4, sizeof(uint32_t));
  xTaskCreate(systask_task, (signed char * )"systask", 512, &sysq, 2, NULL);
}

void systask_call(systask_id task_id) {
  uint32_t uid = (uint32_t)task_id;
  xQueueSend(sysq, &uid, 0);
}
