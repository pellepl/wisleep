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
#include "semphr.h"
#include "timers.h"

#include "espressif/esp_common.h"
#include "dhcpserver.h"
#include "lwip/api.h"

#include "server.h"

#include "fs.h"

#include "../umac/umac.h"
#include "bridge_esp.h"
#include "systasks.h"

#include "esp/hwrand.h"


uint32_t device_id;
static umac um;
static unsigned char rx_buf[768];
static unsigned char tx_buf[768];
static unsigned char tx_ack_buf[768];
static const long um_tim_id = 123;
static xTimerHandle um_tim_hdl;
xSemaphoreHandle um_mutex;

int _impl_umac_tx_pkt(uint8_t ack, uint8_t *buf, uint16_t len) {
  (void)xSemaphoreTake(um_mutex, portMAX_DELAY);
  int res;
  memcpy(tx_buf, buf, len);
  res = umac_tx_pkt(&um, ack, tx_buf, len);
  (void)xSemaphoreGive(um_mutex);
  return res;
}

int _impl_umac_reply_pkt(uint8_t *buf, uint16_t len) {
  (void)xSemaphoreTake(um_mutex, portMAX_DELAY);
  int res;
  memcpy(tx_ack_buf, buf, len);
  res = umac_tx_reply_ack(&um, tx_ack_buf, len);
  (void)xSemaphoreGive(um_mutex);
  return res;
}

static void uart_task(void *pvParameters) {
  unsigned char ch;
  while (1) {
    if (read(0, (void*) &ch, 1)) { // 0 is stdin
      umac_report_rx_byte(&um, ch);
    }
  }
}

void um_tim_cb(xTimerHandle xTimer) {
  umac_tick(&um);
}

static void umac_impl_request_future_tick(umtick delta_tick) {
  xTimerChangePeriod(um_tim_hdl, delta_tick, 0);
  xTimerReset(um_tim_hdl, 0);
}

static void umac_impl_cancel_future_tick(void) {
  xTimerStop(um_tim_hdl, 0);
}

static void umac_impl_tx_byte(uint8_t c) {
  uart_putc(0, c);
}

static void umac_impl_tx_buf(uint8_t *c, uint16_t len) {
  while (len--) {
    uart_putc(0, *c++);
  }
}

static uint8_t last_rx_seqno = 0;
static void umac_impl_rx_pkt(umac_pkt *pkt) {
  if (pkt->seqno == 0) {
    bridge_rx_pkt(pkt, false);
  } else {
    if (last_rx_seqno == pkt->seqno) {
      bridge_rx_pkt(pkt, true);
    } else {
      bridge_rx_pkt(pkt, false);
      last_rx_seqno = pkt->seqno;
    }
  }
}

static umtick umac_impl_now_tick(void) {
  return xTaskGetTickCount();
}

//#define AP

void user_init(void) {
  uart_set_baud(0, 921600);

  printf("\n\nESP8266 UMAC\n\n");

  bridge_init();
  umac_cfg um_cfg = {
      .timer_fn = umac_impl_request_future_tick,
      .cancel_timer_fn = umac_impl_cancel_future_tick,
      .now_fn = umac_impl_now_tick,
      .tx_byte_fn = umac_impl_tx_byte,
      .tx_buf_fn = umac_impl_tx_buf,
      .rx_pkt_fn = umac_impl_rx_pkt,
      .rx_pkt_ack_fn = bridge_pkt_acked,
      .timeout_fn = bridge_timeout,
      .nonprotocol_data_fn = NULL
  };
  umac_init(&um, &um_cfg, rx_buf);

  um_tim_hdl = xTimerCreate(
      (signed char *)"umac_tim",
      10,
      false,
      (void *)um_tim_id, um_tim_cb);

  char ap_cred[128];
  struct sdk_station_config config;
  bool setup_ap = true;

  systask_init();
  device_id = 0;
  fs_init();
  if (fs_mount() >= 0) {
#if 0
    spiffs_DIR d;
    struct spiffs_dirent e;
    struct spiffs_dirent *pe = &e;

    fs_opendir("/", &d);
    while ((pe = fs_readdir(&d, pe))) {
      printf("%s [%04x] size:%i\n", pe->name, pe->obj_id, pe->size);
    }
    fs_closedir(&d);
#endif

    // read preferred ssid
    spiffs_file fd_ssid = fs_open(".ssid", SPIFFS_RDONLY, 0);
    if (fd_ssid > 0) {
      if (fs_read(fd_ssid, (uint8_t *)ap_cred, sizeof(ap_cred)) > 0) {
        fs_close(fd_ssid);
        char *nl_ix = strchr(ap_cred, '\n');
        if (nl_ix > 0) {
          memset(&config, 0, sizeof(struct sdk_station_config));
          strncpy((char *)&config.ssid, ap_cred, nl_ix - ap_cred);
          char *nl_ix2 = strchr(nl_ix + 1, '\n');
          if (nl_ix2 > 0) {
            strncpy((char *)&config.password, nl_ix + 1, nl_ix2 - (nl_ix + 1));
            setup_ap = false;
          }
        }
        printf("ssid:%s\n", config.ssid);
      } // if read
      else {
        printf("could not read .ssid\n");
      }
    } // if fs_ssid
    else {
      printf("no .ssid found, running softAP\n");
    }
    // find device id or create one
    spiffs_file fd_devid = fs_open(".devid", SPIFFS_RDONLY, 0);
    if (fd_devid < 0) {
      device_id = hwrand();
      fd_devid = fs_open(".devid", SPIFFS_O_CREAT | SPIFFS_O_TRUNC | SPIFFS_O_WRONLY | SPIFFS_O_APPEND, 0);
      fs_write(fd_devid, &device_id, 4);
      printf("create devid\n");
    } else {
      fs_read(fd_devid, &device_id, 4);
    }
    fs_close(fd_devid);
    printf("devid %08x\n", device_id);

    // remove previous scan results
    fs_remove(SYSTASK_AP_SCAN_FILENAME);
  } // if mount

  if (setup_ap) {
    sdk_wifi_set_opmode(SOFTAP_MODE);

    struct ip_info ap_ip;
    IP4_ADDR(&ap_ip.ip, 192, 169, 1, 1);
    IP4_ADDR(&ap_ip.gw, 0, 0, 0, 0);
    IP4_ADDR(&ap_ip.netmask, 255, 255, 255, 0);
    sdk_wifi_set_ip_info(1, &ap_ip);


    struct sdk_softap_config ap_cfg = {
        .ssid = "WISLEEP",
        .password = "",
        .ssid_len = 7,
        .channel = 1,
        .authmode = AUTH_OPEN,
        .ssid_hidden = false,
        .max_connection = 255,
        .beacon_interval = 100
    };

    sdk_wifi_softap_set_config(&ap_cfg);

    ip_addr_t first_client_ip;
    IP4_ADDR(&first_client_ip, 192, 169, 1, 100);
    dhcpserver_start(&first_client_ip, 4);
  } else {
    // required to call wifi_set_opmode before station_set_config
    sdk_wifi_set_opmode(STATION_MODE);
    sdk_wifi_station_set_config(&config);
  }

  server_init(server_actions);
  um_mutex = xSemaphoreCreateMutex();
  xTaskCreate(uart_task, (signed char * )"uart_task", 512, NULL, 2, NULL);
  xTaskCreate(server_task, (signed char *)"server_task", 1024, NULL, 2, NULL);
}
