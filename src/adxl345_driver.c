/*
 * adxl345_driver.c
 *
 *  Created on: Jan 10, 2016
 *      Author: petera
 */

#include "adxl345_driver.h"


static void adxl_cb(i2c_dev *idev, int res) {
  adxl345_dev *dev = (adxl345_dev *)I2C_DEV_get_user_data(idev);
  adxl_state old_state = dev->state;
  if (res != I2C_OK) {
    dev->state = ADXL345_STATE_IDLE;
    if (dev->callback) dev->callback(dev, old_state, res);
    return;
  }

  switch (dev->state) {
  case ADXL345_STATE_CONFIG:
    break;

  case ADXL345_STATE_READ:
    if (dev->arg.data) {
      dev->arg.data->x = (dev->tmp_buf[1] << 8) | dev->tmp_buf[2];
      dev->arg.data->y = (dev->tmp_buf[3] << 8) | dev->tmp_buf[4];
      dev->arg.data->z = (dev->tmp_buf[5] << 8) | dev->tmp_buf[6];
    }
    break;

  case ADXL345_STATE_ID:
    if (dev->arg.id_ok) {
      *dev->arg.id_ok =
          dev->tmp_buf[1] == ADXL345_ID;
    }
    break;

  case ADXL345_STATE_IDLE:
    res = I2C_ERR_ADXL345_STATE;
    break;
  }

  dev->state = ADXL345_STATE_IDLE;

  if (dev->callback) dev->callback(dev, old_state, res);
}

void adxl_open(adxl345_dev *dev, i2c_bus *bus, u32_t clock,
    void (*adxl_callback)(adxl345_dev *dev, adxl_state state, int res)) {
  memset(dev, 0, sizeof(adxl345_dev));
  I2C_DEV_init(&dev->i2c_dev, clock, bus, ADXL345_I2C_ADDR);
  I2C_DEV_set_user_data(&dev->i2c_dev, dev);
  I2C_DEV_set_callback(&dev->i2c_dev, adxl_cb);
  I2C_DEV_open(&dev->i2c_dev);
  dev->callback = adxl_callback;
}

void adxl_close(adxl345_dev *dev) {
  I2C_DEV_close(&dev->i2c_dev);
}


int adxl_check_id(adxl345_dev *dev, bool *id_ok) {
  if (id_ok == NULL) {
    return I2C_ERR_ADXL345_NULLPTR;
  }

  dev->arg.id_ok = id_ok;
  dev->tmp_buf[0] = ADXL345_R_DEVID;
  I2C_SEQ_TX_C(dev->seq[0], &dev->tmp_buf[0], 1);
  I2C_SEQ_RX_STOP_C(dev->seq[1], &dev->tmp_buf[1], 1);
  dev->state = ADXL345_STATE_ID;
  int res = I2C_DEV_sequence(&dev->i2c_dev, &dev->seq[0], 2);
  if (res != I2C_OK) {
    dev->state = ADXL345_STATE_IDLE;
  }

  return res;
}

