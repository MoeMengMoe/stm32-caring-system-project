#ifndef SENSOR_MVP_H
#define SENSOR_MVP_H

#include "stm32u5xx_hal.h"

typedef void (*SensorMvp_LogFn)(const char *text);

typedef struct
{
  float temperature_c;
  float humidity_pct;
  int gas;
  uint16_t gas_baseline_mv;
  uint16_t gas_delta_mv;
  uint16_t gas_ppm_est;
  int presence;
  uint8_t pir_presence;
  uint8_t rd03_ot2_presence;
  uint8_t radar_valid;
  uint8_t radar_presence;
  uint16_t radar_distance_cm;
  uint8_t radar_zone;
  uint8_t radar_peak_gate;
  uint16_t radar_peak_gate_cm;
  uint32_t radar_peak_energy;
  uint32_t radar_energy_sum;
  uint32_t radar_motion_score;
  uint8_t radar_active_gate_count;
  uint32_t radar_occupied_seconds;
  uint32_t radar_still_seconds;
  uint32_t radar_last_seen_age_ms;
  uint8_t env_valid;
  uint8_t gas_valid;
} SensorMvp_Status_t;

void SensorMvp_Init(SensorMvp_LogFn log_fn);
void SensorMvp_Update(void);
HAL_StatusTypeDef SensorMvp_GetStatus(SensorMvp_Status_t *status);
void SensorMvp_SetGasPpmDebugOffset(uint16_t ppm_offset);
uint16_t SensorMvp_GetGasPpmDebugOffset(void);
void SensorMvp_SetRadarCalibrationLogEnabled(uint8_t enabled);
uint8_t SensorMvp_GetRadarCalibrationLogEnabled(void);

#endif
