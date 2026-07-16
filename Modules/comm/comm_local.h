#ifndef STM32_CARING_SYSTEM_PROJECT_COMM_LOCAL_H
#define STM32_CARING_SYSTEM_PROJECT_COMM_LOCAL_H

#include "comm_wifi.h"

#include <stdint.h>

typedef struct
{
    uint32_t rx_bytes;
    uint32_t rx_lines;
    uint32_t rx_queued;
    uint32_t rx_filtered;
    uint32_t rx_queue_full;
    uint32_t rx_drop_oldest;
    uint32_t rx_overflow;
    uint32_t rx_resynced;
    uint32_t rx_uart_errors;
    uint32_t rx_restart_fail;
    uint32_t rx_restart_ok;
    uint32_t rx_service_rearm;
    uint32_t last_uart_error;
    uint32_t parse_ok;
    uint32_t parse_fail;
    uint16_t current_line_len;
    uint8_t queued_lines;
    uint8_t rx_armed;
    uint8_t rearm_needed;
    uint8_t last_rx_byte;
    char last_line[64];
    char last_parsed_line[64];
    char current_line[64];
} CommLocal_Diagnostics_t;

/*
 * UART4 local command input for the ESP32 microphone module.
 *
 * The ESP32 performs local voice recognition and sends semantic command lines
 * to STM32. It accepts the same C/D frame grammar as the ESP8266 gateway:
 * - C,request_id,relay_id,ON|OFF + line ending
 * - D,request_id,command_type,scenario,value + line ending
 * It also accepts the ESP-SR prototype frame:
 * - RISK:0..3 + line ending
 *
 * UART4 is RX interrupt driven and has no DMA requirement.
 */
CommWifi_Result CommLocal_Init(void);
void CommLocal_Service(void);
CommWifi_Result CommLocal_PollCommand(CommWifi_Command_t *cmd);
void CommLocal_GetDiagnostics(CommLocal_Diagnostics_t *diag);

void CommLocal_OnRxComplete(void);
void CommLocal_OnUartError(void);

#endif // STM32_CARING_SYSTEM_PROJECT_COMM_LOCAL_H
