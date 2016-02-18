/*
 * umac.h
 *
 * micro mac
 *
 * Simple protocol stack for transmitting/receiving packets, sort of MAC-like.
 * Packets can either be synchronized (needing an ack) or unsynchronized (not needing ack).
 * The payload length can vary from 0 to 768 bytes.
 *
 * The stack handles retransmits automatically. Unless user acks packets herself, the
 * stack auto-acks if necessary. Acks can be piggybacked with payload data.
 *
 * Only one synchronized packed can be in the air at a time. It is legal to send unsynched
 * packets while a synchronized is not yet acked, though.
 *
 *  Created on: Feb 15, 2016
 *      Author: petera
 */

#ifndef _UMAC_H_
#define _UMAC_H_

/*
  DATA LINK FORMAT
    PKT header
    8 bits : 0x5a       preamble
    2 bits : 0,1,2,3    0:PKT_NREQ  1:PKT_REQ  2:ACK  3:NACK
                          PKT_NREQ : packet not requiring ack
                          PKT_REQ  : packet requiring ack
                          ACK      : acknowledge of good packet
                          NACK     : acknowledge of bad packet, length 1 with error code
    4 bits : seqno      sequence nbr
    2 bits : len_def    0: no data
                        1: data bytes = datlen + 1
                        2: data bytes = datlen + 1 + 256
                        3: data bytes = datlen + 1 + 512
   (8 bits : datlen     data length in bytes) if len_def > 0
   ( ..      payload    data                ) if len_def bytes > 0
    16 bits: checksum   CRC-CCITT16 of whole packet excluding preamble

     76543210  76543210   76543210  data   76543210  76543210
    [PREAMBLE][TySeqnLd]([DatLen  ][]..[])[CrcHi   ][CrcLo   ]

    For packets requiring ack (PKT_REQ), the seqno increments from 1..15.
    For packets not requiring ack (PKT_NREQ), the seqno is 0.
    An ACK or NACK keeps the seqno of the packet being acked or nacked, if known.
    If unknown, 0x0 is used as seqno.

    NACK error codes:
      0x01 - not preamble
      0x02 - bad crc
      0x03 - rx timeout
      0x04 - not ready

 */

#include "umac_cfg.h"

#ifndef CFG_UMAC_DBG
#define CFG_UMAC_DBG(...)
#endif

#ifndef CFG_UMAC_RETRIES
#define CFG_UMAC_RETRIES             10
#endif

#ifndef CFG_UMAC_RETRY_DELTA
#define CFG_UMAC_RETRY_DELTA         40
#endif

#ifndef CFG_UMAC_RX_TIMEOUT
#define CFG_UMAC_RX_TIMEOUT          2*CFG_UMAC_RETRY_DELTA*CFG_UMAC_RETRIES
#endif

#ifndef CFG_UMAC_TICK_TYPE
#define CFG_UMAC_TICK_TYPE           uint32_t
#endif

#define UMAC_PREAMBLE                0x5a

#define UMAC_NACK_ERR_NOT_PREAMBLE   0x01
#define UMAC_NACK_ERR_BAD_CRC        0x02
#define UMAC_NACK_ERR_RX_TIMEOUT     0x03
#define UMAC_NACK_ERR_NOT_READY      0x04

typedef CFG_UMAC_TICK_TYPE umtick;

typedef enum {
  UMAC_PKT_NREQ_ACK = 0,
  UMAC_PKT_REQ_ACK,
  UMAC_PKT_ACK,
  UMAC_PKT_NACK
} umac_pkt_type;

typedef enum {
  UMST_RX_EXP_PREAMBLE = 0,
  UMST_RX_NOT_PREAMBLE,
  UMST_RX_EXP_HDR_HI,
  UMST_RX_EXP_HDR_LO,
  UMST_RX_DATA,
  UMST_RX_CRC_HI,
  UMST_RX_CRC_LO,
} umac_rx_state;

typedef struct {
  umac_pkt_type pkt_type;
  uint8_t seqno;
  uint8_t *data;
  uint16_t length;
  uint16_t crc;
} umac_pkt;

typedef void (* umac_request_future_tick)(umtick delta_tick);
typedef void (* umac_cancel_future_tick)(void);
typedef umtick (* umac_now_tick)(void);
typedef void (* umac_tx_byte)(uint8_t c);
typedef void (* umac_tx_buf)(uint8_t *c, uint16_t len);
typedef void (* umac_rx_pkt)(umac_pkt *pkt);
typedef void (* umac_tx_pkt_acked)(uint8_t seqno, uint8_t *data, uint16_t len);
typedef void (* umac_timeout)(umac_pkt *pkt);

typedef struct {
  /** Requests that umac_tick is to be called within given ticks */
  umac_request_future_tick timer_fn;
  /** Cancel any previous request to call umac_tick */
  umac_cancel_future_tick cancel_timer_fn;
  /** Returns current system tick */
  umac_now_tick now_fn;
  /** Transmits one byte over the PHY layer */
  umac_tx_byte tx_byte_fn;
  /** Transmits a buffer over the PHY layer */
  umac_tx_buf tx_buf_fn;
  /** Called when a packet is received */
  umac_rx_pkt rx_pkt_fn;
  /** Called when a synchronous packet is acked from other side */
  umac_tx_pkt_acked rx_pkt_ack_fn;
  /** Called if a synchronous packet is not acked within timeout */
  umac_timeout timeout_fn;
} umac_cfg;

typedef struct {
  umac_cfg cfg;
  umac_rx_state rx_state;
  uint8_t tmp[8];

  umac_pkt rx_pkt;

  uint16_t rx_data_cnt;
  uint16_t rx_local_crc;
  uint8_t rx_user_acked;
  uint8_t tx_seqno;

  umac_pkt tx_pkt;
  uint8_t await_ack;
  uint8_t retry_ctr;

  uint8_t timer_enabled;
  uint8_t timer_ack_enabled;
  umtick timer_ack_delta;
  umtick timer_ack_start_tick;
  uint8_t timer_rx_enabled;
  umtick timer_rx_delta;
  umtick timer_rx_start_tick;
} umac;

/**
 * Initiates protocol stack with given configuration and
 * given rx buffer. The buffer should be 768 bytes.
 */
void umac_init(umac *u, umac_cfg *cfg, uint8_t *rx_buffer);
/**
 * Call umac_tick when a requested timer times out. See
 * umac_request_future_tick timer_fn in config struct.
 */
void umac_tick(umac *u);
/**
 * Transmits a packet.
 */
int umac_tx_pkt(umac *u, uint8_t ack, uint8_t *buf, uint16_t len);
/**
 * When a synchronous packet is received, umac_rx_pkt rx_pkt_fn
 * in config struct is called. In this call, user may ack with
 * piggybacked data if wanted. If not, the stack autoacks with an
 * empty ack.
 */
int umac_tx_reply_ack(umac *u,  uint8_t *buf, uint16_t len);

/**
 * Report to stack that a byte was received from PHY.
 */
void umac_report_rx_byte(umac *u, uint8_t c);
/**
 * Report to stack that a buffer was received from PHY.
 */
void umac_report_rx_buf(umac *u, uint8_t *buf, uint16_t len);

#endif /* _UMAC_H_ */
