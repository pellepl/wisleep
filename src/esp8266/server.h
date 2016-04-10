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
#include "FreeRTOS.h"
#include "task.h"
#include "espressif/esp_common.h"

void server_task(void *pvParameters);

bool server_is_busy(void);
void server_claim_busy(void);
void server_release_busy(void);
void server_set_busy_status(const char *str, int progress);

#endif /* _ESP8266_SERVER_H_ */
