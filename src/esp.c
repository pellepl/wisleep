/*
 * esp_flash_tunnel.c
 *
 *  Created on: Jun 27, 2016
 *      Author: petera
 */

#include "esp.h"

#include "system.h"
#include "io.h"
#include "gpio.h"
#include "app.h"
#include "uart_driver.h"
#include "taskq.h"
#include "miniutils.h"

#define INACTIVITY_MONITOR_POLL_MS  200
#define INACTIVITY_TMO_MS           1500

static bool active = FALSE;
static task *monitor_task;
static task_timer monitor_timer;
static sys_time last_tick;
static u32_t blink;
static u32_t recv;

static void blink_update() {
  blink++;
  if ((blink >> (recv > 10*1024 ? 0 : 2)) & 1) {
    gpio_enable(PIN_LED);
  } else {
    gpio_disable(PIN_LED);
  }
}

static void esp_programmer_rx_pin_irq(gpio_pin pin) {
  last_tick = SYS_get_time_ms();
  recv++;
}


static void monitor_tick(u32_t arg, void *arg_p) {
  sys_time now = SYS_get_time_ms();
  if (now - last_tick > INACTIVITY_TMO_MS) {
    esp_flash_done();
  } else {
    blink_update();
  }
}

void esp_powerup(bool boot_flash) {
  gpio_config(PIN_WIFI_ENA, CLK_2MHZ, OUT, AF0, PUSHPULL, NOPULL);
  gpio_config(PIN_WIFI_BOOT, CLK_2MHZ, OUT, AF0, PUSHPULL, NOPULL);
  gpio_config(PIN_WIFI_RESET, CLK_2MHZ, OUT, AF0, PUSHPULL, NOPULL);
  gpio_config(PIN_WIFI_BL2, CLK_2MHZ, OUT, AF0, PUSHPULL, NOPULL);
  gpio_config(PIN_WIFI_BL15, CLK_2MHZ, OUT, AF0, PUSHPULL, NOPULL);
  gpio_config(PIN_WIFI_IO5, CLK_2MHZ, IN, AF0, OPENDRAIN, NOPULL);
  gpio_config(PIN_WIFI_IO4, CLK_2MHZ, IN, AF0, OPENDRAIN, NOPULL);

  gpio_enable(PIN_WIFI_BL2);
  gpio_disable(PIN_WIFI_BL15);
  gpio_enable(PIN_WIFI_ENA);

  gpio_disable(PIN_WIFI_RESET);

  if (boot_flash) {
    gpio_disable(PIN_WIFI_BOOT);
  } else {
    gpio_enable(PIN_WIFI_BOOT);
  }

  gpio_enable(PIN_POW_WIFI);

  SYS_hardsleep_ms(75);
  gpio_enable(PIN_WIFI_RESET);
  if (boot_flash) {
    SYS_hardsleep_ms(75);
    gpio_enable(PIN_WIFI_BOOT);
  }
}

void esp_flash_start(u32_t ignore, void *ignore_p) {
  if (!active) {
    TRACE_USR_MSG(0);
    active = TRUE;

    // start timer
    monitor_task = TASK_create(monitor_tick, TASK_STATIC);

    // disable esp programming uart
    UART_config(_UART(0), ESP_FLASH_PROGRAMMING_BAUD_RATE, UART_DATABITS_8,
        UART_STOPBITS_1, UART_PARITY_NONE, UART_FLOWCONTROL_NONE, FALSE);

    gpio_config(PIN_WIFI_UART_TX, CLK_2MHZ, IN, AF0, OPENDRAIN, NOPULL);
    gpio_config(PIN_WIFI_UART_RX, CLK_50MHZ, IN, AF0, OPENDRAIN, NOPULL);

    gpio_interrupt_config(PIN_WIFI_UART_TX, esp_programmer_rx_pin_irq, FLANK_UP);
    gpio_interrupt_mask_enable(PIN_WIFI_UART_TX, TRUE);

    print("esp flash...\n");
  }

  blink = 0;
  recv = 0;
  last_tick = SYS_get_time_ms();

  TASK_stop_timer(&monitor_timer);
  TASK_start_timer(monitor_task, &monitor_timer, 0, 0, 0, INACTIVITY_MONITOR_POLL_MS, "flashmon");

  // put esp to flash boot state
  esp_powerup(TRUE);
}

void esp_flash_done(void) {
  if (active) {
    // stop timer
    TASK_stop_timer(&monitor_timer);
    TASK_free(monitor_task);

    // reenable and reset uart speed to original
    //gpio_interrupt_mask_disable(PIN_WIFI_UART_TX);

    gpio_config(PIN_WIFI_UART_TX, CLK_50MHZ, AF, AF0, PUSHPULL, NOPULL);
    gpio_config(PIN_WIFI_UART_RX, CLK_50MHZ, IN, AF0, OPENDRAIN, NOPULL);

    UART_config(_UART(0), UART1_SPEED, UART_DATABITS_8,
        UART_STOPBITS_1, UART_PARITY_NONE, UART_FLOWCONTROL_NONE, TRUE);

    active = FALSE;
    gpio_disable(PIN_LED);
    esp_powerup(FALSE);
    print("esp flash done\n");
  }
}
