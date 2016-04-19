/*
 * server_actions.c
 *
 *  Created on: Apr 13, 2016
 *      Author: petera
 */

#include "server.h"
#include "../uweb/src/uweb.h"
#include "../uweb/src/uweb_http.h"
#include "bridge_esp.h"
#include "fs.h"
#include "systasks.h"
#include "ntp.h"

uweb_response server_actions(
    uweb_request_header *req, UW_STREAM res, uweb_http_status *http_status,
    char *content_type, char **extra_headers) {
  char arg[32];
  if (get_arg_str(req->resource, "col", arg) && strlen(arg) == 6) {
    uint32_t col = strtol(arg, NULL, 16);
    printf("set col %08x\n", col);
    bridge_lamp_set_color(col);
    return UWEB_OK;
  }
  else if (get_arg_str(req->resource, "inten", arg)) {
    uint32_t intensity = strtol(arg, NULL, 10);
    printf("set intensity %08x\n", intensity);
    bridge_lamp_set_intensity(intensity);
    return UWEB_OK;
  }
  else if (get_arg_str(req->resource, "askstat", arg)) {
    lamp_status *stat = bridge_lamp_get_status(true);
    char buf[64];
    sprintf(buf, "%s,%i,#%06x",
        stat->ena ? "1":"0",
        stat->intensity,
        stat->rgb);
    make_char_stream_copy(res, buf);
    return UWEB_CHUNKED;
  }
  else if (get_arg_str(req->resource, "qntp", arg)) {
    ntp_set_host(arg);
    systask_call(SYS_NTP_QUERY, true);
    char buf[64];
    sprintf(buf, arg);
    make_char_stream_copy(res, buf);
    return UWEB_CHUNKED;
  }
  else if (get_arg_str(req->resource, "ping", arg)) {
    bridge_ping();
    return UWEB_OK;
  }
  make_char_stream(res, NOTFOUND);
  *http_status = S404_NOT_FOUND;
  return UWEB_CHUNKED;
}
