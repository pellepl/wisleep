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

void bridge_set_color(uint32_t rgb) {
  uint8_t pkt[] = {
      P_STM_LAMP_COLOR,
      (rgb >> 16),
      (rgb >> 8),
      (rgb)
  };
  bridge_tx_pkt(true, pkt, sizeof(pkt));
}

void bridge_rx_pkt(umac_pkt *pkt, bool resent) {

}

void bridge_pkt_acked(uint8_t seqno, uint8_t *data, uint16_t len) {

}

void bridge_timeout(umac_pkt *pkt) {

}

void bridge_tx_pkt(uint8_t ack, uint8_t *buf, uint16_t len) {
  (void)_impl_umac_tx_pkt(ack, buf, len);
}

void bridge_tx_reply(uint8_t *buf, uint16_t len) {
  (void)_impl_umac_reply_pkt(buf, len);
}
