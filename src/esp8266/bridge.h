/*
 * bridge.h
 *
 *  Created on: Apr 13, 2016
 *      Author: petera
 */

#ifndef _BRIDGE_H_
#define _BRIDGE_H_

#include "../umac/umac.h"

void bridge_set_color(uint32_t rgb);


void bridge_rx_pkt(umac_pkt *pkt, bool resent);

void bridge_pkt_acked(uint8_t seqno, uint8_t *data, uint16_t len);

void bridge_timeout(umac_pkt *pkt);

void bridge_tx_pkt(uint8_t ack, uint8_t *buf, uint16_t len);

void bridge_tx_reply(uint8_t *buf, uint16_t len);

int _impl_umac_tx_pkt(uint8_t ack, uint8_t *buf, uint16_t len);
int _impl_umac_reply_pkt(uint8_t *buf, uint16_t len);

#endif /* _BRIDGE_H_ */
