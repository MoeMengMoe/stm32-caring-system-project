#include "edge_ai.h"

#include <string.h>

#define EDGE_AI_INPUTS   12U
#define EDGE_AI_HIDDEN   8U
#define EDGE_AI_CLASSES  6U

static EdgeAi_Result_t s_result;

/*
 * Tiny bootstrap MLP for local edge risk inference.
 *
 * The feature extractor is intentionally compact and deterministic so the
 * model can run inside the STM32 super-loop without heap allocation. The
 * weights are the first calibrated prototype weights; later collection runs
 * can replace these constants without changing firmware architecture.
 */
static const float s_w1[EDGE_AI_HIDDEN][EDGE_AI_INPUTS] =
{
  { 0.0f, 0.0f, 0.0f, 2.4f, 1.1f, 0.0f, 0.0f, 0.0f, 0.2f, 0.0f, 0.0f, 0.0f },
  { 0.0f, 0.0f, 0.0f, 0.1f, 0.0f, 1.4f, 1.3f, 0.2f, 0.6f, 0.0f, 2.0f, 0.0f },
  { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.8f, 0.8f, 0.3f, 1.1f, 1.7f, 0.0f, 0.0f },
  { 1.2f, 1.1f, 0.8f, 0.0f, 0.0f, 0.2f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
  { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.7f, 1.0f, 0.5f, 0.8f, 0.2f, 0.3f, 0.0f },
  { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.8f },
  { 0.4f, 0.4f, 0.2f, 0.8f, 0.5f, 0.4f, 0.4f, 0.2f, 0.3f, 0.4f, 0.6f, 0.5f },
  {-0.5f,-0.5f,-0.4f,-1.2f,-0.6f, 0.0f, 0.0f, 0.0f,-0.3f,-0.3f,-0.6f,-0.5f }
};

static const float s_b1[EDGE_AI_HIDDEN] =
{
  -0.35f, -2.05f, -0.75f, -0.45f, -0.95f, -0.30f, -0.20f, 1.20f
};

static const float s_w2[EDGE_AI_CLASSES][EDGE_AI_HIDDEN] =
{
  {-0.9f, -0.8f, -0.5f, -0.5f,  0.1f, -0.6f, -0.5f,  1.7f },
  {-0.2f, -0.2f, -0.1f,  1.5f,  0.0f,  0.0f,  0.3f, -0.4f },
  { 2.1f,  0.1f,  0.0f, -0.1f,  0.1f,  0.0f,  0.6f, -0.9f },
  { 0.1f,  2.0f,  0.1f,  0.0f,  0.6f,  0.0f,  0.4f, -0.8f },
  { 0.0f,  0.2f,  1.8f,  0.0f,  0.6f,  0.0f,  0.3f, -0.6f },
  { 0.0f,  0.1f,  0.0f,  0.1f,  0.0f,  1.8f,  0.5f, -0.6f }
};

static const float s_b2[EDGE_AI_CLASSES] =
{
  0.25f, -0.35f, -0.45f, -0.55f, -0.45f, -0.40f
};

static float clamp01(float value)
{
  if (value < 0.0f)
  {
    return 0.0f;
  }
  if (value > 1.0f)
  {
    return 1.0f;
  }
  return value;
}

static float relu(float value)
{
  return (value > 0.0f) ? value : 0.0f;
}

static uint8_t confidence_from_margin(float best, float second)
{
  float margin = best - second;
  int confidence = 50 + (int)(margin * 18.0f);

  if (confidence < 35)
  {
    confidence = 35;
  }
  if (confidence > 99)
  {
    confidence = 99;
  }
  return (uint8_t)confidence;
}

static uint8_t class_to_risk(EdgeAiScene_t scene, const SensorMvp_Status_t *sensor)
{
  switch (scene)
  {
    case EDGE_AI_SCENE_GAS_RISK:
      if ((sensor != NULL) && (sensor->gas_ppm_est >= 300U))
      {
        return 3U;
      }
      return 2U;

    case EDGE_AI_SCENE_STILLNESS_RISK:
      return 2U;

    case EDGE_AI_SCENE_ACTIVITY_ANOMALY:
      return 1U;

    case EDGE_AI_SCENE_SYSTEM_CONTEXT:
    case EDGE_AI_SCENE_ENV_COMFORT:
      return 1U;

    case EDGE_AI_SCENE_NORMAL:
    default:
      return 0U;
  }
}

static uint16_t anomaly_score_from_result(EdgeAiScene_t scene,
                                          uint8_t confidence,
                                          uint8_t risk_level)
{
  uint32_t score = ((uint32_t)risk_level * 250U) + ((uint32_t)confidence * 2U);

  if (scene == EDGE_AI_SCENE_NORMAL)
  {
    score = (uint32_t)(100U - confidence);
  }
  if (score > 1000U)
  {
    score = 1000U;
  }
  return (uint16_t)score;
}

static void build_features(const SensorMvp_Status_t *sensor,
                           const AppStatus_t *app_status,
                           float x[EDGE_AI_INPUTS])
{
  memset(x, 0, sizeof(float) * EDGE_AI_INPUTS);

  if (sensor != NULL)
  {
    if (sensor->env_valid != 0U)
    {
      x[0] = clamp01((sensor->temperature_c - 25.0f) / 15.0f);
      x[1] = clamp01((15.0f - sensor->temperature_c) / 15.0f);
      x[2] = clamp01((sensor->humidity_pct - 65.0f) / 35.0f);
    }

    if (sensor->gas_valid != 0U)
    {
      x[3] = clamp01((float)sensor->gas_ppm_est / 300.0f);
      x[4] = clamp01((float)sensor->gas_delta_mv / 300.0f);
    }

    x[5] = (sensor->presence != 0) ? 1.0f : 0.0f;
    x[6] = ((sensor->radar_valid != 0U) && (sensor->radar_presence != 0U)) ? 1.0f : 0.0f;
    x[7] = (sensor->radar_distance_cm == 0U) ? 0.0f :
           clamp01(1.0f - ((float)sensor->radar_distance_cm / 320.0f));
    x[8] = clamp01((float)sensor->radar_active_gate_count / 12.0f);
    x[9] = clamp01((float)sensor->radar_motion_score / 3000.0f);
    x[10] = clamp01((float)sensor->radar_still_seconds / 180.0f);
  }

  if (app_status != NULL)
  {
    x[11] = (app_status->network_state == APP_NETWORK_OFFLINE) ? 1.0f : 0.0f;
  }
}

void EdgeAi_Init(void)
{
  memset(&s_result, 0, sizeof(s_result));
  s_result.valid = 0U;
  s_result.scene = EDGE_AI_SCENE_NORMAL;
}

void EdgeAi_Update(const SensorMvp_Status_t *sensor, const AppStatus_t *app_status, uint32_t now_ms)
{
  float x[EDGE_AI_INPUTS];
  float h[EDGE_AI_HIDDEN];
  float y[EDGE_AI_CLASSES];
  uint32_t best_index = 0U;
  uint32_t second_index = 0U;

  (void)now_ms;

  build_features(sensor, app_status, x);

  for (uint32_t i = 0U; i < EDGE_AI_HIDDEN; i++)
  {
    float sum = s_b1[i];
    for (uint32_t j = 0U; j < EDGE_AI_INPUTS; j++)
    {
      sum += s_w1[i][j] * x[j];
    }
    h[i] = relu(sum);
  }

  for (uint32_t i = 0U; i < EDGE_AI_CLASSES; i++)
  {
    float sum = s_b2[i];
    for (uint32_t j = 0U; j < EDGE_AI_HIDDEN; j++)
    {
      sum += s_w2[i][j] * h[j];
    }
    y[i] = sum;
  }

  for (uint32_t i = 1U; i < EDGE_AI_CLASSES; i++)
  {
    if (y[i] > y[best_index])
    {
      second_index = best_index;
      best_index = i;
    }
    else if ((i != best_index) && ((second_index == best_index) || (y[i] > y[second_index])))
    {
      second_index = i;
    }
  }

  s_result.valid = (sensor != NULL) ? 1U : 0U;
  s_result.scene = (EdgeAiScene_t)best_index;
  s_result.confidence = confidence_from_margin(y[best_index], y[second_index]);
  s_result.risk_level = class_to_risk(s_result.scene, sensor);
  s_result.anomaly_score = anomaly_score_from_result(s_result.scene,
                                                     s_result.confidence,
                                                     s_result.risk_level);
  s_result.sequence++;
}

void EdgeAi_GetResult(EdgeAi_Result_t *result)
{
  if (result == NULL)
  {
    return;
  }
  *result = s_result;
}

const char *EdgeAi_SceneToText(EdgeAiScene_t scene)
{
  switch (scene)
  {
    case EDGE_AI_SCENE_ENV_COMFORT:
      return "ENV_COMFORT";
    case EDGE_AI_SCENE_GAS_RISK:
      return "GAS_RISK";
    case EDGE_AI_SCENE_STILLNESS_RISK:
      return "STILLNESS_RISK";
    case EDGE_AI_SCENE_ACTIVITY_ANOMALY:
      return "ACTIVITY_ANOMALY";
    case EDGE_AI_SCENE_SYSTEM_CONTEXT:
      return "SYSTEM_CONTEXT";
    case EDGE_AI_SCENE_NORMAL:
    default:
      return "NORMAL";
  }
}
