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
#include "rtc.h"
#include "sensor.h"
#include <stdarg.h>

static volatile u8_t cpu_claims;
static u8_t cli_buf[16];
volatile bool cli_rd;
static task_timer heartbeat_timer;
static task_timer temp_timer;
static task *cli_tmo_task;
static task_timer cli_tmo_timer;
static bool was_uart_connected = FALSE;
static u64_t cli_tmo_last_time = 0;
static bool cli_claimed = FALSE;

static void app_cli_claim(void) {
  APP_claim(CLAIM_CLI);
  cli_claimed = TRUE;
  TASK_start_timer(cli_tmo_task, &cli_tmo_timer, 0, 0, 0, APP_CLI_POLL_MS, "cli");
  cli_tmo_last_time = RTC_get_tick();
}

static void app_cli_release(void) {
  APP_release(CLAIM_CLI);
  TASK_stop_timer(&cli_tmo_timer);
  cli_claimed = FALSE;
}

static bool app_detect_uart(void) {
  gpio_config(PORTA, PIN3, CLK_50MHZ, IN, AF0, OPENDRAIN, PULLDOWN);
  SYS_hardsleep_ms(2); // wait for pin to stabilize
  bool res = gpio_get(PORTA, PIN3);
  // reset to uart config
  gpio_config(PORTA, PIN3, CLK_50MHZ, IN, AF0, OPENDRAIN, NOPULL);
  return res;
}

static void cli_task_on_input(u32_t len, void *p) {
  u8_t io = (u8_t)((u32_t)p);
  while (IO_rx_available(io)) {
    u32_t rlen = IO_get_buf(io, cli_buf, MIN(IO_rx_available(io), sizeof(cli_buf)));
    cli_recv((char *)cli_buf, rlen);
  }
  cli_rd = FALSE;
  cli_tmo_last_time = RTC_get_tick();
}

static void cli_tmo(u32_t a, void *p) {
  if (cli_claimed) {
    u64_t tick_now = RTC_get_tick();
    if (tick_now - cli_tmo_last_time > RTC_S_TO_TICK(APP_CLI_INACT_SHUTDOWN_S)) {
      app_cli_release();
    }
  }
}

static void cli_rx_avail_irq(u8_t io, void *arg, u16_t available) {
  if (!cli_rd) {
    task *t = TASK_create(cli_task_on_input, 0);
    TASK_run(t, 0, (void *)((u32_t)io));
    cli_rd = TRUE;
  }
}

static void heartbeat(u32_t ignore, void *ignore_more) {
  gpio_disable(PIN_LED);

  WDOG_feed();

  bool is_uart_connected = app_detect_uart();
  if (is_uart_connected && !was_uart_connected && !cli_claimed) {
    DBG(D_APP, D_DEBUG, "UART reconnected\n");
    app_cli_claim();
  }
  was_uart_connected = is_uart_connected;

  gpio_enable(PIN_LED);
}

static void read_temp(u32_t ignore, void *ignore_more) {
  SENS_read_temp();
}

static void sleep_stop_restore(void)
{
  // Enable HSE
  RCC_HSEConfig(RCC_HSE_ON);

  // Wait till HSE is ready
  ErrorStatus HSEStartUpStatus;
  HSEStartUpStatus = RCC_WaitForHSEStartUp();
  ASSERT(HSEStartUpStatus == SUCCESS);
  // Enable PLL
  RCC_PLLCmd(ENABLE);

  // Wait till PLL is ready
  while(RCC_GetFlagStatus(RCC_FLAG_PLLRDY) == RESET);
  // Select PLL as system clock source
  RCC_SYSCLKConfig(RCC_SYSCLKSource_PLLCLK);
  // Wait till PLL is used as system clock source
  while(RCC_GetSYSCLKSource() != 0x08);
}

static void app_spin(void) {
  while (1) {
    u64_t cur_tick = RTC_get_tick();
    IF_DBG(D_APP, D_DEBUG) {
      rtc_datetime dt;
      RTC_get_date_time(&dt);
      DBG(D_APP, D_DEBUG, "%04i-%02i-%02i %02i:%02i:%02i.%03i %08x %08x\n",
          dt.date.year,
          dt.date.month + 1,
          dt.date.month_day,
          dt.time.hour,
          dt.time.minute,
          dt.time.second,
          dt.time.millisecond,
          (u32_t)((cur_tick)>>32), (u32_t)(cur_tick)
          );
    }

    // execute all pending tasks
    while (TASK_tick());

    // get nearest timer
    time wakeup_ms;
    volatile u64_t wu_tick = (u64_t)(-1ULL);
    task_timer *timer;
    s32_t no_wakeup = TASK_next_wakeup_ms(&wakeup_ms, &timer);
    s64_t diff_tick = 0;
    if (!no_wakeup) {
      wu_tick = RTC_MS_TO_TICK(wakeup_ms);
      diff_tick = wu_tick - cur_tick;
      ASSERT(diff_tick < 0x100000000LL);
      if (diff_tick <= 0) {
        // got at least one timer that already should've triggered: fire and loop
        TASK_timer();
        continue;
      }
    }

    if (no_wakeup) {
      // wait forever
      RTC_cancel_alarm();
    } else {
      // wake us at timer value
      RTC_set_alarm_tick(wu_tick);
      DBG(D_APP, D_DEBUG, "RTC alarm @ %08x %08x from timer %s\n",
          (u32_t)(wu_tick>>32),(u32_t)(wu_tick), timer->name);
    }

    if (cpu_claims || (!no_wakeup && diff_tick < RTC_MS_TO_TICK(APP_PREVENT_SLEEP_IF_LESS_MS))) {
      // resources held or too soon to wake up to go deep-sleep
      DBG(D_APP, D_INFO, "..snoozing for %i ms, %i resources claimed\n", (u32_t)(wakeup_ms - RTC_TICK_TO_MS(RTC_get_tick())), cpu_claims);
      //print("..snoozing for %i ms, %i resources claimed\n", (u32_t)(wakeup_ms - RTC_TICK_TO_MS(RTC_get_tick())), cpu_claims);
      while (RTC_get_tick() <= wu_tick && cpu_claims && !TASK_tick()) {
        __WFI();
      }
    } else {
      // no one holding any resource, sleep
      DBG(D_APP, D_INFO, "..sleeping for %i ms\n   ", (u32_t)(wakeup_ms - RTC_TICK_TO_MS(RTC_get_tick())));
      //print("..sleeping for %i ms\n  ", (u32_t)(wakeup_ms - RTC_TICK_TO_MS(RTC_get_tick())));
      irq_disable();
      IO_tx_flush(IOSTD);
      irq_enable();

      PWR_ClearFlag(PWR_FLAG_PVDO);
      PWR_ClearFlag(PWR_FLAG_WU);
      PWR_ClearFlag(PWR_FLAG_SB);

      EXTI_ClearITPendingBit(EXTI_Line17);
      RTC_ClearITPendingBit(RTC_IT_ALR);
      RTC_WaitForLastTask();

      // sleep
      PWR_EnterSTOPMode(PWR_Regulator_ON, PWR_STOPEntry_WFI);

      // wake, reconfigure
      sleep_stop_restore();

      DBG(D_APP, D_DEBUG, "awaken from sleep\n");
    }

    // check if any timers fired and insert them into task queue
    TASK_timer();
  } // while forever
}

void APP_init(void) {
  WDOG_start(APP_WDOG_TIMEOUT_S);

  if (PWR_GetFlagStatus(PWR_FLAG_WU) == SET) {
    PWR_ClearFlag(PWR_FLAG_WU);
    print("PWR_FLAG_WU\n");
  }
  if (PWR_GetFlagStatus(PWR_FLAG_SB) == SET) {
    PWR_ClearFlag(PWR_FLAG_SB);
    print("PWR_FLAG_SB\n");
  }
  if (PWR_GetFlagStatus(PWR_FLAG_PVDO) == SET) {
    PWR_ClearFlag(PWR_FLAG_PVDO);
    print("PWR_FLAG_PVDO\n");
  }
  if (RCC_GetFlagStatus(RCC_FLAG_SFTRST) == SET) print("SFT\n");
  if (RCC_GetFlagStatus(RCC_FLAG_PORRST) == SET) print("POR\n");
  if (RCC_GetFlagStatus(RCC_FLAG_PINRST) == SET) print("PIN\n");
  if (RCC_GetFlagStatus(RCC_FLAG_IWDGRST) == SET) print("IWDG\n");
  if (RCC_GetFlagStatus(RCC_FLAG_LPWRRST) == SET) print("LPWR\n");
  if (RCC_GetFlagStatus(RCC_FLAG_WWDGRST) == SET) print("WWDG\n");
  RCC_ClearFlag();


  gpio_enable(PIN_LED);

  cpu_claims = 0;

  task *heatbeat_task = TASK_create(heartbeat, TASK_STATIC);
  TASK_start_timer(heatbeat_task, &heartbeat_timer, 0, 0, 0, APP_HEARTBEAT_MS, "heartbeat");

  task *temp_task = TASK_create(read_temp, TASK_STATIC);
  TASK_start_timer(temp_task, &temp_timer, 0, 0, 0, APP_TEMPERATURE_MS, "temp");

  cli_tmo_task = TASK_create(cli_tmo, TASK_STATIC);
  if (app_detect_uart()) {
    app_cli_claim();
  } else {
    cli_tmo_last_time = 0;
  }

  cli_rd = FALSE;
  IO_set_callback(IOSTD, cli_rx_avail_irq, NULL);

  WS2812B_STM32F1_init(NULL);

  SENS_init();
  SENS_enter_active();

  app_spin();
}

void APP_shutdown(void) {
}

void APP_dump(void) {
  print("APP specifics\n-------------\n");
  print("\n");
}

void APP_claim(u8_t resource) {
  (void)resource;
  irq_disable();
  ASSERT(cpu_claims < 0xff);
  cpu_claims++;
  irq_enable();
}

void APP_release(u8_t resource) {
  (void)resource;
  irq_disable();
  ASSERT(cpu_claims > 0);
  cpu_claims--;
  irq_enable();
}

void APP_report_activity(bool activity, bool inactivity, bool tap, bool doubletap, bool issleep) {
  if (inactivity || issleep) {
    SENS_enter_idle();
  } else if (activity || tap || doubletap) {
    SENS_enter_active();
  }
}


static s32_t cli_temp(u32_t argc) {
  SENS_read_temp();
  return CLI_OK;
}

static s32_t cli_info(u32_t argc) {
  RCC_ClocksTypeDef clocks;
  print("DEV:%08x REV:%08x\n", DBGMCU_GetDEVID(), DBGMCU_GetREVID());
  RCC_GetClocksFreq(&clocks);
  print(
      "HCLK:  %i\n"
      "PCLK1: %i\n"
      "PCLK2: %i\n"
      "SYSCLK:%i\n"
      "ADCCLK:%i\n",
      clocks.HCLK_Frequency,
      clocks.PCLK1_Frequency,
      clocks.PCLK2_Frequency,
      clocks.SYSCLK_Frequency,
      clocks.ADCCLK_Frequency);
  print(
      "RTCCLK:%i\n"
      "RTCDIV:%i\n"
      "RTCTCK:%i ticks/sec\n",
      CONFIG_RTC_CLOCK_HZ,
      CONFIG_RTC_PRESCALER,
      CONFIG_RTC_CLOCK_HZ/CONFIG_RTC_PRESCALER
      );
  u64_t rtccnt = RTC_get_tick();
  u64_t rtcalm = RTC_get_alarm_tick();

  print(
      "RTCCNT:%08x%08x\n"
      "RTCALR:%08x%08x\n",
      (u32_t)(rtccnt>>32),(u32_t)rtccnt,
      (u32_t)(rtcalm>>32),(u32_t)rtcalm
      );
  return CLI_OK;
}


CLI_EXTERN_MENU(common)

CLI_MENU_START_MAIN
CLI_EXTRAMENU(common)
CLI_FUNC("temp", cli_temp, "Reads temperature")
CLI_FUNC("info", cli_info, "Prints system info")
CLI_FUNC("help", cli_help, "Prints help")
CLI_MENU_END
