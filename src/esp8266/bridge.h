/*
 * bridge.h
 *
 *  Created on: Apr 13, 2016
 *      Author: petera
 */

#ifndef _BRIDGE_H_
#define _BRIDGE_H_

#include <stdint.h>
#include <sys/types.h>
#include <stdbool.h>

#include "../umac/umac.h"

typedef struct {
  bool ena;
  uint8_t intensity;
  uint32_t rgb;
} lamp_status;

void bridge_lamp_set_ena(bool ena);
void bridge_lamp_set_intensity(uint8_t i);
void bridge_lamp_set_color(uint32_t rgb);
void bridge_lamp_set_status(bool ena, uint8_t intensity, uint32_t rgb);
void bridge_lamp_ask_status(void);
lamp_status *bridge_lamp_get_status(void);

void bridge_rx_pkt(umac_pkt *pkt, bool resent);

void bridge_pkt_acked(uint8_t seqno, uint8_t *data, uint16_t len);

void bridge_timeout(umac_pkt *pkt);

void bridge_tx_pkt(uint8_t ack, uint8_t *buf, uint16_t len);

void bridge_tx_reply(uint8_t *buf, uint16_t len);

int _impl_umac_tx_pkt(uint8_t ack, uint8_t *buf, uint16_t len);
int _impl_umac_reply_pkt(uint8_t *buf, uint16_t len);

#endif /* _BRIDGE_H_ */
