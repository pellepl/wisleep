/*
 * protocol.h
 *
 *  Created on: Apr 13, 2016
 *      Author: petera
 */

#ifndef SRC_PROTOCOL_H_
#define SRC_PROTOCOL_H_

// packet ids to stm from efm
typedef enum {
  P_STM_HELLO = 0,
  P_STM_LAMP_ENA,           // ACK:[on/off]
  P_STM_LAMP_INTENSITY,     // ACK:[intensity]
  P_STM_LAMP_COLOR,         // ACK:[red][green][blue]
  P_STM_LAMP_STATUS,        // ACK:[on/off][intensity][red][green][blue]
  P_STM_CURRENT_TIME,       //

} proto_stm;

// packet ids to efm from stm
typedef enum {
  P_EFM_HELLO = 0,
  P_EFM_LAMP_ENA,           // [on/off]
  P_EFM_LAMP_INTENSITY,     // [intensity]
  P_EFM_LAMP_COLOR,         // [red][green][blue]
  P_EFM_LAMP_STATUS,        // [on/off][intensity][red][green][blue]
  P_EFM_REQUEST_TIME,       //


} proto_efm;

#endif /* SRC_PROTOCOL_H_ */
