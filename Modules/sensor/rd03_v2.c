#include "rd03_v2.h"

#include <string.h>

#define RD03_V2_RX_BUDGET             512U
#define RD03_V2_DMA_RX_BUFFER_SIZE    256U
#define RD03_V2_SOFT_RX_RING_SIZE     1024U
#define RD03_V2_REPORT_PAYLOAD_LEN    (3U + (RD03_V2_GATE_COUNT * 4U))
#define RD03_V2_REPORT_PAYLOAD_MAX    160U
#define RD03_V2_COMMAND_TIMEOUT_MS     100U
#define RD03_V2_COMMAND_PAYLOAD_MAX    64U
#define RD03_V2_DRAIN_TIME_MS          20U
#define RD03_V2_STATUS_TIMEOUT_MS      2000U
#define RD03_V2_RX_BYTE_TIMEOUT_MS     1U

typedef enum
{
  RD03_PARSE_HEADER = 0,
  RD03_PARSE_LENGTH_LOW,
  RD03_PARSE_LENGTH_HIGH,
  RD03_PARSE_PAYLOAD,
  RD03_PARSE_FOOTER
} Rd03V2_ParseState_t;

typedef enum
{
  RD03_ACK_PARSE_HEADER = 0,
  RD03_ACK_PARSE_LENGTH_LOW,
  RD03_ACK_PARSE_LENGTH_HIGH,
  RD03_ACK_PARSE_PAYLOAD,
  RD03_ACK_PARSE_FOOTER
} Rd03V2_AckParseState_t;

static const uint8_t s_report_header[] = {0xF4U, 0xF3U, 0xF2U, 0xF1U};
static const uint8_t s_report_footer[] = {0xF8U, 0xF7U, 0xF6U, 0xF5U};
static const uint8_t s_command_header[] = {0xFDU, 0xFCU, 0xFBU, 0xFAU};
static const uint8_t s_command_footer[] = {0x04U, 0x03U, 0x02U, 0x01U};

static const uint8_t s_open_command_mode[] =
{
  0xFDU, 0xFCU, 0xFBU, 0xFAU, 0x04U, 0x00U, 0xFFU, 0x00U,
  0x01U, 0x00U, 0x04U, 0x03U, 0x02U, 0x01U
};

static const uint8_t s_set_report_mode[] =
{
  0xFDU, 0xFCU, 0xFBU, 0xFAU, 0x08U, 0x00U, 0x12U, 0x00U,
  0x00U, 0x00U, 0x04U, 0x00U, 0x00U, 0x00U, 0x04U, 0x03U,
  0x02U, 0x01U
};

static const uint8_t s_close_command_mode[] =
{
  0xFDU, 0xFCU, 0xFBU, 0xFAU, 0x02U, 0x00U, 0xFEU, 0x00U,
  0x04U, 0x03U, 0x02U, 0x01U
};

static UART_HandleTypeDef *s_uart;
static Rd03V2_Status_t s_status;
static Rd03V2_ParseState_t s_parse_state;
static uint8_t s_header_index;
static uint8_t s_footer_index;
static uint16_t s_payload_len;
static uint16_t s_payload_index;
static uint8_t s_payload[RD03_V2_REPORT_PAYLOAD_MAX];
static uint8_t s_dma_rx_buffer[RD03_V2_DMA_RX_BUFFER_SIZE];
static uint8_t s_rx_ring[RD03_V2_SOFT_RX_RING_SIZE];
static volatile uint16_t s_rx_head;
static volatile uint16_t s_rx_tail;
static uint16_t s_dma_last_pos;
static volatile uint8_t s_dma_rx_active;

static uint16_t Read_U16_Le(const uint8_t *data)
{
  return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8U));
}

static uint32_t Read_U32_Le(const uint8_t *data)
{
  return (uint32_t)data[0] |
         ((uint32_t)data[1] << 8U) |
         ((uint32_t)data[2] << 16U) |
         ((uint32_t)data[3] << 24U);
}

static void Reset_Parser(void)
{
  s_parse_state = RD03_PARSE_HEADER;
  s_header_index = 0U;
  s_footer_index = 0U;
  s_payload_len = 0U;
  s_payload_index = 0U;
}

static uint16_t Advance_Ring_Index(uint16_t index)
{
  index++;
  if (index >= RD03_V2_SOFT_RX_RING_SIZE)
  {
    index = 0U;
  }

  return index;
}

static void Queue_Rx_Byte(uint8_t byte)
{
  uint16_t next = Advance_Ring_Index(s_rx_head);

  if (next == s_rx_tail)
  {
    s_rx_tail = Advance_Ring_Index(s_rx_tail);
    s_status.rx_overflow_count++;
  }

  s_rx_ring[s_rx_head] = byte;
  s_rx_head = next;
}

static uint8_t Pop_Rx_Byte(uint8_t *byte)
{
  if ((byte == NULL) || (s_rx_tail == s_rx_head))
  {
    return 0U;
  }

  *byte = s_rx_ring[s_rx_tail];
  s_rx_tail = Advance_Ring_Index(s_rx_tail);
  return 1U;
}

static void Queue_Dma_Range(uint16_t start, uint16_t end)
{
  if (end > RD03_V2_DMA_RX_BUFFER_SIZE)
  {
    end = RD03_V2_DMA_RX_BUFFER_SIZE;
  }

  for (uint16_t i = start; i < end; i++)
  {
    Queue_Rx_Byte(s_dma_rx_buffer[i]);
  }
}

static void Queue_Dma_New_Data(uint16_t pos)
{
  if (pos > RD03_V2_DMA_RX_BUFFER_SIZE)
  {
    pos = RD03_V2_DMA_RX_BUFFER_SIZE;
  }

  if (pos == s_dma_last_pos)
  {
    return;
  }

  if (pos > s_dma_last_pos)
  {
    Queue_Dma_Range(s_dma_last_pos, pos);
  }
  else
  {
    Queue_Dma_Range(s_dma_last_pos, RD03_V2_DMA_RX_BUFFER_SIZE);
    Queue_Dma_Range(0U, pos);
  }

  s_dma_last_pos = pos;
}

static HAL_StatusTypeDef Start_Dma_Rx(void)
{
  HAL_StatusTypeDef result;

  if (s_uart == NULL)
  {
    return HAL_ERROR;
  }

  s_dma_last_pos = 0U;
  result = HAL_UARTEx_ReceiveToIdle_DMA(s_uart, s_dma_rx_buffer, (uint16_t)sizeof(s_dma_rx_buffer));
  if (result == HAL_OK)
  {
    s_dma_rx_active = 1U;
    s_status.dma_restart_count++;
  }
  else
  {
    s_dma_rx_active = 0U;
    s_status.uart_error_count++;
    s_status.last_uart_error = s_uart->ErrorCode;
  }

  return result;
}

static void Parse_Report(void)
{
  if (s_payload_len < 3U)
  {
    return;
  }

  s_status.presence = (s_payload[0] != 0U) ? 1U : 0U;
  s_status.distance_cm = Read_U16_Le(&s_payload[1]);

  if (s_payload_len >= RD03_V2_REPORT_PAYLOAD_LEN)
  {
    for (uint32_t gate = 0U; gate < RD03_V2_GATE_COUNT; gate++)
    {
      s_status.gate_energy[gate] = Read_U32_Le(&s_payload[3U + (gate * 4U)]);
    }
  }

  s_status.valid = 1U;
  s_status.frame_count++;
  s_status.last_update_tick = HAL_GetTick();
}

static void Parse_Byte(uint8_t byte)
{
  switch (s_parse_state)
  {
    case RD03_PARSE_HEADER:
      if (byte == s_report_header[s_header_index])
      {
        s_header_index++;
        if (s_header_index == sizeof(s_report_header))
        {
          s_status.header_sync_count++;
          s_parse_state = RD03_PARSE_LENGTH_LOW;
          s_header_index = 0U;
        }
      }
      else
      {
        s_header_index = (byte == s_report_header[0]) ? 1U : 0U;
      }
      break;

    case RD03_PARSE_LENGTH_LOW:
      s_payload_len = byte;
      s_parse_state = RD03_PARSE_LENGTH_HIGH;
      break;

    case RD03_PARSE_LENGTH_HIGH:
      s_payload_len |= (uint16_t)((uint16_t)byte << 8U);
      if ((s_payload_len == 0U) || (s_payload_len > sizeof(s_payload)))
      {
        s_status.invalid_length_count++;
        Reset_Parser();
      }
      else
      {
        s_payload_index = 0U;
        s_parse_state = RD03_PARSE_PAYLOAD;
      }
      break;

    case RD03_PARSE_PAYLOAD:
      s_payload[s_payload_index++] = byte;
      if (s_payload_index >= s_payload_len)
      {
        s_footer_index = 0U;
        s_parse_state = RD03_PARSE_FOOTER;
      }
      break;

    case RD03_PARSE_FOOTER:
      if (byte != s_report_footer[s_footer_index])
      {
        s_status.invalid_footer_count++;
        Reset_Parser();
        break;
      }

      s_footer_index++;
      if (s_footer_index == sizeof(s_report_footer))
      {
        Parse_Report();
        Reset_Parser();
      }
      break;

    default:
      Reset_Parser();
      break;
  }
}

static void Drain_Uart(void)
{
  uint8_t byte;
  uint32_t start_tick;

  if (s_uart == NULL)
  {
    return;
  }

  start_tick = HAL_GetTick();
  while ((HAL_GetTick() - start_tick) < RD03_V2_DRAIN_TIME_MS)
  {
    HAL_StatusTypeDef result = HAL_UART_Receive(s_uart, &byte, 1U, RD03_V2_RX_BYTE_TIMEOUT_MS);

    if (result == HAL_OK)
    {
      s_status.init_rx_byte_count++;
    }
    else if (result != HAL_TIMEOUT)
    {
      s_status.uart_error_count++;
      s_status.last_uart_error = s_uart->ErrorCode;
    }
  }
}

static HAL_StatusTypeDef Send_Command(const uint8_t *command, uint16_t len)
{
  if ((s_uart == NULL) || (command == NULL) || (len == 0U))
  {
    return HAL_ERROR;
  }

  return HAL_UART_Transmit(s_uart, command, len, RD03_V2_COMMAND_TIMEOUT_MS);
}

static HAL_StatusTypeDef Receive_Command_Ack(uint16_t command_word, uint8_t *ack_ok)
{
  Rd03V2_AckParseState_t state = RD03_ACK_PARSE_HEADER;
  uint8_t header_index = 0U;
  uint8_t footer_index = 0U;
  uint16_t payload_len = 0U;
  uint16_t payload_index = 0U;
  uint8_t payload[RD03_V2_COMMAND_PAYLOAD_MAX];
  uint8_t byte;
  uint32_t start_tick;

  if ((s_uart == NULL) || (ack_ok == NULL))
  {
    return HAL_ERROR;
  }

  *ack_ok = 0U;
  start_tick = HAL_GetTick();

  while ((HAL_GetTick() - start_tick) < RD03_V2_COMMAND_TIMEOUT_MS)
  {
    HAL_StatusTypeDef result = HAL_UART_Receive(s_uart, &byte, 1U, RD03_V2_RX_BYTE_TIMEOUT_MS);

    if (result == HAL_TIMEOUT)
    {
      continue;
    }

    if (result != HAL_OK)
    {
      s_status.uart_error_count++;
      s_status.last_uart_error = s_uart->ErrorCode;
      return result;
    }

    s_status.init_rx_byte_count++;

    switch (state)
    {
      case RD03_ACK_PARSE_HEADER:
        if (byte == s_command_header[header_index])
        {
          header_index++;
          if (header_index == sizeof(s_command_header))
          {
            state = RD03_ACK_PARSE_LENGTH_LOW;
            header_index = 0U;
          }
        }
        else
        {
          header_index = (byte == s_command_header[0]) ? 1U : 0U;
        }
        break;

      case RD03_ACK_PARSE_LENGTH_LOW:
        payload_len = byte;
        state = RD03_ACK_PARSE_LENGTH_HIGH;
        break;

      case RD03_ACK_PARSE_LENGTH_HIGH:
        payload_len |= (uint16_t)((uint16_t)byte << 8U);
        if ((payload_len < 4U) || (payload_len > sizeof(payload)))
        {
          state = RD03_ACK_PARSE_HEADER;
          header_index = 0U;
        }
        else
        {
          payload_index = 0U;
          state = RD03_ACK_PARSE_PAYLOAD;
        }
        break;

      case RD03_ACK_PARSE_PAYLOAD:
        payload[payload_index++] = byte;
        if (payload_index >= payload_len)
        {
          footer_index = 0U;
          state = RD03_ACK_PARSE_FOOTER;
        }
        break;

      case RD03_ACK_PARSE_FOOTER:
        if (byte != s_command_footer[footer_index])
        {
          state = RD03_ACK_PARSE_HEADER;
          header_index = (byte == s_command_header[0]) ? 1U : 0U;
          break;
        }

        footer_index++;
        if (footer_index == sizeof(s_command_footer))
        {
          uint16_t ack_command = Read_U16_Le(payload);
          uint16_t ack_status = Read_U16_Le(&payload[2]);

          if (ack_command == (uint16_t)(command_word | 0x0100U))
          {
            *ack_ok = (ack_status == 0U) ? 1U : 0U;
            return (*ack_ok != 0U) ? HAL_OK : HAL_ERROR;
          }

          state = RD03_ACK_PARSE_HEADER;
          header_index = 0U;
        }
        break;

      default:
        state = RD03_ACK_PARSE_HEADER;
        header_index = 0U;
        break;
    }
  }

  return HAL_TIMEOUT;
}

static HAL_StatusTypeDef Enable_Report_Mode(void)
{
  HAL_StatusTypeDef result = HAL_OK;

  if (Send_Command(s_open_command_mode, sizeof(s_open_command_mode)) != HAL_OK)
  {
    return HAL_ERROR;
  }

  HAL_Delay(100U);
  Drain_Uart();

  if (Send_Command(s_open_command_mode, sizeof(s_open_command_mode)) != HAL_OK)
  {
    return HAL_ERROR;
  }

  if (Receive_Command_Ack(0x00FFU, &s_status.open_command_ack_ok) != HAL_OK)
  {
    result = HAL_ERROR;
  }

  if (Send_Command(s_set_report_mode, sizeof(s_set_report_mode)) != HAL_OK)
  {
    return HAL_ERROR;
  }

  if (Receive_Command_Ack(0x0012U, &s_status.report_mode_ack_ok) != HAL_OK)
  {
    result = HAL_ERROR;
  }

  if (Send_Command(s_close_command_mode, sizeof(s_close_command_mode)) != HAL_OK)
  {
    return HAL_ERROR;
  }

  if (Receive_Command_Ack(0x00FEU, &s_status.close_command_ack_ok) != HAL_OK)
  {
    result = HAL_ERROR;
  }

  return result;
}

HAL_StatusTypeDef Rd03V2_Init(UART_HandleTypeDef *uart)
{
  HAL_StatusTypeDef config_result;
  HAL_StatusTypeDef dma_result;

  if (uart == NULL)
  {
    return HAL_ERROR;
  }

  s_uart = uart;
  memset(&s_status, 0, sizeof(s_status));
  s_rx_head = 0U;
  s_rx_tail = 0U;
  s_dma_last_pos = 0U;
  s_dma_rx_active = 0U;
  Reset_Parser();

  config_result = Enable_Report_Mode();
  dma_result = Start_Dma_Rx();

  return (config_result == HAL_OK) ? dma_result : config_result;
}

void Rd03V2_Update(void)
{
  uint8_t byte;

  if (s_uart == NULL)
  {
    return;
  }

  for (uint32_t i = 0U; i < RD03_V2_RX_BUDGET; i++)
  {
    if (Pop_Rx_Byte(&byte) == 0U)
    {
      break;
    }

    s_status.rx_byte_count++;
    Parse_Byte(byte);
  }

  if (s_dma_rx_active == 0U)
  {
    (void)Start_Dma_Rx();
  }
}

HAL_StatusTypeDef Rd03V2_GetStatus(Rd03V2_Status_t *status)
{
  if (status == NULL)
  {
    return HAL_ERROR;
  }

  *status = s_status;
  if ((status->valid != 0U) &&
      ((HAL_GetTick() - status->last_update_tick) > RD03_V2_STATUS_TIMEOUT_MS))
  {
    status->valid = 0U;
    status->presence = 0U;
    status->distance_cm = 0U;
  }

  return HAL_OK;
}

void Rd03V2_OnUartRxEvent(UART_HandleTypeDef *uart, uint16_t size)
{
  HAL_UART_RxEventTypeTypeDef event_type;

  if ((s_uart == NULL) || (uart != s_uart))
  {
    return;
  }

  s_status.dma_event_count++;
  Queue_Dma_New_Data(size);

  event_type = HAL_UARTEx_GetRxEventType(uart);
  if ((event_type == HAL_UART_RXEVENT_IDLE) || (event_type == HAL_UART_RXEVENT_TC))
  {
    s_dma_rx_active = 0U;
    (void)Start_Dma_Rx();
  }
}

void Rd03V2_OnUartError(UART_HandleTypeDef *uart)
{
  if ((s_uart == NULL) || (uart != s_uart))
  {
    return;
  }

  s_status.uart_error_count++;
  s_status.last_uart_error = uart->ErrorCode;
  (void)HAL_UART_AbortReceive(uart);
  s_dma_rx_active = 0U;
  (void)Start_Dma_Rx();
}
