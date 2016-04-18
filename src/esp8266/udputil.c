/*
 * udputil.c
 *
 *  Created on: Apr 18, 2016
 *      Author: petera
 */

#include <stdint.h>
#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <esp8266.h>
#include <stdio.h>
#include <ntp.h>
#include "espressif/esp_common.h"
#include "lwip/api.h"
#include "lwip/err.h"
#include "lwip/sockets.h"


static struct {
  uint16_t port;
  uint16_t timeout;
  uint32_t ipaddr;
  uint8_t *data;
  uint16_t len;
} udp_cfg;

void udputil_config(uint32_t ipaddr, uint16_t port, u16_t timeout, uint8_t *data, uint16_t len) {
  udp_cfg.ipaddr = ipaddr;
  udp_cfg.port = port;
  udp_cfg.timeout = timeout;
  udp_cfg.data = data;
  udp_cfg.len = len;
}

int udputil_send(void) {
  uint8_t *buf = udp_cfg.data;
  const uint16_t len = udp_cfg.len;
  const int port = udp_cfg.port;
  const int timeout = udp_cfg.timeout;
  ip_addr_t dst_address;

  dst_address.addr = htonl(udp_cfg.ipaddr);

  int sock_fd = lwip_socket(AF_INET, SOCK_DGRAM, 0);
  if (sock_fd < 0) {
    printf("idp socket alloc failed\n");
    return -1;
  }

  struct sockaddr_in local;
  memset(&local, 0, sizeof(local));
  local.sin_family = AF_INET;
  local.sin_addr.s_addr = htonl(INADDR_ANY);
  local.sin_port = htons(INADDR_ANY);

  if (lwip_bind(sock_fd, (struct sockaddr *) &local, sizeof(local) < 0)) {
    printf("bind failed\n");
    lwip_close(sock_fd);
    return -1;
  }

  if (timeout) {
    lwip_setsockopt(sock_fd, SOL_SOCKET, SO_RCVTIMEO, (char *) &timeout,
        sizeof(timeout));
  }

  int broadcast_perm = 1;
  if ((dst_address.addr == 0xffffffff)) {
    printf("UDP broadcast\n");
    if (setsockopt(sock_fd, SOL_SOCKET, SO_BROADCAST, (void *)&broadcast_perm, sizeof(broadcast_perm)) < 0) {
      printf("set broadcast perm failed\n");
      lwip_close(sock_fd);
      return -1;
    }
  }

  struct sockaddr_in remote;
  memset(&remote, 0, sizeof(remote));
  remote.sin_family = AF_INET;
  remote.sin_port = htons(port);
  inet_addr_from_ipaddr(&remote.sin_addr, &dst_address);

  printf("sendto %08x:%i\n", dst_address.addr, port);
  if (!lwip_sendto(sock_fd, buf, len, 0,
      (struct sockaddr *) &remote, sizeof(remote))) {
    printf("sendto failed\n");
    closesocket(sock_fd);
    return -1;
  }

  closesocket(sock_fd);
  return 0;
}
