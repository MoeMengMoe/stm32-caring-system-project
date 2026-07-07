#include "edge_ai.h"

#include "stm32u5xx_hal.h"

#include <string.h>

#define EDGE_AI_INPUTS   12U
#define EDGE_AI_HIDDEN   8U
#define EDGE_AI_CLASSES  6U
#define EDGE_AI_INFERENCE_PERIOD_MS 250UL
#define EDGE_AI_STALE_TIMEOUT_MS    1500UL
#define EDGE_AI_EMA_OLD_WEIGHT 3.0f
#define EDGE_AI_EMA_NEW_WEIGHT 1.0f
#define EDGE_AI_EMA_WEIGHT_SUM (EDGE_AI_EMA_OLD_WEIGHT + EDGE_AI_EMA_NEW_WEIGHT)
#define EDGE_AI_EVIDENCE_ENV      (1U << 0)
#define EDGE_AI_EVIDENCE_GAS      (1U << 1)
#define EDGE_AI_EVIDENCE_PRESENCE (1U << 2)
#define EDGE_AI_EVIDENCE_RADAR    (1U << 3)
#define EDGE_AI_EVIDENCE_MOTION   (1U << 4)
#define EDGE_AI_EVIDENCE_STILL    (1U << 5)
#define EDGE_AI_EVIDENCE_NETWORK  (1U << 6)
#define EDGE_AI_EVIDENCE_HEALTH   (1U << 7)

static EdgeAi_Result_t s_result;
static float s_score_ema[EDGE_AI_CLASSES];
static uint8_t s_score_ema_ready;
static EdgeAiScene_t s_last_scene;
static uint8_t s_scene_stability;
static uint8_t s_history_valid;
static uint16_t s_last_gas_ppm;
static uint32_t s_last_radar_motion_score;
static uint16_t s_last_radar_distance_cm;
static uint32_t s_next_inference_ms;
static uint32_t s_skipped_count;
static uint8_t s_inference_busy;

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

static uint8_t max_u8(uint8_t a, uint8_t b)
{
  return (a > b) ? a : b;
}

static uint16_t clamp_u16(uint32_t value, uint16_t max_value)
{
  return (value > (uint32_t)max_value) ? max_value : (uint16_t)value;
}

static uint8_t build_evidence_mask(const SensorMvp_Status_t *sensor,
                                   const AppStatus_t *app_status)
{
  uint8_t mask = 0U;

  if (sensor != NULL)
  {
    if (sensor->env_valid != 0U)
    {
      mask |= EDGE_AI_EVIDENCE_ENV;
    }
    if ((sensor->gas_valid != 0U) &&
        ((sensor->gas_ppm_est >= 50U) || (sensor->gas_delta_mv >= 40U)))
    {
      mask |= EDGE_AI_EVIDENCE_GAS;
    }
    if (sensor->presence != 0)
    {
      mask |= EDGE_AI_EVIDENCE_PRESENCE;
    }
    if ((sensor->radar_valid != 0U) && (sensor->radar_presence != 0U))
    {
      mask |= EDGE_AI_EVIDENCE_RADAR;
    }
    if ((sensor->radar_motion_score >= 800UL) ||
        (sensor->radar_active_gate_count >= 6U))
    {
      mask |= EDGE_AI_EVIDENCE_MOTION;
    }
    if (sensor->radar_still_seconds >= 20UL)
    {
      mask |= EDGE_AI_EVIDENCE_STILL;
    }
    if ((sensor->env_valid == 0U) ||
        (sensor->gas_valid == 0U) ||
        ((sensor->rd03_ot2_presence != 0U) && (sensor->radar_valid == 0U)))
    {
      mask |= EDGE_AI_EVIDENCE_HEALTH;
    }
  }
  else
  {
    mask |= EDGE_AI_EVIDENCE_HEALTH;
  }

  if ((app_status != NULL) && (app_status->network_state == APP_NETWORK_OFFLINE))
  {
    mask |= EDGE_AI_EVIDENCE_NETWORK;
  }

  return mask;
}

static uint16_t compute_trend_score(const SensorMvp_Status_t *sensor,
                                    const AppStatus_t *app_status)
{
  uint32_t score = 0U;

  if (sensor != NULL)
  {
    if (sensor->gas_valid != 0U)
    {
      score += clamp_u16((uint32_t)sensor->gas_delta_mv * 2U, 350U);
      if ((s_history_valid != 0U) && (sensor->gas_ppm_est > s_last_gas_ppm))
      {
        score += clamp_u16(((uint32_t)sensor->gas_ppm_est - (uint32_t)s_last_gas_ppm) * 4U, 300U);
      }
    }

    score += clamp_u16(sensor->radar_motion_score / 8U, 300U);
    score += clamp_u16(sensor->radar_still_seconds * 3U, 300U);

    if ((s_history_valid != 0U) &&
        (sensor->radar_valid != 0U) &&
        (s_last_radar_distance_cm != 0U) &&
        (sensor->radar_distance_cm != 0U))
    {
      uint32_t distance_delta = (sensor->radar_distance_cm > s_last_radar_distance_cm) ?
                                ((uint32_t)sensor->radar_distance_cm - s_last_radar_distance_cm) :
                                ((uint32_t)s_last_radar_distance_cm - sensor->radar_distance_cm);
      score += clamp_u16(distance_delta * 2U, 120U);
    }

    if ((s_history_valid != 0U) && (sensor->radar_motion_score > s_last_radar_motion_score))
    {
      score += clamp_u16((sensor->radar_motion_score - s_last_radar_motion_score) / 4U, 150U);
    }

    if ((sensor->radar_valid != 0U) &&
        ((sensor->rd03_ot2_presence != sensor->radar_presence) ||
         ((sensor->pir_presence != 0U) && (sensor->radar_presence == 0U))))
    {
      score += 120U;
    }
  }

  if ((app_status != NULL) && (app_status->network_state == APP_NETWORK_OFFLINE))
  {
    score += 80U;
  }

  return clamp_u16(score, 1000U);
}

static void update_history(const SensorMvp_Status_t *sensor)
{
  if (sensor == NULL)
  {
    return;
  }

  s_last_gas_ppm = sensor->gas_ppm_est;
  s_last_radar_motion_score = sensor->radar_motion_score;
  s_last_radar_distance_cm = sensor->radar_distance_cm;
  s_history_valid = 1U;
}

static void update_scene_stability(EdgeAiScene_t scene)
{
  if ((s_result.valid != 0U) && (scene == s_last_scene))
  {
    if (s_scene_stability < 255U)
    {
      s_scene_stability++;
    }
  }
  else
  {
    s_scene_stability = 1U;
    s_last_scene = scene;
  }
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

static void apply_safety_fusion(const SensorMvp_Status_t *sensor,
                                const AppStatus_t *app_status,
                                EdgeAiScene_t *scene,
                                uint8_t *risk_level,
                                uint8_t *confidence,
                                uint16_t trend_score,
                                uint8_t evidence_mask)
{
  if ((scene == NULL) || (risk_level == NULL) || (confidence == NULL))
  {
    return;
  }

  if ((sensor != NULL) && (sensor->gas_valid != 0U))
  {
    if (sensor->gas_ppm_est >= 300U)
    {
      *scene = EDGE_AI_SCENE_GAS_RISK;
      *risk_level = 3U;
      *confidence = max_u8(*confidence, 95U);
    }
    else if ((sensor->gas_ppm_est >= 100U) ||
             ((sensor->gas_delta_mv >= 120U) && (trend_score >= 250U)))
    {
      *scene = EDGE_AI_SCENE_GAS_RISK;
      *risk_level = max_u8(*risk_level, 2U);
      *confidence = max_u8(*confidence, 84U);
    }
  }

  if ((sensor != NULL) &&
      ((evidence_mask & EDGE_AI_EVIDENCE_STILL) != 0U) &&
      ((evidence_mask & EDGE_AI_EVIDENCE_RADAR) != 0U))
  {
    if (sensor->radar_still_seconds >= 90UL)
    {
      *scene = EDGE_AI_SCENE_STILLNESS_RISK;
      *risk_level = max_u8(*risk_level, 2U);
      *confidence = max_u8(*confidence, 88U);
    }
    else if (sensor->radar_still_seconds >= 45UL)
    {
      *scene = EDGE_AI_SCENE_STILLNESS_RISK;
      *risk_level = max_u8(*risk_level, 1U);
      *confidence = max_u8(*confidence, 72U);
    }
  }

  if ((sensor != NULL) &&
      ((sensor->radar_motion_score >= 2200UL) ||
       (sensor->radar_active_gate_count >= 9U)))
  {
    *scene = EDGE_AI_SCENE_ACTIVITY_ANOMALY;
    *risk_level = max_u8(*risk_level, 1U);
    *confidence = max_u8(*confidence, 70U);
  }

  if ((app_status != NULL) && (app_status->network_state == APP_NETWORK_OFFLINE))
  {
    if (*risk_level == 0U)
    {
      *scene = EDGE_AI_SCENE_SYSTEM_CONTEXT;
      *risk_level = 1U;
      *confidence = max_u8(*confidence, 80U);
    }
  }

  if ((evidence_mask & EDGE_AI_EVIDENCE_HEALTH) != 0U)
  {
    *confidence = (*confidence > 88U) ? 88U : *confidence;
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
  memset(s_score_ema, 0, sizeof(s_score_ema));
  s_result.valid = 0U;
  s_result.scene = EDGE_AI_SCENE_NORMAL;
  s_result.raw_scene = EDGE_AI_SCENE_NORMAL;
  s_score_ema_ready = 0U;
  s_last_scene = EDGE_AI_SCENE_NORMAL;
  s_scene_stability = 0U;
  s_history_valid = 0U;
  s_last_gas_ppm = 0U;
  s_last_radar_motion_score = 0UL;
  s_last_radar_distance_cm = 0U;
  s_next_inference_ms = 0UL;
  s_skipped_count = 0UL;
  s_inference_busy = 0U;
}

void EdgeAi_Update(const SensorMvp_Status_t *sensor, const AppStatus_t *app_status, uint32_t now_ms)
{
  float x[EDGE_AI_INPUTS];
  float h[EDGE_AI_HIDDEN];
  float y[EDGE_AI_CLASSES];
  uint32_t best_index = 0U;
  uint32_t second_index = 0U;
  uint32_t raw_best_index = 0U;
  uint8_t evidence_mask;
  uint16_t trend_score;
  EdgeAiScene_t final_scene;
  uint8_t final_risk;
  uint8_t final_confidence;

  (void)now_ms;

  build_features(sensor, app_status, x);
  evidence_mask = build_evidence_mask(sensor, app_status);
  trend_score = compute_trend_score(sensor, app_status);

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
    if (y[i] > y[raw_best_index])
    {
      raw_best_index = i;
    }
  }

  if (s_score_ema_ready == 0U)
  {
    for (uint32_t i = 0U; i < EDGE_AI_CLASSES; i++)
    {
      s_score_ema[i] = y[i];
    }
    s_score_ema_ready = 1U;
  }
  else
  {
    for (uint32_t i = 0U; i < EDGE_AI_CLASSES; i++)
    {
      s_score_ema[i] = ((s_score_ema[i] * EDGE_AI_EMA_OLD_WEIGHT) +
                        (y[i] * EDGE_AI_EMA_NEW_WEIGHT)) /
                       EDGE_AI_EMA_WEIGHT_SUM;
    }
  }

  for (uint32_t i = 1U; i < EDGE_AI_CLASSES; i++)
  {
    if (s_score_ema[i] > s_score_ema[best_index])
    {
      second_index = best_index;
      best_index = i;
    }
    else if ((i != best_index) &&
             ((second_index == best_index) || (s_score_ema[i] > s_score_ema[second_index])))
    {
      second_index = i;
    }
  }

  final_scene = (EdgeAiScene_t)best_index;
  final_confidence = confidence_from_margin(s_score_ema[best_index], s_score_ema[second_index]);
  final_risk = class_to_risk(final_scene, sensor);
  apply_safety_fusion(sensor,
                      app_status,
                      &final_scene,
                      &final_risk,
                      &final_confidence,
                      trend_score,
                      evidence_mask);
  update_scene_stability(final_scene);

  s_result.valid = (sensor != NULL) ? 1U : 0U;
  s_result.scene = final_scene;
  s_result.raw_scene = (EdgeAiScene_t)raw_best_index;
  s_result.confidence = final_confidence;
  s_result.risk_level = final_risk;
  s_result.stability = s_scene_stability;
  s_result.evidence_mask = evidence_mask;
  s_result.anomaly_score = anomaly_score_from_result(s_result.scene,
                                                     s_result.confidence,
                                                     s_result.risk_level);
  if (trend_score > s_result.anomaly_score)
  {
    s_result.anomaly_score = trend_score;
  }
  s_result.trend_score = trend_score;
  s_result.sequence++;
  s_result.last_update_ms = now_ms;
  s_result.stale = 0U;
  s_result.ran_this_tick = 1U;
  update_history(sensor);
}

uint8_t EdgeAi_UpdateIfDue(const SensorMvp_Status_t *sensor, const AppStatus_t *app_status, uint32_t now_ms)
{
  uint32_t start_ms;
  uint32_t elapsed_ms;

  if (s_inference_busy != 0U)
  {
    s_skipped_count++;
    s_result.skipped_count = s_skipped_count;
    s_result.ran_this_tick = 0U;
    return 0U;
  }

  if ((s_result.sequence != 0UL) && ((int32_t)(now_ms - s_next_inference_ms) < 0))
  {
    s_skipped_count++;
    s_result.skipped_count = s_skipped_count;
    s_result.ran_this_tick = 0U;
    if ((s_result.last_update_ms != 0UL) &&
        ((now_ms - s_result.last_update_ms) > EDGE_AI_STALE_TIMEOUT_MS))
    {
      s_result.stale = 1U;
    }
    return 0U;
  }

  s_inference_busy = 1U;
  start_ms = HAL_GetTick();
  EdgeAi_Update(sensor, app_status, now_ms);
  elapsed_ms = HAL_GetTick() - start_ms;
  s_inference_busy = 0U;

  s_result.last_run_ms = elapsed_ms;
  if (elapsed_ms > s_result.max_run_ms)
  {
    s_result.max_run_ms = elapsed_ms;
  }
  s_result.next_update_ms = now_ms + EDGE_AI_INFERENCE_PERIOD_MS;
  s_result.skipped_count = s_skipped_count;
  s_result.ran_this_tick = 1U;
  s_result.stale = 0U;
  s_next_inference_ms = s_result.next_update_ms;

  return 1U;
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
