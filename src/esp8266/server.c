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
#include "systasks.h"

//#define RAW_SERV // TODO
//#define NETCONN_SERV
#define SOCK_SERV

struct part_def_s;

typedef uint32_t (* generate_partial_content_f)(struct part_def_s *part, uint32_t max_len, uint32_t part_nbr, uint8_t *dst);

typedef struct part_def_s {
  uint8_t content[256];
  generate_partial_content_f gen_fn;
  uint32_t nbr;
  uint32_t len;
  uint32_t ix;
  void *user;
} part_def;

static uint32_t _upload_sz;
static spiffs_file _upload_fd = 0;
static uweb_data_stream _stream_tcp, _stream_res;
static char *_busy_title;
static int _busy_progress;
static volatile uint8_t _server_busy_claims;

static int get_errno(int sock_fd) {
  int error;
  uint32_t optlen = sizeof(error);
  (void)lwip_getsockopt(sock_fd, SOL_SOCKET, SO_ERROR, &error, &optlen);
  return error;
}

#ifdef SOCK_SERV
static int32_t sockstr_read(UW_STREAM str, uint8_t *dst, uint32_t len) {
  int32_t r = recv((intptr_t)str->user, dst, len, 0);
  if (r < 0) {
    printf("sockstr_rd err:%i\n", get_errno((intptr_t)str->user));
  }
  return r;
}
static int32_t sockstr_write(UW_STREAM str, uint8_t *src, uint32_t len) {
  int l = 0;
  while (len) {
    l = lwip_write((intptr_t)str->user, src, len);
    if (l < 0) {
      printf("sockstr_wr err:%i\n", get_errno((intptr_t)str->user));
      break;
    }
    len -= l;
  }
  return l;
}
static UW_STREAM make_tcp_stream(UW_STREAM str, int sockfd) {
  str->total_sz = -1;
  str->avail_sz = 256;
  str->user = (void *)((intptr_t)sockfd);
  str->read = sockstr_read;
  str->write = sockstr_write;
  return str;
}
#endif // SOCK_SERV
#ifdef NETCONN_SERV
static struct netbuf *_nb = NULL;
static int32_t netconn_str_read(UW_STREAM str, uint8_t *dst, uint32_t len) {
  struct netconn *nc = (struct netconn *)str->user;
  int32_t r = -1;
  err_t err = ERR_OK;
  if (str->avail_sz == 0 || _nb == NULL) {
    printf("nc recv - ");
    err = netconn_recv(nc, &_nb);
    if (err == ERR_OK) {
      printf("ok, len %i\n", netbuf_len(_nb));
      str->total_sz = 0; // use as offset, safe as we're chunking always
      netbuf_first(_nb);
      str->avail_sz = netbuf_len(_nb);
    } else {
      printf("err %i\n", err);
      _nb = NULL;
      return -1;
    }
  }
  if (err == ERR_OK || _nb != NULL) {
    uint32_t rd_len = str->avail_sz < len ? str->avail_sz : len;
    r = netbuf_copy_partial(_nb, dst, rd_len, str->total_sz);
    if (r == 0) {
      _nb = NULL;
      str->avail_sz = 0;
      return -1;
    }
    str->total_sz += r;
    if (r > str->avail_sz) {
      str->avail_sz = 0;
    } else {
      str->avail_sz -= r;
    }
  }
  return r;
}
static int32_t netconn_str_write(UW_STREAM str, uint8_t *src, uint32_t len) {
  struct netconn *nc = (struct netconn *)str->user;
  int res = 0;
  res = netconn_write(nc, src, len, NETCONN_COPY);
  return res == ERR_OK ? len : res;
}
static UW_STREAM make_tcp_stream(UW_STREAM str, struct netconn *nc) {
  str->total_sz = -1;
  str->avail_sz = 256;
  str->user = (void *)(nc);
  str->read = netconn_str_read;
  str->write = netconn_str_write;
  return str;
}
#endif // NETCONN_SERV

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

#define STREAM_CHUNK_SZ      256 /* this seems to be an optimal length */ //UWEB_TX_MAX_LEN

static int32_t spifstr_read(UW_STREAM str, uint8_t *dst, uint32_t len) {
  //WDT.FEED = WDT_FEED_MAGIC;
  uint32_t addr = (uint32_t)(intptr_t)str->user;
  len = str->avail_sz < len ? str->avail_sz : len;
  len = str->total_sz < len ? str->total_sz : len;
  printf("dump spi flash @ %08x: %i\n", addr, len);
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


static part_def part;

static void part_update(UW_STREAM str) {
  part_def *part = (part_def *)str->user;
  part->ix = 0;
  part->len = part->gen_fn(part, sizeof(part->content), part->nbr, part->content);
  str->avail_sz = part->len;
  part->nbr++;
}

static int32_t partstr_read(UW_STREAM str, uint8_t *dst, uint32_t len) {
  part_def *part = (part_def *)str->user;
  if (part->ix < part->len) {
    uint32_t rd_len = len < (part->len - part->ix) ? len : (part->len - part->ix);
    memcpy(dst, &part->content[part->ix], rd_len);
    part->ix += rd_len;
    str->avail_sz -= str->avail_sz < rd_len ? str->avail_sz : rd_len;
    if (str->avail_sz == 0 && part->gen_fn) {
      part_update(str);
    }

    return rd_len;
  } else {
    return 0;
  }
}
static UW_STREAM make_partial_stream(UW_STREAM str, part_def *part, generate_partial_content_f fn, void *user) {
  str->user = part;
  part->nbr = 0;
  part->user = user;
  part->gen_fn = fn;
  part_update(str);
  str->total_sz = 0;
  str->read = partstr_read;
  str->write = NULL;
  return str;
}
static void close_partial_stream(part_def *part) {
  part->gen_fn = NULL;
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


///////////////////////////////////////////////////////////////////////////////


static uint32_t part_upload(part_def *part, uint32_t max_len, uint32_t part_nbr, uint8_t *dst) {
  uint32_t len = 0;
  if (part->nbr == 0) {
    if (_upload_fd) {
      sprintf((char *)dst,
        "<!DOCTYPE html>"
        "<html><body onload=\"setTimeout('location.reload(true);',1000);\">"
        "<fieldset>"
          "<legend>File upload</legend>"
          "%i bytes"
        "</fieldset>"
        "</body></html>", _upload_sz
       );
    } else {
      sprintf((char *)dst,
        "<!DOCTYPE html>"
        "<html><body onload=\"window.location.href ='ls';\">"
        "</body></html>");
    }
    len = strlen((char *)dst);
    close_partial_stream(part);
  }
  return len;
}

static uint32_t part_busy(part_def *part, uint32_t max_len, uint32_t part_nbr, uint8_t *dst) {
  uint32_t len = 0;
  if (part->nbr == 0) {
    sprintf((char *)dst,
        "<!DOCTYPE html>"
        "<html><body onload=\"setTimeout('location.reload(true);',500);\">"
        "<h2 align=\"center\">%s</h2>"
        "<progress align=\"center\" value=\"%i\" max=\"100\"></progress>"
        "</body></html>",
        _busy_title, _busy_progress
    );
    len = strlen((char *)dst);
    close_partial_stream(part);
  }
  return len;
}

static spiffs_DIR _ls_d;

static uint32_t part_ls(part_def *part, uint32_t max_len, uint32_t part_nbr, uint8_t *dst) {
  uint32_t len = 0;
  if (part->nbr == 0) {
    const char *LS_PRE =
        "<!DOCTYPE html>"
        "<html><body>"
        "<table style=\"border:1px solid\">"
        "<tr><td><b>NAME</b></td><td><b>SIZE</b></td><td><b>OBJID</b></td><td><b>DELETE</b></td></tr>";
    part->user = (void *)fs_opendir("/", &_ls_d);
    len = strlen(LS_PRE);
    memcpy(dst, LS_PRE, len);
  } else if (part->nbr == 0x10001) {
    sprintf((char *)dst,
        "<table border=0><tr><td>"
        "<form action=\"fschk\">"
        "<input type=\"submit\" value=\"FS check\">"
        "</form>"
        "</td>"
        );
    len = strlen((char *)dst);
  } else if (part->nbr == 0x10002) {
    sprintf((char *)dst,
      "<td>"
        "<button onclick=\"doform()\">Format</button>"
        "<script>"
        "function doform() {"
          "if (confirm(\"Really format?\")==true) {"
            "window.location.href='format';"
          "}"
        "}"
        "</script>"
      "</td></tr></table>"
      "</body></html>"
        );
    len = strlen((char *)dst);
    close_partial_stream(part);
  } else {
    spiffs_DIR *d = (spiffs_DIR *)part->user;
    struct spiffs_dirent e;
    struct spiffs_dirent *pe = &e;
    if ((pe = fs_readdir(d, pe))) {
      sprintf((char *)dst,
          "<tr><td><a href=\"%s\">"
          "%s</a></td>"
          "<td>%i</td>"
          "<td>%04x</td>"
          "<td><a href=\"/rm?name=%s\">X</a></td>"
          "</tr>",
          e.name, e.name, e.size, e.obj_id, e.name);
      len = strlen((char *)dst);
    } else {
      uint32_t total, used;
      fs_info(&total, &used);
      sprintf((char *)dst,
        "<tr><td colspan=\"4\">&nbsp;</td></tr>"
        "<tr><td>USED</td><td>%i</td><td>TOTAL</td><td>%i</td></tr>"
        "</table>",
      used, total);
      len = strlen((char *)dst);
      part->nbr = 0x10000;
    }
  }
  return len;
}


static const char *DEF_INDEX =
    "<!DOCTYPE html>"
    "<html><body>"
    "<h1>wisleep</h1>"
    "<link rel=\"icon\" href=\"favicon.ico\"/>"
    "<form action=\"spiflash\" method=\"get\">"
      "<fieldset>"
        "<legend>Dump spi flash</legend>"
        "Address:<br>"
        "<input type=\"text\" name=\"addr\" value=\"0x00000000\"><br>"
        "Length:<br>"
        "<input type=\"text\" name=\"len\" value=\"1024\"><br><br>"
        "<input type=\"submit\" value=\"Dump\">"
      "</fieldset>"
    "</form>"
    "<form action=\"uploadfile\" method=\"post\" enctype=\"multipart/form-data\">"
      "<fieldset>"
        "<legend>Upload file</legend>"
        "<input type=\"file\" name=\"upfile\" id=\"upfile\">"
        "<input type=\"submit\" value=\"Upload\" name=\"submit\">"
      "</fieldset>"
    "</form>"
    "<table border=0><tr>"
      "<td><form action=\"ls\">"
      "<input type=\"submit\" value=\"List Files\">"
      "</form></td>"
      "<td><form action=\"test\">"
      "<input type=\"submit\" value=\"Test\">"
      "</form></td>"
    "</tr></table>"
    "</body></html>"
        ;
static const char *NOTFOUND =
    "<!DOCTYPE html>"
    "<html><body>"
    "<h1 align=\"center\">404 Not found</h1>"
    "</body></html>";

///////////////////////////////////////////////////////////////////////////////


static char extra_hdrs[128];

static part_def part;

static uweb_response uweb_resp(uweb_request_header *req, UW_STREAM *res,
    uweb_http_status *http_status, char *content_type,
    char **extra_headers) {
  if (server_is_busy()) {
    if (req->chunk_nbr == 0) {
      printf("req BUSY %s\n", &req->resource[1]);
      make_partial_stream(&_stream_res, &part, part_busy, NULL);
    }

    *res = &_stream_res;

    return UWEB_CHUNKED;
  }


  if (req->chunk_nbr == 0) {
    printf("req %s\n", &req->resource[1]);
    make_null_stream(&_stream_res);
    WDT.FEED = WDT_FEED_MAGIC;

    if (strcmp(req->resource, "/index.html") == 0 || strlen(req->resource) == 1) {
      // --- index.html
      spiffs_file fd = fs_open("index.html", SPIFFS_RDONLY, 0);
      if (fd < 0) {
        make_char_stream(&_stream_res, DEF_INDEX);
      } else {
        printf("opening fs file %s (fd=%i)\n", &req->resource[1], fd);
        make_file_stream(&_stream_res, fd);
      }

    } else if (strstr(req->resource, "/uploadfile") == req->resource) {
        // --- uploading page
      make_partial_stream(&_stream_res, &part, part_upload, NULL);

    } else if (strstr(req->resource, "/ls") == req->resource) {
        // --- file list page
      make_partial_stream(&_stream_res, &part, part_ls, NULL);

    } else if (strstr(req->resource, "/test") == req->resource) {
        // --- test
      make_char_stream(&_stream_res, DEF_INDEX);
      systask_call(SYS_TEST);

    } else if (strstr(req->resource, "/rm?name=") == req->resource) {
        // --- rm file and file list page
      char *fname = (char *)&req->resource[9]; // strlen(/rm?name=)
      fs_remove(fname);
      make_partial_stream(&_stream_res, &part, part_ls, NULL);

    } else if (strstr(req->resource, "/format") == req->resource) {
        // --- format and file list page
      fs_format();
      make_partial_stream(&_stream_res, &part, part_ls, NULL);

    } else if (strstr(req->resource, "/fschk") == req->resource) {
        // --- check fs and file list page
      systask_call(SYS_FS_CHECK);
      //make_partial_stream(&_stream_res, &part, part_ls, NULL);
      make_partial_stream(&_stream_res, &part, part_busy, NULL);

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

      make_spif_stream(&_stream_res, addr, len);

    } else {
      // --- unknown resource
      char *fname = &req->resource[1];
      if (fname[0] == '.') fname++;
      spiffs_file fd = fs_open(fname, SPIFFS_RDONLY, 0);
      if (fd < 0) {
        make_char_stream(&_stream_res, NOTFOUND);
        *http_status = S404_NOT_FOUND;
      } else {
        printf("opening fs file %s (fd=%i)\n", fname, fd);
        if (strstr(fname, ".jpg") || strstr(fname, ".jpeg") || strstr(fname, ".JPG") || strstr(fname, ".JPEG")) {
          sprintf(content_type, "image/jpeg");
        }
        make_file_stream(&_stream_res, fd);
      }
    }

  }
  *res = &_stream_res;

  return UWEB_CHUNKED;
}

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
    if (_upload_fd > 0 && data == 0 && length == 0) {
      printf("closing uploaded file\n");
      fs_close(_upload_fd);
      _upload_fd = 0;
    } else if (req->cur_multipart.multipart_nbr == 0 &&
        (fname = strstr(req->cur_multipart.content_disp, "filename=\"")) &&
        (fname_end = strchr(fname + 10, '\"'))) {
      fname += 10; //filename="
      *fname_end = 0;
      printf("request to save file %s\n", fname);
      _upload_fd = fs_open(fname, SPIFFS_RDWR | SPIFFS_CREAT | SPIFFS_TRUNC, 0);
      if (_upload_fd > 0) {
        int32_t res = fs_write(_upload_fd, data, length);
        _upload_sz = length;
        if (res < SPIFFS_OK) {
          printf("error saving upload data err %i\n", fs_errno());
          fs_close(_upload_fd);
          _upload_fd = 0;
        }
      }
    } else if (_upload_fd > 0) {
      int32_t res = fs_write(_upload_fd, data, length);
      _upload_sz += length;
      if (res < SPIFFS_OK) {
        printf("error saving upload data err %i\n", fs_errno());
        fs_close(_upload_fd);
        _upload_fd = 0;
      }
    }
  }

}

bool server_is_busy(void) {
  return _server_busy_claims > 0;
}

void server_claim_busy(void) {
  _server_busy_claims++;
}

void server_release_busy(void) {
  _server_busy_claims--;
}

void server_set_busy_status(const char *str, int progress) {
  _busy_title = str;
  _busy_progress = progress;
}

#ifdef NETCONN_SERV
void server_task(void *pvParameters) {
  UWEB_init(uweb_resp, uweb_data);

  struct netconn *nc = netconn_new(NETCONN_TCP);
  if (!nc) {
    printf("netconn alloc fail\n");
    return;
  }
  netconn_bind(nc, IP_ADDR_ANY, 80);
  netconn_listen(nc);
  printf("listening on port 80\n");

  while (1) {
    struct netconn *client = NULL;
    err_t err = netconn_accept(nc, &client);

    if (err != ERR_OK) {
      if (client) {
        netconn_delete(client);
      }
      continue;
    }

    printf("connection\n");
    make_tcp_stream(&_stream_tcp, client);
    UWEB_parse(&_stream_tcp, &_stream_tcp);
//    ip_addr_t client_addr;
//    uint16_t port_ignore;
//    netconn_peer(client, &client_addr, &port_ignore);
    netconn_delete(client);
    printf("closed\n");
  }
}
#endif
#ifdef SOCK_SERV
void server_task(void *pvParameters) {
  int sock_fd, client_fd;
  struct sockaddr_in sa, isa;

  UWEB_init(uweb_resp, uweb_data);

  while (1) {
    printf("socket serv setup\n");
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
        printf("accept failed err %i\n", get_errno(sock_fd));
        lwip_close(sock_fd);
        break;
      }
      printf("client accepted\n");

#if 0
       {
        struct timeval timeout = {
            .tv_sec = 3,
            .tv_usec = 0
        };

        if (lwip_setsockopt(sock_fd, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout)) != 0) {
          printf("set recvtmo err %i\n", get_errno(sock_fd));
        }
        if (lwip_setsockopt(sock_fd, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout, sizeof(timeout)) != 0) {
          printf("set sendtmo err %i\n", get_errno(sock_fd));
        }
        if (lwip_fcntl(sock_fd, F_SETFL, O_NONBLOCK) != 0) {
          printf("set nonblock err %i\n", get_errno(sock_fd));
        }
      }
#endif
      make_tcp_stream(&_stream_tcp, client_fd);
      UWEB_parse(&_stream_tcp, &_stream_tcp);

      printf("client closed\n");
      lwip_close(client_fd);
    }
  }
}
#endif



