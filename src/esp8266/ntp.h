/*
 * ntp.h
 *
 *  Created on: Apr 16, 2016
 *      Author: petera
 */

#ifndef _ESP8266_NTP_H_
#define _ESP8266_NTP_H_

void ntp_set_host(const char *hostname);
void ntp_query(void);

#endif /* _ESP8266_NTP_H_ */
