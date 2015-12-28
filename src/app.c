/*
 * app.c
 *
 *  Created on: Jan 2, 2014
 *      Author: petera
 */

#include "app.h"
#include "taskq.h"
#include "miniutils.h"
#include "gpio.h"

typedef enum {
  UNARMED = 0,
  ARMING,
  ARMED
} armstate;

static task_timer alarm_timer;
static task_timer arm_timer;
static task_timer blink_timer;
static task *blink_task;
static u8_t blink_seq = 0;
static u8_t blink_cycle = 0;
static bool blink_enable = FALSE;
static volatile bool alarm_triggered = FALSE;
static volatile armstate state = UNARMED;
static u16_t arm_process_count;
static volatile bool arm_trig_pending = FALSE;

static void blink_tf(u32_t arg, void *argp) {
  if (!blink_enable) {
    TASK_stop_timer(&blink_timer);
    APP_set_light(FALSE);
    return;
  }
  bool new_cycle = FALSE;
  switch (blink_cycle) {
  case 0:
    APP_set_light(FALSE);
    new_cycle = blink_seq > 20;
    break;
  case 1:
    APP_set_light(TRUE);
    new_cycle = blink_seq > 20;
    break;
  case 2:
    if (blink_seq & 1) {
      APP_set_light(TRUE);
    } else {
      APP_set_light(FALSE);
    }
    new_cycle = blink_seq > 40;
    break;
  case 3:
    if ((rand_next() & 7) > 3) { //(blink_seq & 7) {
      APP_set_light(FALSE);
    } else {
      APP_set_light(TRUE);
    }
    new_cycle = blink_seq > 40;
    break;
  }
  if (new_cycle) {
    blink_seq = 0;
    blink_cycle = rand_next() & 3;
  }
  blink_seq++;
}

static void alarm_end_tf(u32_t arg, void *argp) {
  alarm_triggered = FALSE;
  blink_enable = FALSE;
  TASK_stop_timer(&blink_timer);
  APP_set_transducer(FALSE);
  APP_set_light(FALSE);
  gpio_disable(PIN_STAT_ALARM);
  gpio_enable(PIN_LED);
}


static void alarm_begin_tf(u32_t arg, void *argp) {
  APP_set_transducer(TRUE);
  APP_set_light(TRUE);
  task *alarm_end_task = TASK_create(alarm_end_tf, 0);
  TASK_start_timer(alarm_end_task, &alarm_timer, 0,0, 3000, 0, "alarm_tim");
  blink_enable = TRUE;
  blink_seq = 0;
  blink_cycle = rand_next() & 0x3;
  TASK_start_timer(blink_task, &blink_timer, 0,0, 0, 25, "blink_tim");
  gpio_enable(PIN_STAT_ALARM);
}

static void arm_process_tf() {
  arm_process_count++;
  if (arm_process_count < 20) {
    if (arm_process_count & 1) {
      gpio_enable(PIN_STAT_ARM);
    } else {
      gpio_disable(PIN_STAT_ARM);
    }
  } else {
    gpio_enable(PIN_STAT_ARM);
    state = ARMED;
    TASK_stop_timer(&arm_timer);
  }
}

static task *alarm_process_task;
static void arm_start_tf() {
  switch (state) {
  case UNARMED: {
    state = ARMING;
    arm_process_count = 0;
    TASK_start_timer(alarm_process_task, &arm_timer, 0,0, 0, 500, "arm_tim");
  }
  break;
  case ARMING:
  case ARMED: {
    TASK_stop_timer(&arm_timer);
    gpio_disable(PIN_STAT_ARM);
    state = UNARMED;
  }
  break;
  default:
    ASSERT(FALSE);
    break;
  }
  arm_trig_pending = FALSE;
}

void APP_trigger_alarm(void) {
  if (!alarm_triggered) {
    alarm_triggered = TRUE;
    task *t = TASK_create(alarm_begin_tf, 0);
    ASSERT(t);
    TASK_run(t, 0, NULL);
  }
}

void APP_trigger_arm(void) {
  if (!arm_trig_pending) {
    arm_trig_pending = TRUE;
    task *t = TASK_create(arm_start_tf, 0);
    ASSERT(t);
    TASK_run(t, 0, NULL);
  }
}

static void pir_interrupt_irq(gpio_pin pin) {
  gpio_disable(PIN_LED);

  if (state == ARMED) {
    APP_trigger_alarm();
  }
}

static void but_arm_irq(gpio_pin pin) {
  APP_trigger_arm();
}

static void but_trig_irq(gpio_pin pin) {
  APP_trigger_alarm();
}

void APP_init(void) {
  //SYS_dbg_level(D_WARN);
  //SYS_dbg_mask_enable(D_ANY); // todo remove
  gpio_enable(PIN_LED);

  gpio_config(PIN_POW_TRANS, CLK_2MHZ, OUT, AF0, PUSHPULL, NOPULL);
  gpio_config(PIN_POW_LIGHT, CLK_2MHZ, OUT, AF0, PUSHPULL, NOPULL);
  gpio_disable(PIN_POW_TRANS);
  gpio_disable(PIN_POW_LIGHT);
  gpio_config(PIN_STAT_ALARM, CLK_2MHZ, OUT, AF0, PUSHPULL, NOPULL);
  gpio_config(PIN_STAT_ARM, CLK_2MHZ, OUT, AF0, PUSHPULL, NOPULL);
  gpio_disable(PIN_STAT_ALARM);
  gpio_disable(PIN_STAT_ARM);

  // config pir interrupts
  gpio_config(PIN_PIR_SIG, CLK_50MHZ, IN, AF0, OPENDRAIN, PULLUP);
  gpio_interrupt_config(PIN_PIR_SIG, pir_interrupt_irq, FLANK_DOWN);
  gpio_interrupt_mask_enable(PIN_PIR_SIG, TRUE);

  alarm_process_task = TASK_create(arm_process_tf, TASK_STATIC);

  // config button interrupts
  gpio_config(PIN_BUTTON_ARM, CLK_50MHZ, IN, AF0, OPENDRAIN, PULLUP);
  gpio_interrupt_config(PIN_BUTTON_ARM, but_arm_irq, FLANK_DOWN);
  gpio_interrupt_mask_enable(PIN_BUTTON_ARM, TRUE);

  gpio_config(PIN_BUTTON_TRIG, CLK_50MHZ, IN, AF0, OPENDRAIN, PULLUP);
  gpio_interrupt_config(PIN_BUTTON_TRIG, but_trig_irq, FLANK_DOWN);
  gpio_interrupt_mask_enable(PIN_BUTTON_TRIG, TRUE);


  blink_task = TASK_create(blink_tf, TASK_STATIC);
}

void APP_set_transducer(bool ena) {
  if (ena) {
    TIM_Cmd(TIM1, ENABLE);
    gpio_enable(PIN_POW_TRANS);
  } else {
    gpio_disable(PIN_POW_TRANS);
    TIM_Cmd(TIM1, DISABLE);
  }
}

void APP_set_light(bool ena) {
  if (ena) {
    gpio_enable(PIN_POW_LIGHT);
  } else {
    gpio_disable(PIN_POW_LIGHT);
  }
}

void APP_shutdown(void) {
  APP_set_transducer(FALSE);
  APP_set_light(FALSE);
}
