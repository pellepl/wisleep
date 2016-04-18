/*
 * udputil.h
 *
 *  Created on: Apr 18, 2016
 *      Author: petera
 */

#ifndef _ESP8266_UDPUTIL_H_
#define _ESP8266_UDPUTIL_H_

typedef void (* udp_recv_cb_fn)(int res, uint8_t *buf, uint16_t len);

void udputil_config(uint32_t ipaddr, uint16_t port, uint16_t timeout, uint8_t *data, uint16_t len);
int udputil_recv(udp_recv_cb_fn recv_cb);
void udputil_send(void);

#endif /* _ESP8266_UDPUTIL_H_ */
