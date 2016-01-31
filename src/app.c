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
#include <stdarg.h>

#ifdef CONFIG_I2C
task_mutex i2c_mutex = TASK_MUTEX_INIT;
#endif

static u8_t cli_buf[16];
volatile bool cli_rd;

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
    TASK_run(t, IO_rx_available(io), (void *)((u32_t)io));
    cli_rd = TRUE;
  }
}

void APP_init(void) {
  gpio_enable(PIN_LED);
  WS2812B_STM32F1_init(NULL);
  cli_rd = FALSE;
  IO_set_callback(IOSTD, cli_rx_avail, NULL);
}

void APP_shutdown(void) {
}

void APP_dump(void) {
  print("APP specifics\n-------------\n");
  print("I2C mutex\n");
  print("  taken: %s\n", i2c_mutex.taken ? "YES":"NO");
  if (i2c_mutex.taken) {
    print("  entries: %i\n", i2c_mutex.entries);
    print("  owner:   0x%08x  [func:0x%08x]\n", i2c_mutex.owner, i2c_mutex.owner->f);
    print("  reentr.: %s\n", i2c_mutex.reentrant ? "YES":"NO");
  }
  print("\n");
}

CLI_EXTERN_MENU(system)
CLI_EXTERN_MENU(bus)
CLI_EXTERN_MENU(dev)

CLI_MENU_START_MAIN
CLI_SUBMENU(system, "sys", "System submenu")
CLI_SUBMENU(bus, "bus", "Processor busses submenu")
CLI_SUBMENU(dev, "dev", "Peripheral devices submenu")
CLI_FUNC("help", cli_help, "Prints help")
CLI_MENU_END
