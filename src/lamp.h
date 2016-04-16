/*
 * lamp.h
 *
 *  Created on: Feb 10, 2016
 *      Author: petera
 */

#ifndef _LAMP_H_
#define _LAMP_H_

#define LAMP_MIN_INTENSITY 0x20
#define LAMP_MAX_INTENSITY 0xf0

void LAMP_init(void);
void LAMP_enable(bool ena);
bool LAMP_on(void);
void LAMP_set_color(u32_t rgb);
void LAMP_set_intensity(u8_t i);
u32_t LAMP_get_color(void);
u8_t LAMP_get_intensity(void);
void LAMP_cycle_delta(s16_t dcycle);
void LAMP_light_delta(s8_t dlight);


#endif /* _LAMP_H_ */
