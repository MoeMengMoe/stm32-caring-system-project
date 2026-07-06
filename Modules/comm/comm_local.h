#ifndef STM32_CARING_SYSTEM_PROJECT_COMM_LOCAL_H
#define STM32_CARING_SYSTEM_PROJECT_COMM_LOCAL_H

#include "comm_wifi.h"

/*
 * UART4 local command input for Simon's ESP32 microphone module.
 *
 * The ESP32 performs local voice recognition and sends semantic command lines
 * to STM32. It uses the same C/D frame grammar as the ESP8266 gateway:
 * - C,request_id,relay_id,ON|OFF + line ending
 * - D,request_id,command_type,scenario,value + line ending
 *
 * UART4 is RX interrupt driven and has no DMA requirement.
 */
CommWifi_Result CommLocal_Init(void);
CommWifi_Result CommLocal_PollCommand(CommWifi_Command_t *cmd);

void CommLocal_OnRxComplete(void);
void CommLocal_OnUartError(void);

#endif // STM32_CARING_SYSTEM_PROJECT_COMM_LOCAL_H
