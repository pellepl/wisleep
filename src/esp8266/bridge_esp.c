/*
 * bridge.c
 *
 *  Created on: Apr 13, 2016
 *      Author: petera
 */

#include "bridge_esp.h"

#include <stdint.h>
#include <sys/types.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "udputil.h"
#include "systasks.h"
#include "../protocol.h"
#include "fs.h"
#include <esp/hwrand.h>

static xQueueHandle syncq;
static uint32_t sync_seqno;
static lamp_status lamp;
static uint32_t ping_val;
static struct {
  uint8_t udp_pkt_preamble[5];
  uint8_t udp_rx_buf[512];
} udp_rx;

static void bridge_udp_recv_cb(int res, uint32_t ip, uint8_t *buf, uint16_t len);

void bridge_ping(void) {
  ping_val = hwrand();
  uint8_t pkt[] = {
      P_STM_HELLO,
      (ping_val >> 24),
      (ping_val >> 16),
      (ping_val >> 8),
      (ping_val)
  };
  bridge_tx_pkt(true, pkt, sizeof(pkt));
}

void bridge_lamp_set_color(uint32_t rgb) {
  uint8_t pkt[] = {
      P_STM_LAMP_COLOR,
      (rgb >> 16),
      (rgb >> 8),
      (rgb)
  };
  bridge_tx_pkt(true, pkt, sizeof(pkt));
}

void bridge_lamp_set_intensity(uint8_t i) {
  uint8_t pkt[] = {
      P_STM_LAMP_INTENSITY,
      i
  };
  bridge_tx_pkt(true, pkt, sizeof(pkt));
}

void bridge_lamp_set_ena(bool ena) {
  uint8_t pkt[] = {
      P_STM_LAMP_ENA,
      ena
  };
  bridge_tx_pkt(true, pkt, sizeof(pkt));
}

void bridge_lamp_set_status(bool ena, uint8_t intensity, uint32_t rgb) {
  uint8_t pkt[] = {
      P_STM_LAMP_STATUS,
      ena,
      intensity,
      (rgb >> 16),
      (rgb >> 8),
      (rgb)
  };
  bridge_tx_pkt(true, pkt, sizeof(pkt));
}

int bridge_lamp_ask_status(void) {
  uint8_t pkt[] = {
      P_STM_LAMP_GET_STATUS
  };
  return bridge_tx_pkt(true, pkt, sizeof(pkt));
}

lamp_status *bridge_lamp_get_status(bool refresh_syncronously) {
  if (refresh_syncronously) {
    sync_seqno = bridge_lamp_ask_status();
    if (sync_seqno > 0) {
      uint32_t msg;
      printf("sync recv pkt %i\n", sync_seqno);
      xQueueReceive(syncq, &msg, 1000/portTICK_RATE_MS);
    }
  }
  return &lamp;
}

///////////////////////////////////////////////////////////

void bridge_pkt_acked(uint8_t seqno, uint8_t *data, uint16_t len) {
  printf("ack pkt %i, cmd %02x, len %i\n", seqno, data[0], len);
  if (len == 0) return;
  switch (data[0]) {
  case P_STM_HELLO: {
    uint32_t ping_val_rcv =
        (data[1]<<24) |
        (data[2]<<16) |
        (data[3]<<8) |
        data[4];
    if (ping_val == ping_val_rcv) {
      printf("PONG ok\n");
    } else {
      printf("PONG bad\n");
    }
    break;
  }
  case P_STM_LAMP_GET_ENA:
    lamp.ena = data[1] != 0;
    break;
  case P_STM_LAMP_GET_INTENSITY:
    lamp.intensity = data[1];
    break;
  case P_STM_LAMP_GET_COLOR:
    lamp.rgb = (data[1] << 16) | (data[2] << 8) | (data[3]);
    break;
  case P_STM_LAMP_GET_STATUS:
    lamp.ena = data[1] != 0;
    lamp.intensity = data[2];
    lamp.rgb = (data[3] << 16) | (data[4] << 8) | (data[5]);
    printf("lamp ena:%i int:%i rgb:%06x\n", lamp.ena, lamp.intensity, lamp.rgb);
    break;

  default:
    break;
  }

  if (sync_seqno > 0 && sync_seqno == seqno) {
    xQueueSend(syncq, &sync_seqno, 0);
    printf("sync notify pkt %i\n", sync_seqno);
    sync_seqno = 0;
  }

}

///////////////////////////////////////////////////////////

void bridge_rx_pkt(umac_pkt *pkt, bool resent) {
  printf("rx pkt %i, cmd %02x, len %i\n", pkt->seqno, pkt->data[0], pkt->length);
  if (pkt->length == 0) return;
  uint8_t *data = pkt->data;
  switch(data[0]) {
  case P_ESP_HELLO:
    bridge_tx_reply(data, pkt->length);
  break;
  case P_ESP_AP_SCAN:
    if (resent) break;
    systask_call(SYS_WIFI_SCAN_DBG, false);
  break;
  case P_ESP_SEND_UDP:
    if (resent) break;
    udputil_config(
        (data[1] << 24) | (data[2] << 16) | (data[3] << 8) | data[4], // ip
        (data[5] << 8) | (data[6]),                                   // port
        0,                                                            // timeout
        &data[7],                                                     // txdata
        pkt->length - 8,                                              // len,
        NULL,                                                         // rxdata
        NULL                                                          // recv_cb
      );
    systask_call(SYS_UDP_SEND, false);
  break;
  case P_ESP_RECV_UDP:
    if (resent) break;
    udputil_config(
        (data[1] << 24) | (data[2] << 16) | (data[3] << 8) | data[4], // ip
        (data[5] << 8) | (data[6]),                                   // port
        (data[7] << 8) | (data[8]),                                   // timeout
        NULL,                                                         // txdata
        0,                                                            // len,
        udp_rx.udp_rx_buf,                                            // rxdata
        bridge_udp_recv_cb                                            // recv_cb
      );
    systask_call(SYS_UDP_RECV, false);
  break;
  case P_ESP_SEND_RECV_UDP:
    if (resent) break;
    udputil_config(
        (data[1] << 24) | (data[2] << 16) | (data[3] << 8) | data[4], // ip
        (data[5] << 8) | (data[6]),                                   // port
        (data[7] << 8) | (data[8]),                                   // timeout
        &data[9],                                                     // txdata
        pkt->length - 10,                                             // len,
        udp_rx.udp_rx_buf,                                            // rxdata
        bridge_udp_recv_cb                                            // recv_cb
      );
    systask_call(SYS_UDP_SEND_RECV, false);
    break;
  case P_ESP_AP_CFG:
    if (resent) break;
    uint32_t ssid_len = data[1];
    uint32_t pass_len = data[1 + ssid_len + 1];
    uint8_t *ssid = &data[2];
    uint8_t *pass = &data[1 + ssid_len + 1 + 1];
    {
      uint8_t nl = '\n';
      fs_clearerr();
      spiffs_file fd = fs_open(".ssid", SPIFFS_O_CREAT | SPIFFS_O_TRUNC | SPIFFS_O_WRONLY, 0);
      if (fd >= 0) {
        fs_write(fd, ssid, ssid_len);
        fs_write(fd, &nl, 1);
        fs_write(fd, pass, pass_len);
        fs_write(fd, &nl, 1);
        fs_close(fd);
      } else {
        printf("could not create credential file\n");
      }
      printf("fs res: %i\n", fs_errno());
    }
  break;


  }
}

///////////////////////////////////////////////////////////

void bridge_timeout(umac_pkt *pkt) {
  if (pkt->length == 0) return;
  uint8_t *data = pkt->data;
  switch(data[0]) {
  case P_ESP_HELLO:
    printf("PING missed\n");
    break;
  default:
    break;
  }
}

int bridge_tx_pkt(uint8_t ack, uint8_t *buf, uint16_t len) {
  return _impl_umac_tx_pkt(ack, buf, len);
}

void bridge_tx_reply(uint8_t *buf, uint16_t len) {
  (void)_impl_umac_reply_pkt(buf, len);
}

void bridge_init(void) {
  syncq = xQueueCreate(1, sizeof(uint32_t));
  sync_seqno = -1;
}

///////////////////////////////////////////////////////////

static void bridge_udp_recv_cb(int res, uint32_t ip, uint8_t *buf, uint16_t len) {
  if (res < 0) return;
  udp_rx.udp_pkt_preamble[0] = P_STM_RECV_UDP;
  udp_rx.udp_pkt_preamble[1] = ip >> 24;
  udp_rx.udp_pkt_preamble[2] = ip >> 16;
  udp_rx.udp_pkt_preamble[3] = ip >> 8;
  udp_rx.udp_pkt_preamble[4] = ip;
  bridge_tx_pkt(false, (uint8_t *)&udp_rx, len+sizeof(udp_rx.udp_pkt_preamble));
}
