#ifndef SCENE_ENGINE_H
#define SCENE_ENGINE_H

#include "app_types.h"
#include "sensor_mvp.h"

#include <stdint.h>

typedef enum
{
  SCENE_ID_NONE = 0,
  SCENE_ID_SENSOR_FAULT = 1,
  SCENE_ID_GAS_WARN = 2,
  SCENE_ID_GAS_ALARM = 3,
  SCENE_ID_LONG_STILL_WATCH = 4,
  SCENE_ID_LONG_STILL_RISK = 5,
  SCENE_ID_RADAR_PRESENCE = 6,
  SCENE_ID_PRESENCE_CONFLICT = 7,
  SCENE_ID_MOTION_BURST = 8,
  SCENE_ID_HEAT_STRESS = 9,
  SCENE_ID_COLD_RISK = 10,
  SCENE_ID_HUMIDITY_HIGH = 11,
  SCENE_ID_HUMIDITY_LOW = 12,
  SCENE_ID_NETWORK_OFFLINE = 13,
  SCENE_ID_ACTIVE_ACK = 14
} SceneId_t;

typedef enum
{
  SCENE_ACTION_NONE = 0,
  SCENE_ACTION_OBSERVE = 1,
  SCENE_ACTION_REPORT = 2,
  SCENE_ACTION_NOTICE = 3,
  SCENE_ACTION_ACK = 4,
  SCENE_ACTION_ALARM = 5
} SceneActionHint_t;

typedef struct
{
  SceneId_t id;
  SceneActionHint_t action_hint;
  uint8_t severity;
  uint8_t confidence;
  uint16_t evidence_primary;
  uint16_t evidence_secondary;
} SceneSignal_t;

typedef struct
{
  SceneSignal_t top;
  uint32_t scene_mask;
  uint8_t signal_count;
} SceneEngine_Status_t;

void SceneEngine_Init(void);
void SceneEngine_Update(const SensorMvp_Status_t *sensor, const AppStatus_t *app_status, uint32_t now_ms);
void SceneEngine_GetStatus(SceneEngine_Status_t *status);
const char *SceneEngine_IdToText(SceneId_t id);
const char *SceneEngine_ActionToText(SceneActionHint_t action);

#endif
