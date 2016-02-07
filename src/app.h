/*
 * app.h
 *
 *  Created on: Jan 2, 2014
 *      Author: petera
 */

#ifndef APP_H_
#define APP_H_

#include "system.h"

#define WIFIDO_VERSION        0x00010000

#define PIN_UART_TX           PORTA, PIN2
#define PIN_UART_RX           PORTA, PIN3
#define PIN_LED               PORTC, PIN13

#define PREVENT_SLEEP_IF_LESS_MS    20

#define CLAIM_CLI             0x00

// initializes application
void APP_init(void);

void APP_shutdown(void);

void APP_claim(u8_t resource);
void APP_release(u8_t resource);

#endif /* APP_H_ */
