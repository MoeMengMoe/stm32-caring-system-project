#ifndef BOARD_IO_H
#define BOARD_IO_H

#include "app_types.h"

#include <stdint.h>

typedef void (*BoardIo_LogFn)(const char *text);

typedef struct
{
  uint8_t sos_pressed;
  uint8_t ack_pressed;
} BoardIo_Events_t;

void BoardIo_Init(BoardIo_LogFn log_fn);
void BoardIo_Update(uint32_t now_ms, const AppStatus_t *status, BoardIo_Events_t *events);
void BoardIo_SetRelayMask(uint8_t relay_mask);

#endif
