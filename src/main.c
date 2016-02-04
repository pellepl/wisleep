#include "stm32f10x.h"
#include "system.h"
#include "uart_driver.h"
#include "io.h"
#include "timer.h"
#include "miniutils.h"
#include "taskq.h"
#include "processor.h"
#include "linker_symaccess.h"
#include "app.h"
#include "gpio.h"
#include "shared_mem.h"
#ifdef CONFIG_SPI
#include "spi_driver.h"
#endif
#ifdef CONFIG_I2C
#include "i2c_driver.h"
#endif
#ifdef CONFIG_WDOG
#include "wdog.h"
#endif
#ifdef CONFIG_RTC
#include "rtc.h"
#endif

#include "cli.h"

static void app_assert_cb(void) {
  APP_shutdown();
}

// main entry from bootstrap
int main(void) {
  enter_critical();
  PROC_base_init();
  SYS_init();
  UART_init();
  UART_assure_tx(_UART(0), TRUE);
  PROC_periph_init();
  exit_critical();

  SYS_set_assert_callback(app_assert_cb);

  IO_define(IOSTD, io_uart, UARTSTDIN);

#ifdef CONFIG_SPI
  SPI_init();
#endif
#ifdef CONFIG_I2C
  I2C_init();
#endif
#ifdef CONFIG_WDOG
  WDOG_init();
#endif
#ifdef CONFIG_RTC
  RTC_init(NULL);
#endif

  print("\n\n\nHardware initialization done\n");

  print("Stack 0x%08x -- 0x%08x\n", STACK_START, STACK_END);

  print("Subsystem initialization done\n");

  TASK_init();

  cli_init();

  print ("\n");
  print(APP_NAME);
  print("\n\n");
  print("build     : %i\n", SYS_build_number());
  print("build date: %i\n", SYS_build_date());

  print("reset reason: ");
  if (SHMEM_validate()) {
    print("%i\n", SHMEM_get()->reboot_reason);
  } else {
    print("cold start\n");
    SYS_dbg_level(D_WARN);
    SYS_dbg_mask_set(0);
  }
  SHMEM_set_reboot_reason(REBOOT_UNKNOWN);

  rand_seed(0xd0decaed ^ SYS_get_tick());

  APP_init();

  while (1) {
    while (TASK_tick());
    time t;
    s32_t err = TASK_next_wakeup_ms(&t);
    if (!err) {
      print("wait until 0x%08x%08x\n", (u32_t)(t>>32), (u32_t)t);
      RTC_set_alarm_tick(RTC_MS_TO_TICK(t));
      SYS_hardsleep_ms(t - RTC_TICK_TO_MS(RTC_get_tick()));
      t = RTC_TICK_TO_MS(RTC_get_tick());
      print("  awaken @ 0x%08x%08x\n", (u32_t)(t>>32), (u32_t)t);
      TASK_timer();
    } else {
      // wait indefinitely
      TASK_wait();
    }
  }

  return 0;
}

// assert failed handler from stmlib? TODO

void assert_failed(uint8_t* file, uint32_t line) {
  SYS_assert((char*)file, (s32_t)line);
}
