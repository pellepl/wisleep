/*
 * app.c
 *
 *  Created on: Jan 2, 2014
 *      Author: petera
 */

#include "app.h"
#include "io.h"
#include "cli.h"
#include "taskq.h"
#include "miniutils.h"
#include "gpio.h"
#include "ws2812b_spi_stm32f1.h"
#include "linker_symaccess.h"
#include "wdog.h"
#include <stdarg.h>

static u8_t cli_buf[16];
volatile bool cli_rd;
static task_timer wdog_timer;

static void cli_task_on_input(u32_t len, void *p) {
  u8_t io = (u8_t)((u32_t)p);
  while (IO_rx_available(io)) {
    u32_t rlen = IO_get_buf(io, cli_buf, MIN(IO_rx_available(io), sizeof(cli_buf)));
    cli_recv((char *)cli_buf, rlen);
  }
  cli_rd = FALSE;
}

static void cli_rx_avail(u8_t io, void *arg, u16_t available) {
  if (!cli_rd) {
    task *t = TASK_create(cli_task_on_input, 0);
    TASK_run(t, 0, (void *)((u32_t)io));
    cli_rd = TRUE;
  }
}

static void wdog_feeder_f(u32_t ignore, void *ignore_more) {
  WDOG_feed();
}

void APP_init(void) {
  WDOG_start(11);
  task *feeder = TASK_create(wdog_feeder_f, TASK_STATIC);
  TASK_start_timer(feeder, &wdog_timer, 0, 0, 0, 10000, "wdog");

  gpio_enable(PIN_LED);
  WS2812B_STM32F1_init(NULL);
  cli_rd = FALSE;
  IO_set_callback(IOSTD, cli_rx_avail, NULL);
}

void APP_shutdown(void) {
}

void APP_dump(void) {
  print("APP specifics\n-------------\n");
  print("\n");
}

CLI_EXTERN_MENU(common)

CLI_MENU_START_MAIN
CLI_EXTRAMENU(common)
CLI_FUNC("help", cli_help, "Prints help")
CLI_MENU_END
