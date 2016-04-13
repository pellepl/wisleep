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
#include "systasks.h"
#include "fs.h"

//#define RAW_SERV // TODO
//#define NETCONN_SERV
#define SOCK_SERV

static action_fn _act_fn = NULL;
static uint32_t _upload_sz;
static spiffs_file _upload_fd = 0;
static uweb_data_stream _stream_tcp, _stream_res;
static const char *_busy_title;
static int _busy_progress;
static volatile uint8_t _server_busy_claims;

static int get_errno(int sock_fd) {
  int error;
  uint32_t optlen = sizeof(error);
  (void)lwip_getsockopt(sock_fd, SOL_SOCKET, SO_ERROR, &error, &optlen);
  return error;
}

bool strendnocase(const char *str, const char *pfx) {
  int str_len = strlen(str);
  int pfx_len = strlen(pfx);
  if (str_len < pfx_len) return false;

  while (pfx_len) {
    char str_c = str[--str_len];
    char pfx_c = pfx[--pfx_len];
    if (str_c >= 'a' && str_c <= 'z') str_c = str_c - 'a' + 'A';
    if (pfx_c >= 'a' && pfx_c <= 'z') pfx_c = pfx_c - 'a' + 'A';
    if (str_c != pfx_c) return false;
  }
  return true;
}

char* get_arg_str(const char *req, const char *arg, char *dst) {
  char arg_str[32];
  memset(arg_str, 0, sizeof(arg_str));
  strcpy(arg_str, arg);
  int arg_len = strlen(arg);
  arg_str[arg_len] = '=';
  char *arg_data;
  if ((arg_data = strstr(req, arg_str))) {
    arg_data += arg_len + 1;
    const char *arg_data_end;
    if ((arg_data_end = strchr(arg_data, '&')) == NULL) {
      arg_data_end = &req[strlen(req)];
    }
    if (dst) {
      urlndecode(dst, arg_data, arg_data_end - arg_data);
    } else {
      dst = arg_data;
    }
  } else {
    dst = NULL;
  }
  return dst;
}

static bool get_arg_int(const char *req, const char *arg, int32_t *dst, int32_t def) {
  char arg_data[14];
  if (get_arg_str(req, arg, arg_data)) {
    if (dst) {
      *dst = strtol(arg_data, NULL, 0);
    }
    return true;
  }
  if (dst) {
    *dst = def;
  }
  return false;
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
static void sockstr_close(UW_STREAM str) {
  close((intptr_t)str->user);
}
UW_STREAM make_tcp_stream(UW_STREAM str, int sockfd) {
  str->total_sz = -1;
  str->avail_sz = 256;
  str->user = (void *)((intptr_t)sockfd);
  str->read = sockstr_read;
  str->write = sockstr_write;
  str->close = sockstr_close;
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

UW_STREAM make_null_stream(UW_STREAM str) {
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
UW_STREAM make_char_stream(UW_STREAM str, const char *txt) {
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
UW_STREAM make_spif_stream(UW_STREAM str, uint32_t addr, uint32_t len) {
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
UW_STREAM make_partial_stream(UW_STREAM str, part_def *part, generate_partial_content_f fn, void *user) {
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
void close_partial_stream(part_def *part) {
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
UW_STREAM make_file_stream(UW_STREAM str, spiffs_file fd) {
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


static uint32_t part_busy(part_def *part, uint32_t max_len, uint32_t part_nbr, uint8_t *dst) {
  uint32_t len = 0;
  if (part->nbr == 0) {
    sprintf((char *)dst,
        "<!DOCTYPE html>"
        "<html>"
        "<style>progress[value]{width:75%%;}</style>"
        "<script>"
          "var polling=false;"
          "function tick() {"
            "if (!polling) {"
              "polling=true;"
              "var xh = new XMLHttpRequest();"
              "xh.open(\"GET\", \"/__busy\", true);"
              "xh.send(null);"
    );
    len = strlen((char *)dst);
  } else if (part->nbr == 1) {
    sprintf((char *)dst,
              "xh.onreadystatechange = function() {"
                "polling=false;"
                "if (xh.readyState==4 && xh.status==200) {"
                  "var s=xh.responseText;"
                  "if (s==\"!BUSY\") {"
                    "location.reload(true);"
                  "} else if (s!=\"INDEF\") {"
                    "document.getElementById(\"bprog\").value=Number(s);"
                  "}"
                "}"
            "};"
          "}"
        );
    len = strlen((char *)dst);
  } else if (part->nbr == 2) {
    char sprogress[32];
    if (_busy_progress >= 0) {
      sprintf(sprogress, "value=\"%i\"", _busy_progress);
    } else {
      memset(sprogress, 0, sizeof(sprogress));
    }
    sprintf((char *)dst,
          "setTimeout('tick();',500);"
        "}"
        "</script>"
        "<body onload=\"tick();\">"
        "<h2 align=\"center\">%s</h2>"
        "<div align=\"middle\"><progress id=\"bprog\" %s max=\"100\"></progress></div>"
        "</body></html>",
        _busy_title, sprogress);
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
        "<html>"
        "<script>"
          "function mv(fnm) {"
           "var nnm = prompt(\"New name\", fnm);"
           "if (nnm && nnm!='null') {"
             "window.location.href='mv?old='+fnm+'&new='+nnm;"
           "}"
           "return false;"
          "}"
        "</script>"
        "<body>"
        "<h1><a href=\"/\">wisleep</a></h1>"
        "<div align=\"middle\"><table style=\"border:1px solid;font-family:courier;border-spacing:8px;\">"
        "<tr><td><b>NAME</b></td><td><b>SIZE</b></td><td><b>OBJID</b></td><td><b>RM</b></td><td><b>MV</b></td></tr>";
    part->user = (void *)fs_opendir("/", &_ls_d);
    len = strlen(LS_PRE);
    memcpy(dst, LS_PRE, len);
  } else if (part->nbr == 0x10001) {
    sprintf((char *)dst,
        "<div align=\"middle\"><table style=\"border:0px;border-spacing:8px;\">"
        "<tr><td>"
        "<form action=\"fschk\">"
        "<input type=\"submit\" value=\"FS check\"/>"
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
      "</td></tr></table></div>"
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
          "<td><a href='/rm?name=%s'>X</a></td>"
          "<td><a href='/mv?%s' onclick='return mv(\"%s\");'>N</a></td>"
          "</tr>",
          e.name, e.name, e.size, e.obj_id, e.name, e.name, e.name);
      len = strlen((char *)dst);
    } else {
      uint32_t total, used;
      fs_info(&total, &used);
      sprintf((char *)dst,
        "<tr><td colspan=\"5\">&nbsp;</td></tr>"
        "<tr><td>USED</td><td>%i</td><td>TOTAL</td><td colspan=\"2\">%i</td></tr>"
        "</table></div>",
      used, total);
      len = strlen((char *)dst);
      part->nbr = 0x10000;
    }
  }
  return len;
}

static uint32_t part_aplist(part_def *part, uint32_t max_len, uint32_t part_nbr, uint8_t *dst) {
  uint32_t len = 0;
  if (part->nbr == 0) {
    const char *LS_PRE =
        "<!DOCTYPE html>"
        "<html><body>"
        "<h1><a href=\"/\">wisleep</a></h1>"
        "<div align=\"middle\"><table style=\"border:1px solid;font-family:courier;border-spacing:8px;\">"
        "<tr><td><b>MAC</b></td><td><b>SSID</b></td><td><b>CH</b></td><td><b>RSSI</b></td></tr>";
    part->user = (void *)(intptr_t)fs_open(SYSTASK_AP_SCAN_FILENAME, SPIFFS_O_RDONLY, 0);
    len = strlen(LS_PRE);
    memcpy(dst, LS_PRE, len);
  } else if (part->nbr == 0x10001) {
    sprintf((char *)dst,
        "<div align=\"middle\"><table style=\"border:0px;border-spacing:8px;\">"
        "<tr><td>"
        "<form action=\"scan\">"
        "<input type=\"submit\" value=\"AP Scan\"/>"
        "</form>"
        "</td>"
        );
    len = strlen((char *)dst);
  } else if (part->nbr == 0x10002) {
    sprintf((char *)dst,
      "</tr></table></div>"
      "</body></html>"
        );
    len = strlen((char *)dst);
    close_partial_stream(part);
  } else {
    spiffs_file fd = (spiffs_file)(intptr_t)part->user;
    struct sdk_bss_info bss_info;
    if (fd > 0 &&
        fs_read(fd, &bss_info, sizeof(struct sdk_bss_info)) == sizeof(struct sdk_bss_info)) {
      sprintf((char *)dst,
          "<tr>"
          "<td>%02x:%02x:%02x:%02x:%02x:%02x</td>"
          "<td>%s</td>"
          "<td>%i</td>"
          "<td>%i</td>"
          "</tr>",
          bss_info.bssid[0], bss_info.bssid[1], bss_info.bssid[2], bss_info.bssid[3], bss_info.bssid[4], bss_info.bssid[5],
          bss_info.ssid,
          bss_info.channel,
          bss_info.rssi
          );
      len = strlen((char *)dst);
    } else {
      fs_close(fd);
      part->user = 0;
      sprintf((char *)dst,
        "</table></div>");
      len = strlen((char *)dst);
      part->nbr = 0x10000;
    }
  }
  return len;
}

static const char *DEF_INDEX =
    "<!DOCTYPE html>"
    "<html><body>"
    "<h1><a href=\"/\">wisleep</a></h1>"
    "<link rel=\"icon\" href=\"favicon.ico\"/>"
    "<form action=\"spiflash\" method=\"get\">"
      "<fieldset>"
        "<legend>Dump spi flash</legend>"
        "Address:<br/>"
        "<input type=\"text\" name=\"addr\" value=\"0x00000000\"/><br/>"
        "Length:<br/>"
        "<input type=\"text\" name=\"len\" value=\"1024\"/><br/><br/>"
        "<input type=\"submit\" value=\"Dump\"/>"
      "</fieldset>"
    "</form>"
    "<form action=\"uploadfile\" method=\"post\" enctype=\"multipart/form-data\">"
      "<fieldset>"
        "<legend>Upload file</legend>"
        "<input type=\"file\" name=\"upfile\" id=\"upfile\"/>"
        "<input type=\"submit\" value=\"Upload\" name=\"submit\"/>"
      "</fieldset>"
    "</form>"
    "<div align=\"middle\"><table style=\"border:0px;border-spacing:8px;\">"
    "<tr>"
      "<td><form action=\"ls\">"
      "<input type=\"submit\" value=\"List Files\"/>"
      "</form></td>"
      "<td><form action=\"scan\">"
      "<input type=\"submit\" value=\"AP Scan\"/>"
      "</form></td>"
      "<td><form action=\"test\">"
      "<input type=\"submit\" value=\"Test\"/>"
      "</form></td>"
    "</tr></table></div>"
    "</body></html>"
        ;
const char *NOTFOUND =
    "<!DOCTYPE html>"
    "<html><body>"
    "<h1><a href=\"/\">wisleep</a></h1>"
    "<h1 align=\"center\">404 Not found</h1>"
    "</body></html>";

///////////////////////////////////////////////////////////////////////////////


static char extra_hdrs[128];

static part_def part;
static char __prog_txt[16];

static uweb_response uweb_resp(uweb_request_header *req, UW_STREAM *res,
    uweb_http_status *http_status, char *content_type,
    char **extra_headers) {

  // if server is busy, load busy page
  if (server_is_busy()) {
    if (req->chunk_nbr == 0) {
      if (strcmp(req->resource, "/__busy") == 0) {
        if (_busy_progress >= 0) {
          sprintf(__prog_txt, "%i", _busy_progress);
        } else {
          sprintf(__prog_txt, "INDEF");
        }
        make_char_stream(&_stream_res, __prog_txt);
      } else {
        printf("req BUSY %s\n", &req->resource[1]);
        make_partial_stream(&_stream_res, &part, part_busy, NULL);
      }
    }

    *res = &_stream_res;

    return UWEB_CHUNKED;
  }

  // server idle, handle request
  if (req->chunk_nbr == 0) {
    printf("req \"%s\"\n", &req->resource[1]);
    make_null_stream(&_stream_res);
    WDT.FEED = WDT_FEED_MAGIC;

    if (strcmp(req->resource, "/__busy") == 0) {
      sprintf(__prog_txt, "!BUSY");
      make_char_stream(&_stream_res, __prog_txt);
    } else if (strcmp(req->resource, "/index.html") == 0 || strlen(req->resource) == 1) {
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
      make_partial_stream(&_stream_res, &part, part_ls, NULL);
      // DO NOT REDIRECT as this closes the stream => no file
      //return UWEB_return_redirect(req, "ls");

    } else if (strstr(req->resource, "/dir") == req->resource) {
      return UWEB_return_redirect(req, "http://esp.pelleplutt.com/ls");

    } else if (strstr(req->resource, "/ls") == req->resource) {
        // --- file list page
      make_partial_stream(&_stream_res, &part, part_ls, NULL);

    } else if (strstr(req->resource, "/test") == req->resource) {
        // --- test
      systask_call(SYS_TEST, true);
      return UWEB_return_redirect(req, "index.html");

    } else if (strstr(req->resource, "/scan") == req->resource) {
        // --- scan APs
      systask_call(SYS_WIFI_SCAN, true);
      return UWEB_return_redirect(req, "aplist");

    } else if (strstr(req->resource, "/aplist") == req->resource) {
        // --- list APs
      make_partial_stream(&_stream_res, &part, part_aplist, NULL);

    } else if (strstr(req->resource, "/rm?name=") == req->resource) {
        // --- rm file and file list page
      char *fname = (char *)&req->resource[9]; // strlen(/rm?name=)
      fs_remove(fname);
      //make_partial_stream(&_stream_res, &part, part_ls, NULL);
      return UWEB_return_redirect(req, "ls");

    } else if (strstr(req->resource, "/mv?") == req->resource) {
        // --- rename file and file list page
      char old_name[SPIFFS_OBJ_NAME_LEN];
      char new_name[SPIFFS_OBJ_NAME_LEN];
      do {
        if (get_arg_str(req->resource, "old", old_name) == NULL) break;
        if (get_arg_str(req->resource, "new", new_name) == NULL) break;
        if (old_name[0] == '.' || new_name[0] == '.') break;
        fs_rename(old_name, new_name);
      } while (0);
      return UWEB_return_redirect(req, "ls");

    } else if (strstr(req->resource, "/format") == req->resource) {
        // --- format and file list page
      systask_call(SYS_FS_FORMAT, true);
      return UWEB_return_redirect(req, "ls");

    } else if (strstr(req->resource, "/fschk") == req->resource) {
        // --- check fs and file list page
      systask_call(SYS_FS_CHECK, true);
      return UWEB_return_redirect(req, "ls");

    } else if (strstr(req->resource, "/spiflash") == req->resource) {
      // --- read spiflash
      sprintf(content_type, "application/octet-stream");
      uint32_t addr, len;
      get_arg_int(req->resource, "addr", (int32_t*)&addr, 0);
      get_arg_int(req->resource, "len", (int32_t*)&len, sdk_flashchip.chip_size);

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
      char fname[SPIFFS_OBJ_NAME_LEN+1];
      memset(fname, 0, SPIFFS_OBJ_NAME_LEN+1);
      char *resource_str = &req->resource[1]; // skip '/'
      if (resource_str[0] == '.') resource_str++;
      char *param_ix = strchr(resource_str, '?');
      if (param_ix) {
        int len = (ptrdiff_t)(param_ix - resource_str);
        strncpy(fname, resource_str, SPIFFS_OBJ_NAME_LEN < len ? SPIFFS_OBJ_NAME_LEN : len);
      } else {
        strncpy(fname, resource_str, SPIFFS_OBJ_NAME_LEN);
      }

      if (strendnocase(fname, "/act")) {
        // action
        if (_act_fn) {
          uweb_response act_res = _act_fn(req, &_stream_res, http_status,
              content_type, extra_headers);
          *res = &_stream_res;
          return act_res;
        }
      } else {
        spiffs_file fd = fs_open(fname, SPIFFS_RDONLY, 0);
        if (fd < 0) {
          printf("fs file %s not found (err %i)\n", fname, fs_errno());
          make_char_stream(&_stream_res, NOTFOUND);
          *http_status = S404_NOT_FOUND;
        } else {
          printf("opening fs file %s (fd=%i)\n", fname, fd);
          if (strendnocase(fname, ".jpg") || strendnocase(fname, ".jpeg")) {
            sprintf(content_type, "image/jpeg");
          }
          else if (strendnocase(fname, ".gif")) {
            sprintf(content_type, "image/gif");
          }
          else if (strendnocase(fname, ".png")) {
            sprintf(content_type, "image/png");
          }
          else if (strendnocase(fname, ".bmp")) {
            sprintf(content_type, "image/bmp");
          }
          else if (strendnocase(fname, ".tiff")) {
            sprintf(content_type, "image/tiff");
          }
          else if (strendnocase(fname, ".webp")) {
            sprintf(content_type, "image/webp");
          }
          else if (strendnocase(fname, ".ico")) {
            sprintf(content_type, "image/x-icon");
          }
          else if (strendnocase(fname, ".css")) {
            sprintf(content_type, "text/css");
          }
          else if (strendnocase(fname, ".pdf")) {
            sprintf(content_type, "application/pdf");
          }
          else if (strendnocase(fname, ".wav")) {
            sprintf(content_type, "audio/x-wav");
          }
          else if (strendnocase(fname, ".js")) {
            sprintf(content_type, "application/javascript");
          }
          make_file_stream(&_stream_res, fd);
        }
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

void server_init(action_fn act_fn) {
  _act_fn = act_fn;
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



