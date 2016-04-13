/*
 * bridge.c
 *
 *  Created on: Apr 13, 2016
 *      Author: petera
 */

#include "bridge.h"
#include <stdint.h>
#include <sys/types.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>

#include "../protocol.h"

static lamp_status lamp;

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

void bridge_lamp_ask_status(void) {
  uint8_t pkt[] = {
      P_STM_LAMP_GET_STATUS
  };
  bridge_tx_pkt(true, pkt, sizeof(pkt));
}

lamp_status *bridge_lamp_get_status(void) {
  return &lamp;
}

/////////////////////////////////////////////////////

void bridge_pkt_acked(uint8_t seqno, uint8_t *data, uint16_t len) {
  switch (data[0]) {
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
    break;
  default:
    break;
  }

}

/////////////////////////////////////////////////////

void bridge_rx_pkt(umac_pkt *pkt, bool resent) {

}

/////////////////////////////////////////////////////

void bridge_timeout(umac_pkt *pkt) {

}

void bridge_tx_pkt(uint8_t ack, uint8_t *buf, uint16_t len) {
  (void)_impl_umac_tx_pkt(ack, buf, len);
}

void bridge_tx_reply(uint8_t *buf, uint16_t len) {
  (void)_impl_umac_reply_pkt(buf, len);
}
