/* Serial terminal example
 * UART RX is interrupt driven
 * Implements a simple GPIO terminal for setting and clearing GPIOs
 *
 * This sample code is in the public domain.
 */

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
#include "timers.h"

#include "espressif/esp_common.h"

#include "../umac/umac.h"

static umac um;
static unsigned char rx_buf[768];
static unsigned char tx_buf[768];
static unsigned char tx_ack_buf[768];
static const long um_tim_id = 123;
static xTimerHandle um_tim_hdl;

static struct sdk_scan_config scan_cfg;
static void sdk_scan_done_cb(void *arg, sdk_scan_status_t status) {
  uint16_t ix = 0;
  if (status == SCAN_OK) {
    struct sdk_bss_info *bss_link = (struct sdk_bss_info *) arg;
    bss_link = (struct sdk_bss_info *) bss_link->next.stqe_next; //ignore first

    while (bss_link != NULL && ix < sizeof(tx_buf) - (6+32+3)) {
//      printf("ssid:%s  ch%i  rssi:%i\n", bss_link->ssid, bss_link->channel,
//          bss_link->rssi);
      memcpy(&tx_buf[ix], bss_link->bssid, 6);
      ix += 6;
      memcpy(&tx_buf[ix], bss_link->ssid, 32);
      ix += 32;
      tx_buf[ix++] = bss_link->authmode;
      tx_buf[ix++] = bss_link->channel;
      tx_buf[ix++] = bss_link->rssi;
      bss_link = (struct sdk_bss_info *) bss_link->next.stqe_next;
    }
    umac_tx_pkt(&um, true, tx_buf, ix);
  }
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

static umtick umac_impl_now_tick(void) {
  return xTaskGetTickCount();
}

static void umac_impl_rx_pkt(umac_pkt *pkt) {
  printf("got pkt %x, sz %i\n", pkt->seqno, pkt->length);
  if (pkt->length == 0) return;
  switch (pkt->data[0]) {
  case 0x00: {
    umtick t = xTaskGetTickCount();
    tx_ack_buf[0] = t >> 24;
    tx_ack_buf[1] = t >> 16;
    tx_ack_buf[2] = t >> 8;
    tx_ack_buf[3] = t;
    umac_tx_reply_ack(&um, tx_ack_buf, 4);
    break;
  }
  case 0x01: {
    bool res = sdk_wifi_station_scan(&scan_cfg, sdk_scan_done_cb);
    tx_ack_buf[0] = res;
    umac_tx_reply_ack(&um, tx_ack_buf, 1);
    break;
  }
  }
}

static void umac_impl_rx_pkt_acked(uint8_t seqno, uint8_t *data, uint16_t len) {
  (void)seqno;
  (void)data;
  (void)len;
}

static void umac_impl_timeout(umac_pkt *pkt) {
  (void)pkt;
}


void user_init(void) {
  sdk_wifi_set_opmode(STATION_MODE);
  uart_set_baud(0, 921600);

  volatile uint32_t a = 0x100000;
  while (--a);

  printf("\n\nESP8266 UMAC\n\n");

  umac_cfg um_cfg = {
      .timer_fn = umac_impl_request_future_tick,
      .cancel_timer_fn = umac_impl_cancel_future_tick,
      .now_fn = umac_impl_now_tick,
      .tx_byte_fn = umac_impl_tx_byte,
      .tx_buf_fn = umac_impl_tx_buf,
      .rx_pkt_fn = umac_impl_rx_pkt,
      .rx_pkt_ack_fn = umac_impl_rx_pkt_acked,
      .timeout_fn = umac_impl_timeout,
      .nonprotocol_data_fn = NULL
  };
  umac_init(&um, &um_cfg, rx_buf);

  um_tim_hdl = xTimerCreate(
      (signed char *)"umac_tim",
      10,
      false,
      (void *)um_tim_id, um_tim_cb);

  xTaskCreate(&uart_task, (signed char * )"uart_task", 512, NULL, 2, NULL);
}
