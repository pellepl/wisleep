/*
 * udputil.c
 *
 *  Created on: Apr 18, 2016
 *      Author: petera
 */

#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <esp8266.h>
#include <stdio.h>
#include <udputil.h>
#include <netdb.h>
#include "espressif/esp_common.h"
#include "lwip/api.h"
#include "lwip/err.h"
#include "lwip/sockets.h"

#define RECV_MAX_LEN  512

static struct {
  uint16_t port;
  uint16_t timeout;
  uint32_t ipaddr;
  uint8_t *txdata;
  uint16_t len;
  uint8_t *rxdata;
  udp_recv_cb_fn recv_cb;
} udp_cfg;

void udputil_config(uint32_t ipaddr, uint16_t port, u16_t timeout,
    uint8_t *txdata, uint16_t len,
    uint8_t *rxdata, udp_recv_cb_fn recv_cb) {
  udp_cfg.ipaddr = ipaddr;
  udp_cfg.port = port;
  udp_cfg.timeout = timeout;
  udp_cfg.txdata = txdata;
  udp_cfg.len = len;
  udp_cfg.rxdata = rxdata;
  udp_cfg.recv_cb = recv_cb;
}

int _udputil_txrx(bool tx, bool rx) {
  uint8_t *txbuf = udp_cfg.txdata;
  uint8_t *rxbuf = udp_cfg.rxdata;
  const uint16_t len = udp_cfg.len;
  const int port = udp_cfg.port;
  const int timeout = udp_cfg.timeout;
  ip_addr_t dst_address;

  dst_address.addr = htonl(udp_cfg.ipaddr);

  struct sockaddr_in local;
  memset(&local, 0, sizeof(local));
  local.sin_family = AF_INET;
  local.sin_addr.s_addr = htonl(INADDR_ANY);
  local.sin_port = htons(port);

  int sock_fd = lwip_socket(AF_INET, SOCK_DGRAM, 0);
  if (sock_fd < 0) {
    printf("socket alloc failed\n");
    if (rx && udp_cfg.recv_cb) udp_cfg.recv_cb(-1,0,NULL,0);
    return -1;
  }

  if (lwip_bind(sock_fd, (struct sockaddr *)&local, sizeof(local) < 0)) {
    printf("bind failed\n");
    lwip_close(sock_fd);
    if (rx && udp_cfg.recv_cb) udp_cfg.recv_cb(-1,0,NULL,0);
    return -1;
  }

  if (timeout) {
    lwip_setsockopt(sock_fd, SOL_SOCKET, SO_RCVTIMEO, (char *) &timeout,
        sizeof(timeout));
  }

  int broadcast_perm = 1;
  if ((dst_address.addr == 0xffffffff)) {
    if (lwip_setsockopt(sock_fd, SOL_SOCKET, SO_BROADCAST, (void *)&broadcast_perm, sizeof(broadcast_perm)) < 0) {
      printf("set broadcast perm failed\n");
      lwip_close(sock_fd);
      if (rx && udp_cfg.recv_cb) udp_cfg.recv_cb(-1,0,NULL,0);
      return -1;
    }
  }

  struct sockaddr_in remote;
  memset(&remote, 0, sizeof(remote));
  remote.sin_family = AF_INET;
  remote.sin_port = htons(port);
  inet_addr_from_ipaddr(&remote.sin_addr, &dst_address);

  if (tx) {
    if (!lwip_sendto(sock_fd, txbuf, len, 0,
        (struct sockaddr *) &remote, sizeof(remote))) {
      printf("sendto failed\n");
      closesocket(sock_fd);
      if (rx && udp_cfg.recv_cb) udp_cfg.recv_cb(-1,0,NULL,0);
      return -1;
    }
  }

  int size = 0;
  if (rx) {
    int tolen = sizeof(remote);
    size = lwip_recvfrom(sock_fd, rxbuf, RECV_MAX_LEN, 0,
        (struct sockaddr *) &remote, (socklen_t *) &tolen);
    if (size <= 0) {
      printf("recv failed %i\n", size);
      closesocket(sock_fd);
      if (rx && udp_cfg.recv_cb) udp_cfg.recv_cb(-1,0,NULL,0);
      return -1;
    }
  }

  closesocket(sock_fd);

  if (rx && udp_cfg.recv_cb) udp_cfg.recv_cb(0, remote.sin_addr.s_addr, udp_cfg.rxdata, size);

  return 0;
}

int udputil_recv(void) {
  return _udputil_txrx(false, true);
}

int udputil_send(void) {
  return _udputil_txrx(true, false);
}

int udputil_send_recv(void){
  return _udputil_txrx(true, true);
}

