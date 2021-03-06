/*
 * systasks.c
 *
 *  Created on: Apr 10, 2016
 *      Author: petera
 */

#include "systasks.h"
#include "fs.h"
#include "server.h"
#include "ntp.h"
#include "udputil.h"
#include "bridge_esp.h"

#define SYSTASK_CLAIM_FLAG (1<<31)

static xQueueHandle sysq;
static bool apscan_dbg = false;

static struct sdk_scan_config scan_cfg;
static void sdk_scan_done_cb(void *arg, sdk_scan_status_t status) {
  if (status == SCAN_OK) {
    spiffs_file fd = fs_open(SYSTASK_AP_SCAN_FILENAME,
        SPIFFS_O_CREAT | SPIFFS_O_TRUNC | SPIFFS_O_APPEND | SPIFFS_O_RDWR, 0);
    struct sdk_bss_info *bss_link = (struct sdk_bss_info *) arg;
    bss_link = (struct sdk_bss_info *) bss_link->next.stqe_next; //ignore first
    while (fd >= 0 && bss_link != NULL) {
      if (apscan_dbg) {
        printf("ch:%2i  bssid:%02x.%02x.%02x.%02x.%02x.%02x  ssid:%s  rssi:%i\n",
            bss_link->channel, bss_link->bssid[5], bss_link->bssid[4],bss_link->bssid[3],bss_link->bssid[2],
            bss_link->bssid[1],bss_link->bssid[0], bss_link->ssid, bss_link->rssi);
      }
      fs_write(fd, bss_link, sizeof(struct sdk_bss_info));
      bss_link = (struct sdk_bss_info *) bss_link->next.stqe_next;
    }
    fs_close(fd);
    if (apscan_dbg) {
      printf("scan finished\n");
      apscan_dbg = false;
    }
    server_release_busy();
  }
}

static void systask_task(void *pvParameters) {
  while (1) {
    uint32_t msg;
    if (xQueueReceive(sysq, &msg, portMAX_DELAY)) {
      systask_id id;
      bool claimed = false;
      if (msg & SYSTASK_CLAIM_FLAG) {
        id = (msg & ~SYSTASK_CLAIM_FLAG);
        claimed = true;
      } else {
        id = msg;
      }
      printf("systask %i EXEC\n", id);
      switch(id) {
      case SYS_FS_FORMAT:
        fs_format();
        break;
      case SYS_FS_CHECK:
        fs_check();
        break;
      case SYS_WIFI_SCAN_DBG:
        apscan_dbg = true;
        printf("scanning aps\n");
        // no break
      case SYS_WIFI_SCAN:
        server_claim_busy();
        server_set_busy_status("Scanning", -1);
        fs_remove(SYSTASK_AP_SCAN_FILENAME);
        bool res = sdk_wifi_station_scan(&scan_cfg, sdk_scan_done_cb);
        if (!res) {
          if (apscan_dbg) {
            printf("Failed scanning aps\n");
          }
          apscan_dbg = false;
          server_release_busy();
        }
        break;
      case SYS_NTP_QUERY: {
        ntp_query();
      }
      break;
      case SYS_UDP_SEND: {
        udputil_send();
      }
      break;
      case SYS_UDP_RECV: {
        udputil_recv();
      }
      break;
      case SYS_UDP_SEND_RECV: {
        udputil_send_recv();
      }
      break;
      case SYS_PING: {
        bridge_ping();
      }
      break;
      case SYS_TEST: {
        uint32_t progress;
        server_claim_busy();
        for (progress = 0; progress <= 100; progress++) {
          server_set_busy_status("Test busy", progress);
          vTaskDelay(100/portTICK_RATE_MS);
        }

        server_release_busy();
      }
      break;
      default:
        printf("warn: unknown systask id\n");
        break;
      }
      printf("systask %i FINISHED\n", id);
      if (claimed) {
        server_release_busy();
      }
    }
  }
}

void systask_init(void) {
  sysq = xQueueCreate(4, sizeof(uint32_t));
  xTaskCreate(systask_task, (signed char * )"systask", 512, &sysq, 2, NULL);
}

void systask_call(systask_id task_id, bool claim) {
  uint32_t uid = (uint32_t)task_id;
  if (claim) {
    server_claim_busy();
    server_set_busy_status("Please wait", -1);
    uid |= SYSTASK_CLAIM_FLAG;
  }
  xQueueSend(sysq, &uid, 0);
}
