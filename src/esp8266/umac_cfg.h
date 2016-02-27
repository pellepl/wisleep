/*
 * umac_cfg.h
 *
 *  Created on: Feb 16, 2016
 *      Author: petera
 */

#ifndef _UMAC_CFG_H_
#define _UMAC_CFG_H_

#include "FreeRTOS.h"
#include <stdint.h>
#include <string.h>
#include <stdio.h>

//#define CFG_UMAC_NACK_GARBAGE
#define CFG_UMAC_RETRIES              10
#define CFG_UMAC_RETRY_DELTA(t)       40/portTICK_RATE_MS
#define CFG_UMAC_RX_TIMEOUT           2*CFG_UMAC_RETRY_DELTA(1)*CFG_UMAC_RETRIES
#define CFG_UMAC_TICK_TYPE            portTickType
#define CFG_UMAC_DBG(...)            //printf( __VA_ARGS__ )

#endif /* _UMAC_CFG_H_ */
