/*
 * protocol_cfg.h
 *
 *  Created on: Feb 16, 2016
 *      Author: petera
 */

#ifndef _PROTOCOL_CFG_H_
#define _PROTOCOL_CFG_H_

#include "system.h"
#include "miniutils.h"

#define CFG_PCOMM_RETRIES             10
#define CFG_PCOMM_RETRY_DELTA         40
#define CFG_PCOMM_RX_TIMEOUT          2*CFG_PCOMM_RETRY_DELTA*CFG_PCOMM_RETRIES
#define CFG_PCOMM_TICK_TYPE           time
#define CFG_PCOMM_DBG(...) DBG(D_COMM, D_DEBUG, "COMM" __VA_ARGS__ )

#endif /* _PROTOCOL_CFG_H_ */
