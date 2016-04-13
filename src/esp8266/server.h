/*
 * server.h
 *
 *  Created on: Mar 2, 2016
 *      Author: petera
 */

#ifndef _ESP8266_SERVER_H_
#define _ESP8266_SERVER_H_
#include <stdint.h>
#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <esp8266.h>
#include <esp/uart.h>
#include <stdio.h>
#include "../uweb/src/uweb.h"
#include "FreeRTOS.h"
#include "task.h"
#include "espressif/esp_common.h"
//#include "fs.h"


typedef uweb_response (*action_fn)(
    uweb_request_header *req, UW_STREAM res, uweb_http_status *http_status,
    char *content_type, char **extra_headers);


struct part_def_s;

typedef uint32_t (* generate_partial_content_f)(struct part_def_s *part, uint32_t max_len, uint32_t part_nbr, uint8_t *dst);

typedef struct part_def_s {
  uint8_t content[512];
  generate_partial_content_f gen_fn;
  uint32_t nbr;
  uint32_t len;
  uint32_t ix;
  void *user;
} part_def;

extern const char *NOTFOUND;

void server_init(action_fn act_fn);
void server_task(void *pvParameters);

bool server_is_busy(void);
void server_claim_busy(void);
void server_release_busy(void);
void server_set_busy_status(const char *str, int progress);

bool strendnocase(const char *str, const char *pfx);
char* get_arg_str(const char *req, const char *arg, char *dst);

UW_STREAM make_null_stream(UW_STREAM str);
UW_STREAM make_char_stream(UW_STREAM str, const char *txt);
//UW_STREAM make_file_stream(UW_STREAM str, spiffs_file fd);
UW_STREAM make_partial_stream(UW_STREAM str, part_def *part, generate_partial_content_f fn, void *user);
UW_STREAM make_spif_stream(UW_STREAM str, uint32_t addr, uint32_t len);
UW_STREAM make_tcp_stream(UW_STREAM str, int sockfd);


uweb_response server_actions(
    uweb_request_header *req, UW_STREAM res, uweb_http_status *http_status,
    char *content_type, char **extra_headers);

#endif /* _ESP8266_SERVER_H_ */
