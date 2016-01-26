/*
 * app.c
 *
 *  Created on: Jan 2, 2014
 *      Author: petera
 */

#include "app.h"
#include "cli.h"
#include "taskq.h"
#include "miniutils.h"
#include "gpio.h"
#include "ws2812b_spi_stm32f1.h"

#ifdef CONFIG_I2C
task_mutex i2c_mutex = TASK_MUTEX_INIT;
#endif

static u8_t cli_buf[32];

static void cli_task_on_input(u32_t len, void *p) {
  u32_t rlen = UART_get_buf(_UART(UARTSTDIN), cli_buf, MIN(len, sizeof(cli_buf)));
  cli_parse((char *)cli_buf, rlen);
}


static void cli_uart_check_char(void *a, u8_t c) {
  if (c == '\n') {
    task *t = TASK_create(cli_task_on_input, 0);
    TASK_run(t, UART_rx_available(_UART(UARTSTDIN)), NULL);
  }
}

void APP_init(void) {
  gpio_enable(PIN_LED);
  WS2812B_STM32F1_init(NULL);
  UART_set_callback(_UART(UARTSTDIN), cli_uart_check_char, NULL);
}

void APP_shutdown(void) {
}

static s32_t cli_reset(u32_t argc) {
  SYS_reboot(REBOOT_USER);
  return CLI_OK;
}

CLI_MENU_START_MAIN
CLI_FUNC("reset", cli_reset, "Resets device")
CLI_FUNC("help", cli_help, "Prints help")
CLI_MENU_END
