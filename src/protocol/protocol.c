/*
 * protocol.c
 *
 *  Created on: Feb 15, 2016
 *      Author: petera
 */

#include "protocol.h"

#define PCOMM_INIT_CRC      0xffff

static void _pcomm_request_rx_timer(pcomm *p, ptick delta);
static void _pcomm_request_ack_timer(pcomm *p, ptick delta);

static inline unsigned short _crc_ccitt_16(unsigned short crc, unsigned char data) {
  crc  = (unsigned char)(crc >> 8) | (crc << 8);
  crc ^= data;
  crc ^= (unsigned char)(crc & 0xff) >> 4;
  crc ^= (crc << 8) << 4;
  crc ^= ((crc & 0xff) << 4) << 1;
  return crc;
}

static uint16_t _crc_buf(uint16_t initial, uint8_t *data, uint16_t len) {
  uint16_t crc = initial;
  while (len--) {
    crc = _crc_ccitt_16(crc, *data++);
  }
  return crc;
}

// starts system timer
static void _pcomm_hal_set_timer(pcomm *p, ptick delta) {
  if (p->timer_enabled) {
    p->cfg.cancel_timer_fn();
  }
  p->timer_enabled = 1;
  p->cfg.timer_fn(delta);
}

// stops system timer
static void _pcomm_hal_disable_timer(pcomm *p) {
  if (p->timer_enabled) {
    p->cfg.cancel_timer_fn();
  }
  p->timer_enabled = 0;
}

// transmit a NACK with error code
static void _pcomm_tx_nack(pcomm *p, uint8_t err, uint8_t seqno) {
  CFG_PCOMM_DBG("RX: NACK seq %x: err %i\n", seqno, err);
  p->tmp[0] = PCOMM_PREAMBLE;
  p->tmp[1] = (PCOMM_PKT_NACK << 6) | ((seqno & 0xf) << 2) | (1);
  p->tmp[2] = 0x00;
  p->tmp[3] = err;
  uint16_t crc = _crc_buf(PCOMM_INIT_CRC, &p->tmp[1], 3);
  p->tmp[4] = crc >> 8;
  p->tmp[5] = crc;
  p->cfg.tx_buf_fn(p->tmp, 6);
}

// transmit an empty ACK
static void _pcomm_tx_ack_empty(pcomm *p, uint8_t seqno) {
  CFG_PCOMM_DBG("RX: autoACK seq %x\n", seqno);
  p->tmp[0] = PCOMM_PREAMBLE;
  p->tmp[1] = (PCOMM_PKT_ACK << 6) | ((seqno & 0xf) << 2) | (0);
  uint16_t crc = _crc_buf(PCOMM_INIT_CRC, &p->tmp[1], 1);
  p->tmp[2] = crc >> 8;
  p->tmp[3] = crc;
  p->cfg.tx_buf_fn(p->tmp, 4);
}

// transmit a general packet only
static void _pcomm_tx(pcomm *p, pcomm_pkt *pkt) {
  CFG_PCOMM_DBG("TX: seq %i, %s\n", pkt->seqno, pkt->pkt_type == PCOMM_PKT_NREQ_ACK ? "unsync" : "sync");
  uint16_t crc;
  p->tmp[0] = PCOMM_PREAMBLE;
  uint16_t hlen = pkt->length == 0 ? 0 : (((((pkt->length-1)>>8) + 1) << 8) | (pkt->length - 1));
  p->tmp[1] = (pkt->pkt_type << 6) | ((pkt->seqno & 0xf) << 2) | (hlen >> 8);
  if (hlen == 0) {
    crc = _crc_buf(PCOMM_INIT_CRC, &p->tmp[1], 1);
    p->tmp[2] = crc >> 8;
    p->tmp[3] = crc;
    p->cfg.tx_buf_fn(p->tmp, 4);
  } else {
    p->tmp[2] = hlen & 0xff;
    crc = _crc_buf(PCOMM_INIT_CRC, &p->tmp[1], 2);
    p->cfg.tx_buf_fn(p->tmp, 3);
    crc = _crc_buf(crc, pkt->data, pkt->length);
    p->cfg.tx_buf_fn(pkt->data, pkt->length);
    p->tmp[0] = crc >> 8;
    p->tmp[1] = crc;
    p->cfg.tx_buf_fn(p->tmp, 2);
  }
}

// transmit a general packet and request ack timer if necessary
static void _pcomm_tx_initial(pcomm *p, pcomm_pkt *pkt) {
  _pcomm_tx(p, pkt);
  if (pkt->pkt_type == PCOMM_PKT_REQ_ACK) {
    p->retry_ctr = 0;
    p->await_ack = 1;
    _pcomm_request_ack_timer(p, CFG_PCOMM_RETRY_DELTA);
  }
}

// adjust active timers from now
static void _pcomm_timers_update(pcomm *p, ptick now) {
  if (p->timer_rx_enabled) {
    ptick d = now - p->timer_rx_start_tick;
    p->timer_rx_start_tick = now;
    if (d > p->timer_rx_delta) {
      p->timer_rx_delta = 0;
    } else {
      p->timer_rx_delta -= d;
    }
  }
  if (p->timer_ack_enabled) {
    ptick d = now - p->timer_ack_start_tick;
    p->timer_ack_start_tick = now;
    if (d > p->timer_ack_delta) {
      p->timer_ack_delta = 0;
    } else {
      p->timer_ack_delta -= d;
    }
  }
}

// request the rx timer
static void _pcomm_request_rx_timer(pcomm *p, ptick delta) {
  ptick now = p->cfg.now_fn();
  _pcomm_timers_update(p, now);
  p->timer_rx_delta = delta;
  p->timer_rx_start_tick = now;
  p->timer_rx_enabled = 1;
  if (p->timer_ack_enabled) {
    if (p->timer_ack_delta <= delta) {
      // ack timer already registered and occurs before,
      // no need to start timer
    } else {
      // new delta before other timer, reset timer
      _pcomm_hal_set_timer(p, delta);
    }
  } else {
    _pcomm_hal_set_timer(p, delta);
  }
}

// request the ack timer
static void _pcomm_request_ack_timer(pcomm *p, ptick delta) {
  ptick now = p->cfg.now_fn();
  _pcomm_timers_update(p, now);
  p->timer_ack_delta = delta;
  p->timer_ack_start_tick = now;
  p->timer_ack_enabled = 1;
  if (p->timer_rx_enabled) {
    if (p->timer_rx_delta <= delta) {
      // rx timer already registered and occurs before,
      // no need to start timer
    } else {
      // new delta before other timer, reset timer
      _pcomm_hal_set_timer(p, delta);
    }
  } else {
    _pcomm_hal_set_timer(p, delta);
  }
}

// cancel the rx timer
static void _pcomm_cancel_rx_timer(pcomm *p) {
  uint8_t was_ena = p->timer_rx_enabled;
  p->timer_rx_enabled = 0;
  ptick now = p->cfg.now_fn();
  _pcomm_timers_update(p, now);
  if (was_ena) {
    if (p->timer_ack_enabled) {
      if (p->timer_ack_delta > p->timer_rx_delta) {
        // timer rx cancelled, but ack requested and happens after, recalc callback tick
        _pcomm_hal_set_timer(p, p->timer_ack_delta);
      }
    } else {
      _pcomm_hal_disable_timer(p);
    }
  }
}

// cancel the ack timer
static void _pcomm_cancel_ack_timer(pcomm *p) {
  uint8_t was_ena = p->timer_ack_enabled;
  p->timer_ack_enabled = 0;
  ptick now = p->cfg.now_fn();
  _pcomm_timers_update(p, now);
  if (was_ena) {
    if (p->timer_rx_enabled) {
      if (p->timer_rx_delta > p->timer_ack_delta) {
        // timer ack cancelled, but rx requested and happens after, recalc callback tick
        _pcomm_hal_set_timer(p, p->timer_rx_delta);
      }
    } else {
      _pcomm_hal_disable_timer(p);
    }
  }
}

// timer ack triggered
static void _pcomm_timer_trig_ack(pcomm *p) {
  if (p->await_ack) {
    p->retry_ctr++;
    if (p->retry_ctr > CFG_PCOMM_RETRIES) {
      CFG_PCOMM_DBG("TX: noACK, TMO seq %i\n", p->tx_seqno);
      p->tx_seqno++;
      p->retry_ctr = 0;
      p->await_ack = 0;
      p->cfg.timeout_fn(&p->tx_pkt);
    } else {
      CFG_PCOMM_DBG("TX: noACK, reTX seq %i, #%i\n", p->tx_seqno, p->retry_ctr);
      _pcomm_tx(p, &p->tx_pkt);
      _pcomm_request_ack_timer(p, CFG_PCOMM_RETRY_DELTA);
    }
  }
}

// timer rx triggered
static void _pcomm_timer_trig_rx(pcomm *p) {
  CFG_PCOMM_DBG("RX: pkt TMO\n");
  _pcomm_tx_nack(p, PCOMM_NACK_ERR_RX_TIMEOUT, p->rx_pkt.seqno);
  p->rx_state = PST_RX_EXP_PREAMBLE;
}

// trigger packet reception, auto ack if required and user didn't call reply in callback
static void _pcomm_trig_rx_pkt(pcomm *p) {
  switch (p->rx_pkt.pkt_type) {
  case PCOMM_PKT_ACK:
    if (p->await_ack && p->tx_seqno == p->rx_pkt.seqno) {
      CFG_PCOMM_DBG("RX: ACK seq %i\n", p->rx_pkt.seqno);
      _pcomm_cancel_ack_timer(p);
      p->await_ack = 0;
      p->tx_seqno++;
      if (p->tx_seqno > 0xf) p->tx_seqno = 1;
      p->cfg.rx_pkt_ack_fn(p->rx_pkt.seqno, p->rx_pkt.data, p->rx_pkt.length);
    } else {
      CFG_PCOMM_DBG("RX: ACK unkn seq %i\n", p->rx_pkt.seqno);
    }
    break;
  case PCOMM_PKT_NACK:
    if (p->await_ack && p->tx_seqno == p->rx_pkt.seqno) {
      if (p->rx_pkt.data[0] != PCOMM_NACK_ERR_NOT_READY &&
          p->rx_pkt.data[0] != PCOMM_NACK_ERR_NOT_PREAMBLE) {
        CFG_PCOMM_DBG("RX: NACK seq %i, err %i, reTX direct\n", p->rx_pkt.seqno, p->rx_pkt.data[0]);
        _pcomm_cancel_ack_timer(p);
        _pcomm_tx_initial(p, &p->tx_pkt);
      } else {
        CFG_PCOMM_DBG("RX: NACK seq %i, err %i\n", p->rx_pkt.seqno, p->rx_pkt.data[0]);
      }
    } else {
      CFG_PCOMM_DBG("RX: NACK unkn seq %i, err %i\n", p->rx_pkt.seqno, p->rx_pkt.data[0]);
    }
    break;
  case PCOMM_PKT_NREQ_ACK:
  case PCOMM_PKT_REQ_ACK:
  {
    uint8_t exp_ack = p->rx_pkt.pkt_type == PCOMM_PKT_REQ_ACK;
    CFG_PCOMM_DBG("RX: seq %i, %s\n", p->rx_pkt.seqno, exp_ack ? "sync" : "unsync");
    uint8_t rx_seqno = p->rx_pkt.seqno;
    p->rx_user_acked = 0;
    p->cfg.rx_pkt_fn(&p->rx_pkt);
    if (exp_ack && !p->rx_user_acked) {
      CFG_PCOMM_DBG("RX: autoACK\n");
      // auto ack
      _pcomm_tx_ack_empty(p, rx_seqno);
    }
    break;
  }
  }
}

// parse an rx char
static void _pcomm_parse_char(pcomm *p, uint8_t c) {
  switch (p->rx_state) {
  case PST_RX_EXP_PREAMBLE:
    if (c == PCOMM_PREAMBLE) {
      p->rx_pkt.seqno = 0;
      _pcomm_request_rx_timer(p, CFG_PCOMM_RX_TIMEOUT);
      p->rx_state = PST_RX_EXP_HDR_HI;
    } else {
      p->rx_state = PST_RX_NOT_PREAMBLE;
    }
    break;
  case PST_RX_NOT_PREAMBLE:
    if (c == PCOMM_PREAMBLE) {
      _pcomm_tx_nack(p, PCOMM_NACK_ERR_NOT_PREAMBLE, 0x0);
      p->rx_pkt.seqno = 0;
      _pcomm_request_rx_timer(p, CFG_PCOMM_RX_TIMEOUT);
      p->rx_state = PST_RX_EXP_HDR_HI;
    }
    break;
  case PST_RX_EXP_HDR_HI:
    p->rx_pkt.pkt_type = (c >> 6) & 0x3;
    p->rx_pkt.seqno = (c >> 2) & 0xf;
    p->rx_pkt.length = c & 0x3;
    p->rx_local_crc = _crc_ccitt_16(PCOMM_INIT_CRC, c);
    p->rx_state = p->rx_pkt.length == 0 ? PST_RX_CRC_HI : PST_RX_EXP_HDR_LO;
    break;
  case PST_RX_EXP_HDR_LO:
    p->rx_pkt.length = ((p->rx_pkt.length - 1) << 8) | (c + 1);
    p->rx_local_crc = _crc_ccitt_16(p->rx_local_crc, c);
    p->rx_data_cnt = 0;
    p->rx_state = PST_RX_DATA;
    break;
  case PST_RX_DATA:
    p->rx_pkt.data[p->rx_data_cnt++] = c;
    p->rx_local_crc = _crc_ccitt_16(p->rx_local_crc, c);
    if (p->rx_data_cnt >= p->rx_pkt.length) {
      p->rx_state = PST_RX_CRC_HI;
    }
    break;
  case PST_RX_CRC_HI:
    p->rx_pkt.crc = (c<<8);
    p->rx_state = PST_RX_CRC_LO;
    break;
  case PST_RX_CRC_LO:
    _pcomm_cancel_rx_timer(p);
    p->rx_pkt.crc |= c;
    if (p->rx_pkt.crc != p->rx_local_crc) {
      _pcomm_tx_nack(p, PCOMM_NACK_ERR_BAD_CRC, p->rx_pkt.seqno);
    } else {
      _pcomm_trig_rx_pkt(p);
    }
    p->rx_state = PST_RX_EXP_PREAMBLE;
    break;
  }
}


void pcomm_init(pcomm *p, pcomm_cfg *cfg, uint8_t *rx_buffer) {
  memset(p, 0, sizeof(pcomm));
  memcpy(&p->cfg, cfg, sizeof(pcomm_cfg));
  p->rx_pkt.data = rx_buffer;
  p->tx_seqno = 1;
}

void pcomm_tick(pcomm *p) {
  ptick now = p->cfg.now_fn();
  _pcomm_timers_update(p, now);
  p->timer_enabled = 0;

  if (p->timer_rx_enabled && p->timer_rx_delta == 0) {
    p->timer_rx_enabled = 0;
    _pcomm_timer_trig_rx(p);
    if (p->timer_ack_enabled && p->timer_ack_delta > 0) {
      // recalc lingering ack timer
      _pcomm_request_ack_timer(p, p->timer_ack_delta);
    }
  }
  if (p->timer_ack_enabled && p->timer_ack_delta == 0) {
    p->timer_ack_enabled = 0;
    _pcomm_timer_trig_ack(p);
    if (p->timer_rx_enabled && p->timer_rx_delta > 0) {
      // recalc lingering rx timer
      _pcomm_request_rx_timer(p, p->timer_rx_delta);
    }
  }
}

int pcomm_tx_pkt(pcomm *p, uint8_t ack, uint8_t *buf, uint16_t len) {
  if (p->await_ack && ack) {
    CFG_PCOMM_DBG("TX: ERR user send sync while BUSY\n");
    return -1; // TODO busy, some error
  }
  pcomm_pkt *pkt = &p->tx_pkt;
  pkt->pkt_type = ack ? PCOMM_PKT_REQ_ACK : PCOMM_PKT_NREQ_ACK;
  pkt->data = buf;
  pkt->length = len;
  pkt->seqno = ack ? p->tx_seqno : 0;
  _pcomm_tx_initial(p, pkt);
  return 0;
}

int pcomm_tx_reply_ack(pcomm *p, uint8_t *buf, uint16_t len) {
  if (p->rx_pkt.pkt_type != PCOMM_PKT_REQ_ACK) {
    CFG_PCOMM_DBG("TX: ERR user send ack wrong state\n");
    return -1; // TODO not within rx call, some error
  }
  p->rx_user_acked = 1;
  // reuse rx pkt as an ack
  p->rx_pkt.pkt_type = PCOMM_PKT_ACK;
  p->rx_pkt.data = buf;
  p->rx_pkt.length = len;
  _pcomm_tx_initial(p, &p->rx_pkt);
  return 0;
}

void pcomm_report_rx_byte(pcomm *p, uint8_t c) {
  _pcomm_parse_char(p, c);
}

void pcomm_report_rx_buf(pcomm *p, uint8_t *buf, uint16_t len) {
  while (len--) {
    _pcomm_parse_char(p, *buf++);
  }
}
