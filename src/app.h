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
#define PIN_ACC_INT           PORTA, PIN0

#define APP_PREVENT_SLEEP_IF_LESS_MS    20
#define APP_WDOG_TIMEOUT_S              23
#define APP_HEARTBEAT_MS                20000
#define APP_CLI_POLL_MS                 1000
#define APP_CLI_INACT_SHUTDOWN_S        30

#define CLAIM_CLI             0x00
#define CLAIM_SEN             0x01
#define CLAIM_ACC             0x02
#define CLAIM_MAG             0x03
#define CLAIM_GYR             0x04
#define CLAIM_LGT             0x05

// initializes application
void APP_init(void);
void APP_shutdown(void);
void APP_claim(u8_t resource);
void APP_release(u8_t resource);

#endif /* APP_H_ */
