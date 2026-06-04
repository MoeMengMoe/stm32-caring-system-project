#ifndef STATUS_DISPLAY_H
#define STATUS_DISPLAY_H

#include "sensor_mvp.h"
#include "stm32u5xx_hal.h"

typedef void (*StatusDisplay_LogFn)(const char *text);

HAL_StatusTypeDef StatusDisplay_Init(SPI_HandleTypeDef *hspi, StatusDisplay_LogFn log_fn);
void StatusDisplay_SetStatus(const SensorMvp_Status_t *status, int risk);
void StatusDisplay_Process(void);

#endif
