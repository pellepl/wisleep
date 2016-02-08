/*
 * sensor.c
 *
 *  Created on: Feb 8, 2016
 *      Author: petera
 */

#include "system.h"
#include "sensor.h"
#include "app.h"
#include "adxl345_driver.h"
#include "hmc5883l_driver.h"
#include "itg3200_driver.h"

#define I2C_BUS               (_I2C_BUS(0))
#define I2C_CLK               (400000)

static adxl345_dev acc_dev;
static hmc5883l_dev mag_dev;
static itg3200_dev gyr_dev;
static volatile bool acc_bsy = FALSE;
static volatile bool mag_bsy = FALSE;
static volatile bool gyr_bsy = FALSE;
static bool check_id;
static itg_cfg gyr_cfg;

static void acc_cb_irq(adxl345_dev *dev, adxl_state s, int res) {
  acc_bsy = FALSE;

}

static void mag_cb_irq(hmc5883l_dev *dev, hmc_state s, int res) {
  mag_bsy = FALSE;

}

static void gyr_cb_irq(itg3200_dev *dev, itg_state s, int res) {
  gyr_bsy = FALSE;

}

void SENS_init(void) {
  int res;
  adxl_open(&acc_dev, I2C_BUS, I2C_CLK, acc_cb_irq);
  hmc_open(&mag_dev, I2C_BUS, I2C_CLK, mag_cb_irq);
  itg_open(&gyr_dev, I2C_BUS, FALSE, I2C_CLK, gyr_cb_irq);
  int ok_ids = 0, i;
  for (i = 0; i < 3; i++) {
    acc_bsy = TRUE;
    res = adxl_check_id(&acc_dev, &check_id);
    ASSERT(res == I2C_OK);
    while (acc_bsy) __WFI();
    if (check_id) ok_ids++;

    mag_bsy = TRUE;
    res = hmc_check_id(&mag_dev, &check_id);
    ASSERT(res == I2C_OK);
    while (mag_bsy) __WFI();
    if (check_id) ok_ids++;

    gyr_bsy = TRUE;
    res = itg_check_id(&gyr_dev, &check_id);
    ASSERT(res == I2C_OK);
    while (gyr_bsy) __WFI();
    if (check_id) ok_ids++;
  }
  ASSERT(ok_ids >= 7);
  gyr_cfg.samplerate_div = 0;
  gyr_cfg.pwr_clk = ITG3200_CLK_INTERNAL;
  gyr_cfg.pwr_reset = ITG3200_NO_RESET;
  gyr_cfg.pwr_sleep = ITG3200_ACTIVE;
  gyr_cfg.pwr_stdby_x = ITG3200_STNDBY_X_OFF;
  gyr_cfg.pwr_stdby_y = ITG3200_STNDBY_Y_OFF;
  gyr_cfg.pwr_stdby_z = ITG3200_STNDBY_Z_OFF;
  gyr_cfg.lp_filter_rate = ITG3200_LP_5;
  gyr_cfg.int_act = ITG3200_INT_ACTIVE_HI;
  gyr_cfg.int_clr = ITG3200_INT_CLR_ANY;
  gyr_cfg.int_data = ITG3200_INT_NO_DATA;
  gyr_cfg.int_pll = ITG3200_INT_NO_PLL;
  gyr_cfg.int_latch_pulse = ITG3200_INT_50US_PULSE;
  gyr_cfg.int_odpp = ITG3200_INT_OPENDRAIN;

}

void SENS_enter_lowpower(void) {
  hmc_config(&mag_dev,
      hmc5883l_mode_idle,
      hmc5883l_i2c_speed_normal,
      hmc5883l_gain_1_3,
      hmc5883l_measurement_mode_normal,
      hmc5883l_data_output_15,
      hmc5883l_samples_avg_1
      );
  ////
  gyr_cfg.pwr_sleep = ITG3200_LOW_POWER;
  itg_config(&gyr_dev, ITG3200_INT_ACTIVE_HI);
}
