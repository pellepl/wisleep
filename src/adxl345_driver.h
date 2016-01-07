/*
 * adxl345_driver.h
 *
 *  Created on: Jan 2, 2016
 *      Author: petera
 */

#ifndef ADXL345_DRIVER_H_
#define ADXL345_DRIVER_H_

#define ADXL345_R_DEVID           0x00
#define ADXL345_R_THRESH_TAP      0x1d
#define ADXL345_R_OFSX            0x1e
#define ADXL345_R_OFSY            0x1f
#define ADXL345_R_OFSZ            0x20
#define ADXL345_R_DUR             0x21
#define ADXL345_R_LATENT          0x22
#define ADXL345_R_WINDOW          0x23
#define ADXL345_R_THRESH_ACT      0x24
#define ADXL345_R_THRESH_INACT    0x25
#define ADXL345_R_TIME_INACT      0x26
#define ADXL345_R_ACT_INACT_CTL   0x27
#define ADXL345_R_THRESH_FF       0x28
#define ADXL345_R_TIME_FF         0x29
#define ADXL345_R_TAP_AXES        0x2a
#define ADXL345_R_ACT_TAP_STATUS  0x2b
#define ADXL345_R_BW_RATE         0x2c
#define ADXL345_R_POWER_CTL       0x2d
#define ADXL345_R_INT_ENABLE      0x2e
#define ADXL345_R_INT_MAP         0x2f
#define ADXL345_R_INT_SOURCE      0x30
#define ADXL345_R_DATA_FORMAT     0x31
#define ADXL345_R_DATAX0          0x32
#define ADXL345_R_DATAX1          0x33
#define ADXL345_R_DATAY0          0x34
#define ADXL345_R_DATAY1          0x35
#define ADXL345_R_DATAZ0          0x36
#define ADXL345_R_DATAZ1          0x37
#define ADXL345_R_FIFO_CTL        0x38
#define ADXL345_R_FIFO_STATUS     0x39

#define ADXL345_ID                0xe5

#define ADXL345_I2C_ADDR          0xa6



typedef struct {
  s16_t x,y,z;
} adxl_reading;

typedef enum {
  ADXL345_STATE_IDLE = 0,
  ADXL345_STATE_CONFIG,
  ADXL345_STATE_READ,
  ADXL345_STATE_ID,
} adxl_state;

typedef enum {
  ADXL345_NONE = 0b000,
  ADXL345_X    = 0b001,
  ADXL345_Y    = 0b010,
  ADXL345_XY   = 0b011,
  ADXL345_Z    = 0b100,
  ADXL345_XZ   = 0b101,
  ADXL345_YZ   = 0b110,
  ADXL345_XYZ  = 0b111,
} adxl_axes;

typedef struct adxl345_dev_s {
  i2c_dev i2c_dev;
  adxl_state state;
  void (* callback)(struct adxl345_dev_s *dev, adxl_state state, int res);
  i2c_dev_sequence seq[3];
  union {
    bool *id_ok;
    adxl_reading *data;
  } arg;
  u8_t tmp_buf[8];
} adxl345_dev;

void adxl_open(adxl345_dev *dev, i2c_bus *bus, u32_t clock, void (*adxl_callback)(adxl345_dev *dev, adxl_state state, int res));
void adxl_close(adxl345_dev *dev);
/**
Configure offsets.
@param x, y, z  The OFSX, OFSY, and OFSZ registers are each eight bits and
                offer user-set offset adjustments in twos complement format
                with a scale factor of 15.6 mg/LSB (that is, 0x7F = 2 g). The
                value stored in the offset registers is automatically added to the
                acceleration data, and the resulting value is stored in the output
                data registers.
 */
int adxl_set_offset(adxl345_dev *dev, s8_t x, s8_t y, s8_t z);

/**
Configure single/double tap detection.
@param tap_ena  Combination of axes enabled for tap detection.
@param thresh   The THRESH_TAP register is eight bits and holds the threshold
                value for tap interrupts. The data format is unsigned, therefore,
                the magnitude of the tap event is compared with the value
                in THRESH_TAP for normal tap detection. The scale factor is
                62.5 mg/LSB (that is, 0xFF = 16 g). A value of 0 may result in
                undesirable behavior if single tap/double tap interrupts are
                enabled.
@param dur      The DUR register is eight bits and contains an unsigned time
                value representing the maximum time that an event must be
                above the THRESH_TAP threshold to qualify as a tap event. The
                scale factor is 625 µs/LSB. A value of 0 disables the single tap/
                double tap functions,
@param latent   The latent register is eight bits and contains an unsigned time
                value representing the wait time from the detection of a tap
                event to the start of the time window (defined by the window
                register) during which a possible second tap event can be detected.
                The scale factor is 1.25 ms/LSB. A value of 0 disables the double tap
                function.
@param window   The window register is eight bits and contains an unsigned time
                value representing the amount of time after the expiration of the
                latency time (determined by the latent register) during which a
                second valid tap can begin. The scale factor is 1.25 ms/LSB. A
                value of 0 disables the double tap function.
@param suppress Setting the suppress bit suppresses double tap detection if
                acceleration greater than the value in THRESH_TAP is present
                between taps.
 */
int adxl_config_tap(adxl345_dev *dev,
    adxl_axes tap_ena, u8_t thresh, u8_t dur, u8_t latent, u8_t window, bool suppress);

/**
Configure activity/inactivity detection.
@param ac_dc    In dc-coupled operation, the current acceleration magnitude is
                compared directly with THRESH_ACT and THRESH_INACT to determine
                whether activity or inactivity is detected. In ac-coupled operation
                for activity detection, the acceleration value at the start of
                activity detection is taken as a reference value. New samples of
                acceleration are then compared to this reference value, and if the
                magnitude of the difference exceeds the THRESH_ACT value, the device
                triggers an activity interrupt.
                Similarly, in ac-coupled operation for inactivity detection, a
                reference value is used for comparison and is updated whenever
                the device exceeds the inactivity threshold. After the reference
                value is selected, the device compares the magnitude of the
                difference between the reference value and the current acceleration
                with THRESH_INACT. If the difference is less than the value in
                THRESH_INACT for the time in TIME_INACT, the device is
                considered inactive and the inactivity interrupt is triggered.
@param act_ena  Combination of axes enabled for activity triggering.
@param inact_ena  Combination of axes enabled for activity triggering.
@param thr_act  The THRESH_ACT register is eight bits and holds the threshold value for
                detecting activity. The data format is unsigned, so the magnitude of the
                activity event is compared with the value in the THRESH_ACT register.
                The scale factor is 62.5 mg/LSB. A value of 0 may result in undesirable
                behavior if the activity interrupt is enabled.
@param thr_inact  The THRESH_INACT register is eight bits and holds the threshold value
                for detecting inactivity. The data format is unsigned, so the magnitude of
                the inactivity event is compared with the value in the THRESH_INACT register.
                The scale factor is 62.5 mg/LSB. A value of 0 may result in undesirable
                behavior if the inactivity interrupt is enabled.
@param time_inact  The TIME_INACT register is eight bits and contains an unsigned time value
                representing the amount of time that acceleration must be less than the value
                in the THRESH_INACT register for inactivity to be declared. The scale factor
                is 1 sec/LSB. Unlike the other interrupt functions, which use unfiltered data
                (see the Threshold section), the inactivity function uses filtered output data.
                At least one output sample must be generated for the inactivity interrupt to
                be triggered. This results in the function appearing unresponsive if the
                TIME_INACT register is set to a value less than the time constant of the output
                data rate. A value of 0 results in an interrupt when the output data is less
                than the value in the THRESH_INACT register.
 */
int adxl_config_activity(adxl345_dev *dev,
    bool ac_dc, adxl_axes act_ena, adxl_axes inact_ena, u8_t thr_act, u8_t thr_inact, u8_t time_inact);


int adxl_config_freefall(adxl345_dev *dev);

#endif /* ADXL345_DRIVER_H_ */
