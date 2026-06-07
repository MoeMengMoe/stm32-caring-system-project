#ifndef RD03_V2_H
#define RD03_V2_H

#include "stm32u5xx_hal.h"

#include <stdint.h>

#define RD03_V2_GATE_COUNT 32U

typedef struct
{
  uint8_t valid;
  uint8_t presence;
  uint16_t distance_cm;
  uint32_t gate_energy[RD03_V2_GATE_COUNT];
  uint32_t frame_count;
  uint32_t rx_byte_count;
  uint32_t init_rx_byte_count;
  uint32_t header_sync_count;
  uint32_t invalid_length_count;
  uint32_t invalid_footer_count;
  uint32_t rx_overflow_count;
  uint32_t dma_event_count;
  uint32_t dma_restart_count;
  uint32_t uart_error_count;
  uint32_t last_uart_error;
  uint8_t open_command_ack_ok;
  uint8_t report_mode_ack_ok;
  uint8_t close_command_ack_ok;
  uint32_t last_update_tick;
} Rd03V2_Status_t;

HAL_StatusTypeDef Rd03V2_Init(UART_HandleTypeDef *uart);
void Rd03V2_Update(void);
HAL_StatusTypeDef Rd03V2_GetStatus(Rd03V2_Status_t *status);
void Rd03V2_OnUartRxEvent(UART_HandleTypeDef *uart, uint16_t size);
void Rd03V2_OnUartError(UART_HandleTypeDef *uart);

#endif
