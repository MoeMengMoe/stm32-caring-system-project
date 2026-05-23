#ifndef BME280_H
#define BME280_H

#include "stm32u5xx_hal.h"
#include <stdint.h>

#define BME280_I2C_ADDR_PRIMARY (0x76U)
#define BME280_CHIP_ID_VALUE    (0x60U)

typedef struct
{
  int32_t temperature_centi_c;
  uint32_t humidity_centi_pct;
  uint32_t pressure_centi_hpa;
} Bme280_Data_t;

HAL_StatusTypeDef Bme280_Init(I2C_HandleTypeDef *hi2c);
HAL_StatusTypeDef Bme280_Read(Bme280_Data_t *data);
uint8_t Bme280_GetChipId(void);

#endif
