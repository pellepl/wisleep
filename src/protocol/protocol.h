/*
 * protocol.h
 *
 * Simple protocol stack for transmitting/receiving packets, ~MAC parts.
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

#ifndef _PROTOCOL_H_
#define _PROTOCOL_H_

/*
  PHYSICAL PROTOCOL
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

#include <protocol_cfg.h>

#ifndef CFG_PCOMM_DBG
#define CFG_PCOMM_DBG(...)
#endif

#ifndef CFG_PCOMM_RETRIES
#define CFG_PCOMM_RETRIES             10
#endif

#ifndef CFG_PCOMM_RETRY_DELTA
#define CFG_PCOMM_RETRY_DELTA         40
#endif

#ifndef CFG_PCOMM_RX_TIMEOUT
#define CFG_PCOMM_RX_TIMEOUT          2*CFG_PCOMM_RETRY_DELTA*CFG_PCOMM_RETRIES
#endif

#ifndef CFG_PCOMM_TICK_TYPE
#define CFG_PCOMM_TICK_TYPE           uint32_t
#endif

#define PCOMM_PREAMBLE                0x5a

#define PCOMM_NACK_ERR_NOT_PREAMBLE   0x01
#define PCOMM_NACK_ERR_BAD_CRC        0x02
#define PCOMM_NACK_ERR_RX_TIMEOUT     0x03
#define PCOMM_NACK_ERR_NOT_READY      0x04

typedef CFG_PCOMM_TICK_TYPE ptick;

typedef enum {
  PCOMM_PKT_NREQ_ACK = 0,
  PCOMM_PKT_REQ_ACK,
  PCOMM_PKT_ACK,
  PCOMM_PKT_NACK
} pcomm_pkt_type;

typedef enum {
  PST_RX_EXP_PREAMBLE = 0,
  PST_RX_NOT_PREAMBLE,
  PST_RX_EXP_HDR_HI,
  PST_RX_EXP_HDR_LO,
  PST_RX_DATA,
  PST_RX_CRC_HI,
  PST_RX_CRC_LO,
} pcomm_rx_state;

typedef struct {
  pcomm_pkt_type pkt_type;
  uint8_t seqno;
  uint8_t *data;
  uint16_t length;
  uint16_t crc;
} pcomm_pkt;

typedef void (* pcomm_request_future_tick)(ptick delta_tick);
typedef void (* pcomm_cancel_future_tick)(void);
typedef ptick (* pcomm_now_tick)(void);
typedef void (* pcomm_tx_byte)(uint8_t c);
typedef void (* pcomm_tx_buf)(uint8_t *c, uint16_t len);
typedef void (* pcomm_rx_pkt)(pcomm_pkt *pkt);
typedef void (* pcomm_tx_pkt_acked)(uint8_t seqno, uint8_t *data, uint16_t len);
typedef void (* pcomm_timeout)(pcomm_pkt *pkt);

typedef struct {
  /** Requests that pcomm_tick is to be called within given ticks */
  pcomm_request_future_tick timer_fn;
  /** Cancel any previous request to call pcomm_tick */
  pcomm_cancel_future_tick cancel_timer_fn;
  /** Returns current system tick */
  pcomm_now_tick now_fn;
  /** Transmits one byte over the PHY layer */
  pcomm_tx_byte tx_byte_fn;
  /** Transmits a buffer over the PHY layer */
  pcomm_tx_buf tx_buf_fn;
  /** Called when a packet is received */
  pcomm_rx_pkt rx_pkt_fn;
  /** Called when a synchronous packet is acked from other side */
  pcomm_tx_pkt_acked rx_pkt_ack_fn;
  /** Called if a synchronous packet is not acked within timeout */
  pcomm_timeout timeout_fn;
} pcomm_cfg;

typedef struct {
  pcomm_cfg cfg;
  pcomm_rx_state rx_state;
  uint8_t tmp[8];

  pcomm_pkt rx_pkt;

  uint16_t rx_data_cnt;
  uint16_t rx_local_crc;
  uint8_t rx_user_acked;
  uint8_t tx_seqno;

  pcomm_pkt tx_pkt;
  uint8_t await_ack;
  uint8_t retry_ctr;

  uint8_t timer_enabled;
  uint8_t timer_ack_enabled;
  ptick timer_ack_delta;
  ptick timer_ack_start_tick;
  uint8_t timer_rx_enabled;
  ptick timer_rx_delta;
  ptick timer_rx_start_tick;
} pcomm;

/**
 * Initiates protocol stack with given configuration and
 * given rx buffer. The buffer should be 768 bytes.
 */
void pcomm_init(pcomm *p, pcomm_cfg *cfg, uint8_t *rx_buffer);
/**
 * Call pcomm_tick when a requested timer times out. See
 * pcomm_request_future_tick timer_fn in config struct.
 */
void pcomm_tick(pcomm *p);
/**
 * Transmits a packet.
 */
int pcomm_tx_pkt(pcomm *p, uint8_t ack, uint8_t *buf, uint16_t len);
/**
 * When a synchronous packet is received, pcomm_rx_pkt rx_pkt_fn
 * in config struct is called. In this call, user may ack with
 * piggybacked data if wanted. If not, the stack autoacks with an
 * empty ack.
 */
int pcomm_tx_reply_ack(pcomm *p,  uint8_t *buf, uint16_t len);

/**
 * Report to stack that a byte was received from PHY.
 */
void pcomm_report_rx_byte(pcomm *p, uint8_t c);
/**
 * Report to stack that a buffer was received from PHY.
 */
void pcomm_report_rx_buf(pcomm *p, uint8_t *buf, uint16_t len);

#endif /* _PROTOCOL_H_ */
