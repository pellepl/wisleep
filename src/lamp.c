/*
 * lamp.c
 *
 *  Created on: Feb 10, 2016
 *      Author: petera
 */

#include "system.h"
#include "lamp.h"
#include "app.h"
#include "taskq.h"
#include "ws2812b_spi_stm32f1.h"
#include "miniutils.h"

static const u8_t gamma[] = {
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  1,  1,  1,  1,
    1,  1,  1,  1,  1,  1,  1,  1,  1,  2,  2,  2,  2,  2,  2,  2,
    2,  3,  3,  3,  3,  3,  3,  3,  4,  4,  4,  4,  4,  5,  5,  5,
    5,  6,  6,  6,  6,  7,  7,  7,  7,  8,  8,  8,  9,  9,  9, 10,
   10, 10, 11, 11, 11, 12, 12, 13, 13, 13, 14, 14, 15, 15, 16, 16,
   17, 17, 18, 18, 19, 19, 20, 20, 21, 21, 22, 22, 23, 24, 24, 25,
   25, 26, 27, 27, 28, 29, 29, 30, 31, 32, 32, 33, 34, 35, 35, 36,
   37, 38, 39, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 50,
   51, 52, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 66, 67, 68,
   69, 70, 72, 73, 74, 75, 77, 78, 79, 81, 82, 83, 85, 86, 87, 89,
   90, 92, 93, 95, 96, 98, 99,101,102,104,105,107,109,110,112,114,
  115,117,119,120,122,124,126,127,129,131,133,135,137,138,140,142,
  144,146,148,150,152,154,156,158,160,162,164,167,169,171,173,175,
  177,180,182,184,186,189,191,193,196,198,200,203,205,208,210,213,
  215,218,220,223,225,228,231,233,236,239,241,244,247,249,252,255 };

static const u32_t colors[] = {
    0xffffff,
    0xff00ff,
    0xffff00,
    0x00ffff,
    0xffd0c0,
    0xc0ffd0,
    0xa0d0ff,
    0xff8020,
    0xff2080,
    0x20ff80,
    0x80ff20,
    0x2080ff,
    0x8020ff,
    0xff0000,
    0x00ff00,
    0x0000ff
};

static task_timer lamp_timer;
static task *lamp_task;
static bool lamp_enabled = FALSE;
static bool lamp_disabling = FALSE;
static u16_t light = 0xff;
static u16_t cycle = 0x0000;
static u32_t src_color = 0;
static u32_t dst_color = 0;
static u32_t lst_color = 0;
static u32_t cur_color = 0;
static u8_t factor = 0;
static volatile bool lamp_bus_bsy = FALSE;

static u8_t lerp(u8_t a, u8_t b, u8_t f) {
  u32_t aa = a, bb = b, ff = f;
  return ((aa<<8) + ff * (bb-aa)) >> 8;
}

static u32_t lerp_col(u32_t src, u32_t dst, u8_t f) {
  u8_t r = lerp((src >> 16) & 0xff, (dst >> 16) & 0xff, f);
  u8_t g = lerp((src >> 8) & 0xff, (dst >> 8) & 0xff, f);
  u8_t b = lerp((src) & 0xff, (dst) & 0xff, f);
  return (r<<16) | (g<<8) | b;
}

static u32_t gamma_col(u32_t col) {
  u8_t r = gamma[(col >> 16) & 0xff];
  u8_t g = gamma[(col >> 8) & 0xff];
  u8_t b = gamma[(col) & 0xff];
  return (r<<16) | (g<<8) | b;
}

static void lamp_cb_irq(bool error) {
  lamp_bus_bsy = FALSE;
  APP_release(CLAIM_SWP);
}

#define DELTA 16
static void lamp_regulate(void) {
  if (factor < 0x100 - DELTA) {
    factor += DELTA;
    cur_color = lerp_col(src_color, dst_color, factor);
  } else {
    factor = 0;
    cur_color = dst_color;
    src_color = dst_color;
    TASK_stop_timer(&lamp_timer);
    if (lamp_disabling) {
      lamp_disabling = FALSE;
      APP_release(CLAIM_LMP);
      print("lamp off\n");
    }
  }
  if (!lamp_bus_bsy) {
    lamp_bus_bsy = TRUE;
    int i;
    u32_t col = gamma_col(lerp_col(0, cur_color, light));
    print("lamp col %06x (%06x->%06x->%06x/%02x : %02x)\n", col, src_color, cur_color, dst_color, factor, light);
    for (i = 0; i < WS2812B_NBR_OF_LEDS; i++) {
      WS2812B_STM32F1_set(col);
    }
    APP_claim(CLAIM_SWP);
    WS2812B_STM32F1_output();
  }
}

static void lamp_update(void) {
  TASK_stop_timer(&lamp_timer);
  TASK_start_timer(lamp_task, &lamp_timer, 0, NULL, 40, 40, "lamp");
}

static void lamp(u32_t a, void *p) {
  lamp_regulate();
}

void LAMP_init(void) {
  WS2812B_STM32F1_init(lamp_cb_irq);
  lamp_task = TASK_create(lamp, TASK_STATIC);
  lst_color = 0x807040;
  src_color = 0;
  dst_color = 0;
  cur_color = 0;
  light = 0xf0;
  factor = 0;
  ASSERT(lamp_task);
}

void LAMP_enable(bool ena) {
  if (!ena) {
    if (lamp_enabled) {
      src_color = cur_color;
      lst_color = dst_color;
      dst_color = 0x000000;
      factor = 0;
      lamp_enabled = FALSE;
      lamp_disabling = TRUE;
      lamp_update();
      print("lamp out\n");
    }
  } else {
    if (!lamp_enabled) {
      lamp_disabling = FALSE;
      lamp_enabled = TRUE;
      APP_claim(CLAIM_LMP);
      dst_color = lst_color;
      lamp_update();
      print("lamp on\n");
    }
  }
}

void LAMP_cycle_delta(s16_t dcycle) {
  const u8_t colcount = sizeof(colors)/sizeof(colors[0]);
  cycle += dcycle;
  cycle %= (colcount<<8) | 0xff;
  src_color = cur_color;
  dst_color = lerp_col(colors[cycle>>8], colors[((cycle>>8)+1)%colcount], cycle & 0xff);
  factor = 0xff-DELTA;
  lamp_update();
}

void LAMP_light_delta(s8_t dlight) {
  light += dlight;
  light = MAX(light, 0x20);
  light = MIN(light, 0xf0);
  factor = 0xff-DELTA;
  lamp_update();
}


