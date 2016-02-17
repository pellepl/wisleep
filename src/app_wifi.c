/*
 * app_wifi.c
 *
 *  Created on: Feb 16, 2016
 *      Author: petera
 */

#include "system.h"
#include "protocol.h"
#include "io.h"
#include "taskq.h"

static volatile bool com_uart_rd;

static u8_t rx_buf[768];
static pcomm com;
static task_timer com_timer;
static task *com_timer_task;

static void rx_pkt(u32_t a, void *p);

static void com_impl_request_future_tick(ptick delta) {
  TASK_start_timer(com_timer_task, &com_timer, 0, NULL, delta, 0, "comtim");
}

static void com_impl_cancel_future_tick(void) {
  TASK_stop_timer(&com_timer);
}

static ptick com_impl_now_tick(void) {
  return SYS_get_time_ms();
}

static void com_impl_tx_byte(u8_t c) {
  IO_put_char(IOWIFI, c);
}

static void com_impl_tx_buf(u8_t *b, u16_t len) {
  IO_put_buf(IOWIFI, b, len);
}

static void com_impl_rx_pkt(pcomm_pkt *pkt) {
  task *rx_task = TASK_create(rx_pkt, 0);
  TASK_run(rx_task, 0, pkt);
}

static void com_impl_tx_pkt_acked(u8_t seqno, u8_t *data, u16_t len) {

}

static void com_impl_timeout(pcomm_pkt *pkt) {

}

static void task_tick(u32_t a, void *p) {
  pcomm_tick(&com);
}

static void rx_pkt(u32_t a, void *p) {
  pcomm_pkt *pkt = (pcomm_pkt *)p;

}

static void com_task_on_input(u32_t io, void *p) {
  while (IO_rx_available(io)) {
    u8_t chunk[32];
    u32_t rlen = IO_get_buf(io, chunk, MIN(IO_rx_available(io), sizeof(chunk)));
    pcomm_report_rx_buf(&com, chunk, rlen);
  }
  com_uart_rd = FALSE;
}

static void com_rx_avail_irq(u8_t io, void *arg, u16_t available) {
  if (!com_uart_rd) {
    task *t = TASK_create(com_task_on_input, 0);
    TASK_run(t, io, NULL);
    com_uart_rd = TRUE;
  }
}

void WB_init(void) {
  IO_assure_tx(IOWIFI, TRUE);
  com_uart_rd = FALSE;
  UART_config(
      _UART(UARTWIFIIN),
      921600,
      UART_DATABITS_8,
      UART_STOPBITS_1,
      UART_PARITY_NONE,
      UART_FLOWCONTROL_NONE,
      TRUE);
  com_timer_task = TASK_create(task_tick, TASK_STATIC);
  pcomm_cfg cfg = {
      .timer_fn = com_impl_request_future_tick,
      .cancel_timer_fn = com_impl_cancel_future_tick,
      .now_fn = com_impl_now_tick,
      .tx_byte_fn = com_impl_tx_byte,
      .tx_buf_fn = com_impl_tx_buf,
      .rx_pkt_fn = com_impl_rx_pkt,
      .rx_pkt_ack_fn = com_impl_tx_pkt_acked,
      .timeout_fn = com_impl_timeout
  };
  pcomm_init(&com, &cfg, rx_buf);
  IO_set_callback(IOWIFI, com_rx_avail_irq, NULL);
}
