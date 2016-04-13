/*
 * server_actions.c
 *
 *  Created on: Apr 13, 2016
 *      Author: petera
 */

#include "server.h"
#include "../uweb/src/uweb.h"
#include "../uweb/src/uweb_http.h"
#include "fs.h"
#include "systasks.h"
#include "bridge.h"

uweb_response server_actions(
    uweb_request_header *req, UW_STREAM res, uweb_http_status *http_status,
    char *content_type, char **extra_headers) {
  char arg[32];
  if (get_arg_str(req->resource, "col", arg) && strlen(arg) == 6) {
    uint32_t col = strtol(arg, NULL, 16);
    printf("set col %08x\n", col);
    bridge_set_color(col);
    return UWEB_OK;
  }
  make_char_stream(res, NOTFOUND);
  *http_status = S404_NOT_FOUND;
  return UWEB_CHUNKED;
}
