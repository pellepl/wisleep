/*
 * esp_flash_tunnel.h
 *
 *  Created on: Jun 27, 2016
 *      Author: petera
 */

#ifndef _ESP_FLASH_TUNNEL_H_
#define _ESP_FLASH_TUNNEL_H_

#include "system.h"

void esp_powerup(bool boot_flash);
void esp_flash_start(u32_t ignore, void *ignore_p);
void esp_flash_done(void);

#endif /* _ESP_FLASH_TUNNEL_H_ */
