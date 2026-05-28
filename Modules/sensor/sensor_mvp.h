#ifndef SENSOR_MVP_H
#define SENSOR_MVP_H

#include "stm32u5xx_hal.h"

typedef void (*SensorMvp_LogFn)(const char *text);

void SensorMvp_Init(SensorMvp_LogFn log_fn);
void SensorMvp_Update(void);

#endif
