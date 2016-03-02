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

#endif /* _ESP8266_SERVER_H_ */
