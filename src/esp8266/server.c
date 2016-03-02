/*
 * server.c
 *
 *  Created on: Mar 2, 2016
 *      Author: petera
 */

#include "server.h"
#include "lwip/api.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "../uweb/src/uweb.h"
#include "../uweb/src/uweb_http.h"

static uweb_data_stream socket_str, res_str;

static int32_t sockstr_read(UW_STREAM str, uint8_t *dst, uint32_t len) {
  int32_t r = recv((intptr_t)str->user, dst, len, 0);
  return r;
}

static int32_t sockstr_write(UW_STREAM str, uint8_t *src, uint32_t len) {
  int l = 0;
  while (len) {
    l = lwip_write((intptr_t)str->user, src, len);
    if (l < 0) break;
    len -= l;
  }
  return l;
}

static UW_STREAM make_socket_stream(UW_STREAM str, int sockfd) {
  str->total_sz = -1;
  str->capacity_sz = 0;
  str->avail_sz = 256;
  str->user = (void *)((intptr_t)sockfd);
  str->read = sockstr_read;
  str->write = sockstr_write;
  return str;
}



static int32_t nullstr_read(UW_STREAM str, uint8_t *dst, uint32_t len) {
  return 0;
}
static int32_t nullstr_write(UW_STREAM str, uint8_t *src, uint32_t len) {
  return 0;
}
static UW_STREAM make_null_stream(UW_STREAM str) {
  str->total_sz = 0;
  str->capacity_sz = 0;
  str->avail_sz = 0;
  str->read = nullstr_read;
  str->write = nullstr_write;
  return str;
}


static int32_t charstr_read(UW_STREAM str, uint8_t *dst, uint32_t len) {
  const char *txt = (const char *)str->user;
  len = len < str->avail_sz ? len : str->avail_sz;
  memcpy(dst, txt, len);
  str->avail_sz -= len;
  str->user += len;
  return len;
}
static int32_t charstr_write(UW_STREAM str, uint8_t *src, uint32_t len) {
  return 0;
}
static UW_STREAM make_char_stream(UW_STREAM str, const char *txt) {
  str->total_sz = strlen(txt);
  str->capacity_sz = 0;
  str->avail_sz = str->total_sz;
  str->read = charstr_read;
  str->write = charstr_write;
  str->user = (void *)txt;
  return str;
}


static const char *TEST = "Holy crap, it works!";

static uweb_response uweb_resp(uweb_request_header *req, UW_STREAM *res,
    uweb_http_status *http_status, char *content_type) {
  if (req->chunk_nbr == 0) {
    printf("opening %s\n", &req->resource[1]);
    if (strcmp(&req->resource[1], "index.html") == 0) {
      make_char_stream(&res_str, TEST);
    } else {
      make_null_stream(&res_str);
      *http_status = S404_NOT_FOUND;
    }
  }
  *res = &res_str;

  return UWEB_CHUNKED;
}

static void uweb_data(uweb_request_header *req, uweb_data_type type,
    uint32_t offset, uint8_t *data, uint32_t length) {

}

void server_task(void *pvParameters) {
  int sock_fd, client_fd;
  struct sockaddr_in sa, isa;

  UWEB_init(uweb_resp, uweb_data);
  sock_fd = lwip_socket(PF_INET, SOCK_STREAM, 0);
  if (sock_fd < 0) {
    printf("socket failed\n");
    return;
  }
  memset(&sa, 0, sizeof(struct sockaddr_in));
  sa.sin_family = AF_INET;
  sa.sin_addr.s_addr = INADDR_ANY;
  sa.sin_port = htons(80);

  if (lwip_bind(sock_fd, (struct sockaddr *)&sa, sizeof(sa) < 0)) {
    printf("bind failed\n");
    lwip_close(sock_fd);
    return;
  }

  lwip_listen(sock_fd, 3);
  socklen_t addr_size = sizeof(sa);
  printf("listening to port 80\n");
  while (true) {
    client_fd = lwip_accept(sock_fd, (struct sockaddr *)&isa, &addr_size);
    if (client_fd < 0) {
      printf("accept failed\n");
      lwip_close(sock_fd);
      return;
    }
    printf("client accepted\n");

    make_socket_stream(&socket_str, client_fd);
    UWEB_parse(&socket_str, &socket_str);

    printf("client closed\n");
    lwip_close(sock_fd);
  }


#if 0
  struct netconn *nc = netconn_new(NETCONN_TCP);
  if(!nc) {
    printf("Status monitor: Failed to allocate socket.\n");
    return;
  }
  printf("listening on port 80\n");
  netconn_bind(nc, IP_ADDR_ANY, 80);
  netconn_listen(nc);

  while(1) {
    struct netconn *client = NULL;
    err_t err = netconn_accept(nc, &client);

    if (err != ERR_OK) {
      if (client) {
        netconn_delete(client);
      }
      continue;
    }

    printf("connection\n");
    make_socket_stream(&socket_str, client);
    UWEB_parse(&socket_str, &socket_str);
    ip_addr_t client_addr;
    uint16_t port_ignore;
    netconn_peer(client, &client_addr, &port_ignore);

    char buf[80];
    snprintf(buf, sizeof(buf), "Uptime %d seconds\r\n",
       xTaskGetTickCount()*portTICK_RATE_MS/1000);
    netconn_write(client, buf, strlen(buf), NETCONN_COPY);
    snprintf(buf, sizeof(buf), "Free heap %d bytes\r\n", (int)xPortGetFreeHeapSize());
    netconn_write(client, buf, strlen(buf), NETCONN_COPY);
    snprintf(buf, sizeof(buf), "Your address is %d.%d.%d.%d\r\n\r\n",
             ip4_addr1(&client_addr), ip4_addr2(&client_addr),
             ip4_addr3(&client_addr), ip4_addr4(&client_addr));
    netconn_write(client, buf, strlen(buf), NETCONN_COPY);
    netconn_delete(client);
  }
#endif
}



