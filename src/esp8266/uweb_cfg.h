/*
 * uweb_cfg.h
 *
 *  Created on: Mar 2, 2016
 *      Author: petera
 */

#ifndef _UWEB_CFG_H_
#define _UWEB_CFG_H_


#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

#define UWEB_SERVER_NAME              "wisleep"
#define UWEB_TX_MAX_LEN               512
#define UWEB_MAX_RESOURCE_LEN         256
#define UWEB_MAX_HOST_LEN             64
#define UWEB_MAX_CONTENT_TYPE_LEN     128
#define UWEB_MAX_CONNECTION_LEN       64
#define UWEB_MAX_CONTENT_DISP_LEN     256
#define UWEB_REQ_BUF_MAX_LEN          512
#define UWEB_ASSERT(x)
#define UWEB_DBG(...)                 ///printf( "[UWEB] "__VA_ARGS__ )


#endif /* _UWEB_CFG_H_ */
