#ifndef SENSOR_MVP_H
#define SENSOR_MVP_H

#include "stm32u5xx_hal.h"

typedef void (*SensorMvp_LogFn)(const char *text);

typedef struct
{
  float temperature_c;
  float humidity_pct;
  int gas;
  int presence;
  uint8_t env_valid;
  uint8_t gas_valid;
} SensorMvp_Status_t;

void SensorMvp_Init(SensorMvp_LogFn log_fn);
void SensorMvp_Update(void);
HAL_StatusTypeDef SensorMvp_GetStatus(SensorMvp_Status_t *status);

#endif
