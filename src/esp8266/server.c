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
#include "octet_spiflash.h"
#include "fs.h"

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
  str->avail_sz = 256;
  str->user = (void *)((intptr_t)sockfd);
  str->read = sockstr_read;
  str->write = sockstr_write;
  return str;
}

static UW_STREAM make_null_stream(UW_STREAM str) {
  str->total_sz = 0;
  str->avail_sz = 0;
  str->read = 0;
  str->write = 0;
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
static UW_STREAM make_char_stream(UW_STREAM str, const char *txt) {
  str->total_sz = strlen(txt);
  str->avail_sz = str->total_sz;
  str->read = charstr_read;
  str->write = 0;
  str->user = (void *)txt;
  return str;
}

#define STREAM_CHUNK_SZ      32 //UWEB_TX_MAX_LEN

static int32_t spifstr_read(UW_STREAM str, uint8_t *dst, uint32_t len) {
  uint32_t addr = (uint32_t)(intptr_t)str->user;
  len = str->avail_sz < len ? str->avail_sz : len;
  len = str->total_sz < len ? str->total_sz : len;
  (void)octet_spif_read(addr, dst, len);
  str->total_sz -= len;
  str->avail_sz = STREAM_CHUNK_SZ < str->total_sz ? STREAM_CHUNK_SZ : str->total_sz;
  str->user = (void *)(intptr_t)(addr + len);
  return len;
}
static UW_STREAM make_spif_stream(UW_STREAM str, uint32_t addr, uint32_t len) {
  str->total_sz = len;
  str->avail_sz = len < STREAM_CHUNK_SZ ? len : STREAM_CHUNK_SZ;
  str->read = spifstr_read;
  str->write = 0;
  str->user = (void *)(intptr_t)addr;
  return str;
}


static int32_t filestr_read(UW_STREAM str, uint8_t *dst, uint32_t len) {
  spiffs_file fd  = (spiffs_file)(intptr_t)str->user;
  len = str->avail_sz < len ? str->avail_sz : len;
  len = str->total_sz < len ? str->total_sz : len;
  int32_t res = fs_read(fd, dst, len);
  if (res < 0) {
    printf("err reading file fd=%i, fs err %i\n", fd, fs_errno());
    fs_close(fd);
    return -1;
  }
  str->total_sz -= len;
  str->avail_sz = STREAM_CHUNK_SZ < str->total_sz ? STREAM_CHUNK_SZ : str->total_sz;
  if (str->total_sz == 0) {
    printf("closing file fd=%i\n", fd);
    fs_close(fd);
  }
  return len;
}
static UW_STREAM make_file_stream(UW_STREAM str, spiffs_file fd) {
  spiffs_stat stat;
  int32_t res = fs_fstat(fd, &stat);
  uint32_t len = 0;
  if (res == SPIFFS_OK) {
    len = stat.size;
  }
  str->total_sz = len;
  str->avail_sz = len < STREAM_CHUNK_SZ ? len : STREAM_CHUNK_SZ;
  str->read = filestr_read;
  str->write = 0;
  str->user = (void *)(intptr_t)fd;
  return str;
}

static const char *UPLOAD =
    "<!DOCTYPE html>"
    "<html><body>"
    "<form action=\"uploadfile\" method=\"post\" enctype=\"multipart/form-data\">"
    "<fieldset>"
    "<legend>Upload file</legend>"
    "<input type=\"file\" name=\"upfile\" id=\"upfile\">"
    "<input type=\"submit\" value=\"Upload\" name=\"submit\">"
    "</fieldset>"
    "</form>"
    "</body></html>";

static char extra_hdrs[128];

static uweb_response uweb_resp(uweb_request_header *req, UW_STREAM *res,
    uweb_http_status *http_status, char *content_type,
    char **extra_headers) {
  if (req->chunk_nbr == 0) {
    printf("opening %s\n", &req->resource[1]);
    WDT.FEED = WDT_FEED_MAGIC;

    if (strcmp(req->resource, "/index.html") == 0 || strlen(req->resource) == 1) {
      // --- index.html
      spiffs_file fd = fs_open("index.html", SPIFFS_RDONLY, 0);
      if (fd < 0) {
        make_null_stream(&res_str);
        *http_status = S404_NOT_FOUND;
      } else {
        printf("opening fs file %s (fd=%i)\n", &req->resource[1], fd);
        make_file_stream(&res_str, fd);
      }

    } else if (strcmp(req->resource, "/upload") == 0) {
      // --- upload page
      make_char_stream(&res_str, UPLOAD);

    } else if (strcmp(req->resource, "/uploadfile") == 0) {
        // --- upload page
      make_null_stream(&res_str);

    } else if (strstr(req->resource, "/spiflash") == req->resource) {
      // --- read spiflash
      sprintf(content_type, "application/octet-stream");
      uint32_t addr = 0;
      uint32_t len = sdk_flashchip.chip_size;
      char *addr_ix, *len_ix;
      if ((addr_ix = strstr(req->resource, "addr="))) {
        addr = strtol(addr_ix+5, NULL, 0);
      }
      if ((len_ix = strstr(req->resource, "len="))) {
        len = strtol(len_ix+4, NULL, 0);
      }
      if (addr >= sdk_flashchip.chip_size) addr = sdk_flashchip.chip_size - 1;
      if (addr + len >= sdk_flashchip.chip_size) len = sdk_flashchip.chip_size - 1 - addr;
      snprintf(extra_hdrs, sizeof(extra_hdrs),
          "Content-Disposition: attachment; filename=\"spifl_dump@%08x_%i.raw\"\n", addr, len);
      *extra_headers = extra_hdrs;
      printf("dumping spiflash addr:0x%08x len:%i\n", addr, len);

      make_spif_stream(&res_str, addr, len);

    } else {
      // --- unknown resource
      spiffs_file fd = fs_open(&req->resource[1], SPIFFS_RDONLY, 0);
      if (fd < 0) {
        make_null_stream(&res_str);
        *http_status = S404_NOT_FOUND;
      } else {
        printf("opening fs file %s (fd=%i)\n", &req->resource[1], fd);
        make_file_stream(&res_str, fd);
      }
    }
  }
  *res = &res_str;

  return UWEB_CHUNKED;
}

static spiffs_file cur_upload_fd = 0;

static void uweb_data(uweb_request_header *req, uweb_data_type type,
    uint32_t offset, uint8_t *data, uint32_t length) {
  printf("data recv offs:%i len:%i\n"
         "     REQ meth:%i conn:%s type:%s chunked:%i chunk#:%i\n"
         "     MLP nbr:%i content:%s  disp:%s\n",
      offset, length,
      req->method, req->connection, req->content_type, req->chunked, req->chunk_nbr,
      req->cur_multipart.multipart_nbr, req->cur_multipart.content_type, req->cur_multipart.content_disp);

  // check if upload file request
  if (strstr(req->cur_multipart.content_disp, "name=\"upfile\"")) {
    char *fname, *fname_end;
    if (cur_upload_fd > 0 && data == 0 && length == 0) {
      printf("closing uploaded file\n");
      fs_close(cur_upload_fd);
      cur_upload_fd = 0;
    } else if (req->cur_multipart.multipart_nbr == 0 &&
        (fname = strstr(req->cur_multipart.content_disp, "filename=\"")) &&
        (fname_end = strchr(fname + 10, '\"'))) {
      fname += 10; //filename="
      *fname_end = 0;
      printf("request to save file %s\n", fname);
      cur_upload_fd = fs_open(fname, SPIFFS_RDWR | SPIFFS_CREAT | SPIFFS_TRUNC, 0);
      if (cur_upload_fd > 0) {
        int32_t res = fs_write(cur_upload_fd, data, length);
        if (res < SPIFFS_OK) {
          printf("error saving upload data err %i\n", fs_errno());
          fs_close(cur_upload_fd);
          cur_upload_fd = 0;
        }
      }
    } else if (cur_upload_fd > 0) {
      int32_t res = fs_write(cur_upload_fd, data, length);
      if (res < SPIFFS_OK) {
        printf("error saving upload data err %i\n", fs_errno());
        fs_close(cur_upload_fd);
        cur_upload_fd = 0;
      }
    }
  }

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
    lwip_close(client_fd);
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



