/*
 * udputil.h
 *
 *  Created on: Apr 18, 2016
 *      Author: petera
 */

#ifndef _ESP8266_UDPUTIL_H_
#define _ESP8266_UDPUTIL_H_

typedef void (* udp_recv_cb_fn)(int res, uint32_t ip, uint8_t *buf, uint16_t len);

void udputil_config(uint32_t ipaddr, uint16_t port, uint16_t timeout,
    uint8_t *txdata, uint16_t len,
    uint8_t *rxdata, udp_recv_cb_fn recv_cb);
int udputil_recv(void);
int udputil_send(void);
int udputil_send_recv(void);

#endif /* _ESP8266_UDPUTIL_H_ */
