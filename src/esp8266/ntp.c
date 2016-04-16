/*
 * ntp.c
 *
 *  Created on: Apr 16, 2016
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

#define NTP_MSG_LEN 48

#define NTP_MODE_CLIENT 3
#define NTP_VERSION 3
#define TRANSMIT_TIME_OFFSET 40
#define REFERENCE_TIME_OFFSET 16
#define ORIGINATE_TIME_OFFSET 24
#define RECEIVE_TIME_OFFSET 32
#define OFFSET_1900_TO_1970 ((uint64_t)((365 * 70) + 17) * 24 * 60 * 60)

static char ntp_host[32];

static uint32_t read32(char* buffer, int offset) {
  char b0 = buffer[offset];
  char b1 = buffer[offset + 1];
  char b2 = buffer[offset + 2];
  char b3 = buffer[offset + 3];

  // convert signed bytes to unsigned values
  uint32_t i0 = ((b0 & 0x80) == 0x80 ? (b0 & 0x7F) + 0x80 : b0);
  uint32_t i1 = ((b1 & 0x80) == 0x80 ? (b1 & 0x7F) + 0x80 : b1);
  uint32_t i2 = ((b2 & 0x80) == 0x80 ? (b2 & 0x7F) + 0x80 : b2);
  uint32_t i3 = ((b3 & 0x80) == 0x80 ? (b3 & 0x7F) + 0x80 : b3);

  uint32_t v = (i0 << 24) + (i1 << 16) + (i2 << 8) + i3;
  return v;
}

static uint64_t read_timestamp(char *buffer, int offset) {
  uint32_t seconds = read32(buffer, offset);
  uint32_t fraction = read32(buffer, offset + 4);
  uint64_t v = ((int64_t) seconds - OFFSET_1900_TO_1970) * 1000
      + (int64_t) fraction * 1000 / (int64_t) 0x100000000;
  return v;
}

void ntp_set_host(const char *hostname) {
  strncpy(ntp_host, hostname, sizeof(ntp_host));
}

void ntp_query(void) {
  //char hostname[] = "216.229.0.179";
  const int portno = 123;
  const int timeout = 5000;

  ip_addr_t ntp_server_address;

  if (!ipaddr_aton(ntp_host, &ntp_server_address)) {
    printf("ip addr failed\n");
    return;
  }

  printf("socket alloc\n");
  int sock_fd = lwip_socket(AF_INET, SOCK_DGRAM, 0);
  if (sock_fd < 0) {
    printf("socket alloc failed\n");
    return;
  }

  struct sockaddr_in local;
  memset(&local, 0, sizeof(local));
  local.sin_family = AF_INET;
  local.sin_addr.s_addr = htonl(INADDR_ANY);
  local.sin_port = htons(INADDR_ANY);

  printf("bind\n");
  if (lwip_bind(sock_fd, (struct sockaddr *) &local, sizeof(local) < 0)) {
    printf("bind failed\n");
    lwip_close(sock_fd);
    return;
  }

  lwip_setsockopt(sock_fd, SOL_SOCKET, SO_RCVTIMEO, (char *) &timeout,
      sizeof(timeout));

  struct sockaddr_in remote;
  memset(&remote, 0, sizeof(remote));
  remote.sin_family = AF_INET;
  remote.sin_port = htons(portno);
  inet_addr_from_ipaddr(&remote.sin_addr, &ntp_server_address);

  char ntpmsg[NTP_MSG_LEN];
  memset(ntpmsg, 0, sizeof(ntpmsg));
  ntpmsg[0] = NTP_MODE_CLIENT | (NTP_VERSION << 3);

  printf("sendto %08x\n", ntp_server_address.addr);
  if (!lwip_sendto(sock_fd, ntpmsg, sizeof(ntpmsg), 0,
      (struct sockaddr *) &remote, sizeof(remote))) {
    printf("sendto failed\n");
    closesocket(sock_fd);
    return;
  }

  char ntprsp[NTP_MSG_LEN];
  int tolen = sizeof(remote);
  printf("recv\n");
  int size = lwip_recvfrom(sock_fd, &ntprsp, NTP_MSG_LEN, 0,
      (struct sockaddr *) &remote, (socklen_t *) &tolen);
  if (size != NTP_MSG_LEN) {
    printf("recv failed %i\n", size);
    closesocket(sock_fd);
    return;
  }

  printf("got data from ntp:");
  int i;
  for (i = 0; i < NTP_MSG_LEN; i++) {
    printf("%02x ", ntprsp[i]);
  }
  printf("\n");

  closesocket(sock_fd);

  uint64_t originate_time = read_timestamp(ntprsp, ORIGINATE_TIME_OFFSET);
  uint64_t receive_time = read_timestamp(ntprsp, RECEIVE_TIME_OFFSET);
  uint64_t transmit_time = read_timestamp(ntprsp, TRANSMIT_TIME_OFFSET);

  printf("originate:%i\n", (uint32_t)(originate_time >> 32));
  printf("receive:  %i\n", (uint32_t)(receive_time >> 32));
  printf("transmit: %i\n", (uint32_t)(transmit_time >> 32));
}

