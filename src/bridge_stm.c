/*
 * app_wifi.c
 *
 *  Created on: Feb 16, 2016
 *      Author: petera
 */

#include "system.h"
#include "io.h"
#include "taskq.h"

#include "umac.h"
#include "cli.h"

#include "protocol.h"

#include "lamp.h"

static volatile bool um_uart_rd;

static u8_t dbg_buf[64];
static u8_t dbg_ix = 0;
static bool dbg_add = FALSE;
static u8_t rx_buf[768];
static u8_t tx_buf[768];
static u8_t tx_ack_buf[768];
static umac um;
static task_timer umac_timer;
static task *umac_timer_task;
static u32_t ping_val;
static sys_time ping_snd;

static u8_t * u32tomem(u8_t *b, uint32_t v) {
  *b++ = v>>24;
  *b++ = v>>16;
  *b++ = v>>8;
  *b++ = v;
  return b;
}

static u8_t * u16tomem(u8_t *b, uint32_t v) {
  *b++ = v>>8;
  *b++ = v;
  return b;
}

static u32_t memtou32(u8_t *b) {
  return (b[0] << 24) | (b[1] << 16) | (b[2] << 8) | b[3];
}

static u16_t memtou16(u8_t *b) {
  return (b[0] << 8) | b[1];
}

static void um_impl_request_future_tick(umtick delta) {
  TASK_start_timer(umac_timer_task, &umac_timer, 0, NULL, delta, 0, "umtim");
}

static void um_impl_cancel_future_tick(void) {
  TASK_stop_timer(&umac_timer);
}

static umtick um_impl_now_tick(void) {
  return SYS_get_time_ms();
}

static void um_impl_tx_byte(u8_t c) {
  IO_put_char(IOWIFI, c);
}

static void um_impl_tx_buf(u8_t *b, u16_t len) {
  IO_put_buf(IOWIFI, b, len);
}

static void um_impl_tx_pkt_acked(u8_t seqno, u8_t *data, u16_t len) {
  if (len == 0) return;
  switch(data[0]) {
  case P_ESP_HELLO: {
    sys_time ping_rcv = SYS_get_time_ms();
    u32_t rec_ping_val = memtou32(&data[1]);
    if (rec_ping_val == ping_val) {
      print("PONG ok, turnaround %i ms\n", ping_rcv - ping_snd);
    } else {
      print("PONG bad, turnaround %i ms\n", ping_rcv - ping_snd);
    }
  }
    break;
  default:
    break;
  }
}

static void um_impl_timeout(umac_pkt *pkt) {
  if (pkt->length == 0) return;
  if (pkt->data[0] == P_ESP_HELLO) {
    print("PONG missed\n");
  }

}

static void um_impl_nonprotocol_data(uint8_t c) {
  if (c == '\n') {
    if (!dbg_add) {
      print("[ESPDBG] ");
    }
    IO_put_buf(IODBG, dbg_buf, dbg_ix);
    print("\n");
    dbg_ix = 0;
    dbg_add = FALSE;
    return;
  }

  if (dbg_ix >= sizeof(dbg_buf)-1) {
    if (!dbg_add) {
      print("[ESPDBG] ");
    }
    IO_put_buf(IODBG, dbg_buf, dbg_ix);
    dbg_ix = 0;
    dbg_add = TRUE;
  }
  dbg_buf[dbg_ix++] = c;
}

static void task_tick(u32_t a, void *p) {
  umac_tick(&um);
}

static void um_impl_rx_pkt(umac_pkt *pkt) {
  print("pkt %02x\n", pkt->data[0]);
  switch (pkt->data[0])  {
  case P_STM_HELLO: {
    memcpy(tx_ack_buf, pkt->data, pkt->length);
    umac_tx_reply_ack(&um, tx_ack_buf, pkt->length);
  }
  break;
  case P_STM_LAMP_ENA: {
    LAMP_enable(pkt->data[1] != 0);
  }
  break;
  case P_STM_LAMP_INTENSITY: {
    u32_t i = pkt->data[1];
    if (i < LAMP_MIN_INTENSITY) {
      LAMP_enable(FALSE);
    } else {
      LAMP_set_intensity(i);
      LAMP_enable(TRUE);
    }
  }
  break;
  case P_STM_LAMP_COLOR: {
    u32_t rgb = (pkt->data[1] << 16) | (pkt->data[2] << 8) | (pkt->data[3]);
    LAMP_set_color(rgb);
  }
  break;
  case P_STM_LAMP_STATUS: {
    LAMP_enable(pkt->data[1] != 0);
    LAMP_set_intensity(pkt->data[2]);
    LAMP_set_color((pkt->data[3] << 16) | (pkt->data[4] << 8) | (pkt->data[5]));
  }
  break;
  case P_STM_LAMP_GET_ENA: {
    u16_t ix = 0;
    tx_ack_buf[ix++] = pkt->data[0];
    tx_ack_buf[ix++] = LAMP_on();
    umac_tx_reply_ack(&um, tx_ack_buf, ix);
  }
  break;
  case P_STM_LAMP_GET_INTENSITY: {
    u16_t ix = 0;
    tx_ack_buf[ix++] = pkt->data[0];
    tx_ack_buf[ix++] = LAMP_get_intensity();
    umac_tx_reply_ack(&um, tx_ack_buf, ix);
  }
  break;
  case P_STM_LAMP_GET_COLOR: {
    u16_t ix = 0;
    u32_t rgb = LAMP_get_color();
    tx_ack_buf[ix++] = pkt->data[0];
    tx_ack_buf[ix++] = rgb>>16;
    tx_ack_buf[ix++] = rgb>>8;
    tx_ack_buf[ix++] = rgb;
    umac_tx_reply_ack(&um, tx_ack_buf, ix);
  }
  break;
  case P_STM_LAMP_GET_STATUS: {
    u16_t ix = 0;
    u32_t rgb = LAMP_get_color();
    tx_ack_buf[ix++] = pkt->data[0];
    tx_ack_buf[ix++] = LAMP_on();
    tx_ack_buf[ix++] = LAMP_get_intensity();
    tx_ack_buf[ix++] = rgb>>16;
    tx_ack_buf[ix++] = rgb>>8;
    tx_ack_buf[ix++] = rgb;
    print("tx lamp ena:%i int:%i rgb:%02x%02x%02x\n", tx_ack_buf[1], tx_ack_buf[2], tx_ack_buf[3], tx_ack_buf[4], tx_ack_buf[5]);
    umac_tx_reply_ack(&um, tx_ack_buf, ix);
  }
  break;
  case P_STM_RECV_UDP: {
    //u32_t ipaddr = memtou32(&pkt->data[1]);
    u16_t udp_len = pkt->length-5;
    print("Got UDP data from %i.%i.%i.%i, %i bytes\n", pkt->data[4], pkt->data[3], pkt->data[1], pkt->data[1], udp_len);
    printbuf(IOSTD, &pkt->data[5], udp_len);
    break;
  }

  default:
    print("unhandled pkt %02x\n", pkt->data[0]);
  break;
  }
}

static void um_task_on_input(u32_t io, void *p) {
  while (IO_rx_available(io)) {
    u8_t chunk[32];
    u32_t rlen = IO_get_buf(io, chunk, MIN(IO_rx_available(io), sizeof(chunk)));
    umac_report_rx_buf(&um, chunk, rlen);
  }
  um_uart_rd = FALSE;
}

static void um_rx_avail_irq(u8_t io, void *arg, u16_t available) {
  if (!um_uart_rd) {
    task *t = TASK_create(um_task_on_input, 0);
    TASK_run(t, io, NULL);
    um_uart_rd = TRUE;
  }
}

void WB_init(void) {
  IO_assure_tx(IOWIFI, TRUE);
  um_uart_rd = FALSE;
  UART_config(
      _UART(UARTWIFIIN),
      921600,
      UART_DATABITS_8,
      UART_STOPBITS_1,
      UART_PARITY_NONE,
      UART_FLOWCONTROL_NONE,
      TRUE);
  umac_timer_task = TASK_create(task_tick, TASK_STATIC);
  umac_cfg cfg = {
      .timer_fn = um_impl_request_future_tick,
      .cancel_timer_fn = um_impl_cancel_future_tick,
      .now_fn = um_impl_now_tick,
      .tx_byte_fn = um_impl_tx_byte,
      .tx_buf_fn = um_impl_tx_buf,
      .rx_pkt_fn = um_impl_rx_pkt,
      .rx_pkt_ack_fn = um_impl_tx_pkt_acked,
      .timeout_fn = um_impl_timeout,
      .nonprotocol_data_fn = um_impl_nonprotocol_data
  };
  umac_init(&um, &cfg, rx_buf);
  IO_set_callback(IOWIFI, um_rx_avail_irq, NULL);
}


static s32_t cli_udp_tx(u32_t argc) {
  tx_buf[0] = P_ESP_SEND_UDP;
  u32tomem(&tx_buf[1], 0xffffff);
  u16tomem(&tx_buf[5], 12345);
  sprint((char *)&tx_buf[7], "Hello world\n");
  umac_tx_pkt(&um, TRUE, tx_buf, 8+12);
  return CLI_OK;
}

static s32_t cli_udp_rx(u32_t argc) {
  tx_buf[0] = P_ESP_RECV_UDP;
  u32tomem(&tx_buf[1], 0xffffff);
  u16tomem(&tx_buf[5], 12345);
  u16tomem(&tx_buf[7], 30000);
  umac_tx_pkt(&um, TRUE, tx_buf, 9);
  return CLI_OK;
}

static s32_t cli_hello(u32_t argc) {
  ping_val = rand_next();
  tx_buf[0] = P_ESP_HELLO;
  u32tomem(&tx_buf[1], ping_val);
  umac_tx_pkt(&um, TRUE, tx_buf, 5);
  ping_snd = SYS_get_time_ms();
  return CLI_OK;
}

CLI_MENU_START(wifi)
CLI_FUNC("ping", cli_hello, "Pings ESP8266")
CLI_FUNC("udp_tx", cli_udp_tx, "Test send an UDP broadcast to port 12345")
CLI_FUNC("udp_rx", cli_udp_rx, "Test receive an UDP broadcast to port 12345")
CLI_MENU_END

