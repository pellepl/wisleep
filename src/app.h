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
#define PIN_TRANS_POS         PORTA, PIN8
#define PIN_TRANS_NEG         PORTA, PIN9
#define PIN_POW_TRANS         PORTB, PIN5
#define PIN_BUTTON_TRIG       PORTA, PIN6
#define PIN_BUTTON_ARM        PORTA, PIN7
#define PIN_POW_LIGHT         PORTB, PIN8
#define PIN_PIR_SIG           PORTB, PIN9
#define PIN_STAT_ALARM        PORTB, PIN10
#define PIN_STAT_ARM          PORTB, PIN11
#define PIN_LED               PORTC, PIN13

// initializes application
void APP_init(void);
void APP_set_transducer(bool ena);
void APP_set_light(bool ena);
void APP_trigger_alarm(void);
void APP_trigger_arm(void);

void APP_shutdown(void);

#endif /* APP_H_ */
