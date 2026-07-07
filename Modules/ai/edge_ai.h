#ifndef EDGE_AI_H
#define EDGE_AI_H

#include "app_types.h"
#include "sensor_mvp.h"

#include <stdint.h>

typedef enum
{
  EDGE_AI_SCENE_NORMAL = 0,
  EDGE_AI_SCENE_ENV_COMFORT = 1,
  EDGE_AI_SCENE_GAS_RISK = 2,
  EDGE_AI_SCENE_STILLNESS_RISK = 3,
  EDGE_AI_SCENE_ACTIVITY_ANOMALY = 4,
  EDGE_AI_SCENE_SYSTEM_CONTEXT = 5
} EdgeAiScene_t;

typedef struct
{
  uint8_t valid;
  EdgeAiScene_t scene;
  EdgeAiScene_t raw_scene;
  uint8_t risk_level;
  uint8_t confidence;
  uint8_t stability;
  uint8_t evidence_mask;
  uint16_t anomaly_score;
  uint16_t trend_score;
  uint32_t sequence;
  uint32_t last_update_ms;
  uint32_t next_update_ms;
  uint32_t last_run_ms;
  uint32_t max_run_ms;
  uint32_t skipped_count;
  uint8_t ran_this_tick;
  uint8_t stale;
} EdgeAi_Result_t;

void EdgeAi_Init(void);
void EdgeAi_Update(const SensorMvp_Status_t *sensor, const AppStatus_t *app_status, uint32_t now_ms);
uint8_t EdgeAi_UpdateIfDue(const SensorMvp_Status_t *sensor, const AppStatus_t *app_status, uint32_t now_ms);
void EdgeAi_GetResult(EdgeAi_Result_t *result);
const char *EdgeAi_SceneToText(EdgeAiScene_t scene);

#endif
