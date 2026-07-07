#include "scene_engine.h"

#include <string.h>

#define SCENE_GAS_WARN_PPM             100U
#define SCENE_GAS_ALARM_PPM            300U
#define SCENE_LONG_STILL_WATCH_SECONDS 20UL
#define SCENE_LONG_STILL_RISK_SECONDS  90UL
#define SCENE_LONG_STILL_MIN_ACTIVE_GATES 2U
#define SCENE_HEAT_NOTICE_C            30.0f
#define SCENE_HEAT_ACK_C               35.0f
#define SCENE_COLD_NOTICE_C            10.0f
#define SCENE_HUMIDITY_HIGH_PCT        75.0f
#define SCENE_HUMIDITY_LOW_PCT         30.0f
#define SCENE_MOTION_BURST_SCORE       1200UL
#define SCENE_MOTION_BURST_GATES       8U

static SceneEngine_Status_t s_status;

static uint32_t scene_bit(SceneId_t id)
{
  if ((id <= SCENE_ID_NONE) || (id >= 32))
  {
    return 0UL;
  }
  return (1UL << (uint32_t)id);
}

static uint8_t clamp_confidence(uint8_t confidence)
{
  return (confidence > 100U) ? 100U : confidence;
}

static uint8_t action_rank(SceneActionHint_t action)
{
  return (uint8_t)action;
}

static uint8_t signal_is_better(const SceneSignal_t *candidate, const SceneSignal_t *current)
{
  if (candidate->id == SCENE_ID_NONE)
  {
    return 0U;
  }
  if (current->id == SCENE_ID_NONE)
  {
    return 1U;
  }
  if (candidate->severity != current->severity)
  {
    return (candidate->severity > current->severity) ? 1U : 0U;
  }
  if (action_rank(candidate->action_hint) != action_rank(current->action_hint))
  {
    return (action_rank(candidate->action_hint) > action_rank(current->action_hint)) ? 1U : 0U;
  }
  return (candidate->confidence > current->confidence) ? 1U : 0U;
}

static void add_signal(SceneId_t id,
                       SceneActionHint_t action,
                       uint8_t severity,
                       uint8_t confidence,
                       uint16_t evidence_primary,
                       uint16_t evidence_secondary)
{
  SceneSignal_t signal;

  if (id == SCENE_ID_NONE)
  {
    return;
  }

  memset(&signal, 0, sizeof(signal));
  signal.id = id;
  signal.action_hint = action;
  signal.severity = severity;
  signal.confidence = clamp_confidence(confidence);
  signal.evidence_primary = evidence_primary;
  signal.evidence_secondary = evidence_secondary;

  s_status.scene_mask |= scene_bit(id);
  if (s_status.signal_count < 255U)
  {
    s_status.signal_count++;
  }
  if (signal_is_better(&signal, &s_status.top) != 0U)
  {
    s_status.top = signal;
  }
}

static void detect_app_context(const AppStatus_t *app_status)
{
  if (app_status == NULL)
  {
    return;
  }

  if (app_status->network_state == APP_NETWORK_OFFLINE)
  {
    add_signal(SCENE_ID_NETWORK_OFFLINE, SCENE_ACTION_REPORT, 1U, 100U, 1U, 0U);
  }

  if ((app_status->state == APP_STATE_ACK_WAIT) ||
      (app_status->state == APP_STATE_ALARM) ||
      (app_status->state == APP_STATE_NO_RESPONSE))
  {
    SceneActionHint_t action = SCENE_ACTION_ACK;
    uint8_t severity = 2U;

    if ((app_status->state == APP_STATE_ALARM) || (app_status->state == APP_STATE_NO_RESPONSE))
    {
      action = SCENE_ACTION_ALARM;
      severity = 3U;
    }
    add_signal(SCENE_ID_ACTIVE_ACK,
               action,
               severity,
               100U,
               (uint16_t)app_status->scenario,
               (uint16_t)app_status->ack_remaining_ms);
  }
}

static void detect_sensor_health(const SensorMvp_Status_t *sensor)
{
  uint16_t fault_bits = 0U;

  if (sensor == NULL)
  {
    add_signal(SCENE_ID_SENSOR_FAULT, SCENE_ACTION_REPORT, 1U, 100U, 0xFFFFU, 0U);
    return;
  }

  if (sensor->env_valid == 0U)
  {
    fault_bits |= 0x0001U;
  }
  if (sensor->gas_valid == 0U)
  {
    fault_bits |= 0x0002U;
  }
  if ((sensor->radar_valid == 0U) && (sensor->rd03_ot2_presence != 0U))
  {
    fault_bits |= 0x0004U;
  }

  if (fault_bits != 0U)
  {
    add_signal(SCENE_ID_SENSOR_FAULT, SCENE_ACTION_REPORT, 1U, 90U, fault_bits, 0U);
  }
}

static void detect_environment(const SensorMvp_Status_t *sensor)
{
  if ((sensor == NULL) || (sensor->env_valid == 0U))
  {
    return;
  }

  if (sensor->temperature_c >= SCENE_HEAT_ACK_C)
  {
    add_signal(SCENE_ID_HEAT_STRESS,
               SCENE_ACTION_NOTICE,
               2U,
               (sensor->presence != 0) ? 85U : 65U,
               (uint16_t)sensor->temperature_c,
               (uint16_t)sensor->humidity_pct);
  }
  else if (sensor->temperature_c >= SCENE_HEAT_NOTICE_C)
  {
    add_signal(SCENE_ID_HEAT_STRESS,
               SCENE_ACTION_REPORT,
               1U,
               (sensor->presence != 0) ? 75U : 55U,
               (uint16_t)sensor->temperature_c,
               (uint16_t)sensor->humidity_pct);
  }

  if (sensor->temperature_c <= SCENE_COLD_NOTICE_C)
  {
    add_signal(SCENE_ID_COLD_RISK,
               SCENE_ACTION_REPORT,
               1U,
               (sensor->presence != 0) ? 75U : 55U,
               (uint16_t)sensor->temperature_c,
               0U);
  }

  if (sensor->humidity_pct >= SCENE_HUMIDITY_HIGH_PCT)
  {
    add_signal(SCENE_ID_HUMIDITY_HIGH,
               SCENE_ACTION_REPORT,
               1U,
               70U,
               (uint16_t)sensor->humidity_pct,
               0U);
  }
  else if (sensor->humidity_pct <= SCENE_HUMIDITY_LOW_PCT)
  {
    add_signal(SCENE_ID_HUMIDITY_LOW,
               SCENE_ACTION_REPORT,
               1U,
               70U,
               (uint16_t)sensor->humidity_pct,
               0U);
  }
}

static void detect_gas(const SensorMvp_Status_t *sensor)
{
  if ((sensor == NULL) || (sensor->gas_valid == 0U))
  {
    return;
  }

  if (sensor->gas_ppm_est >= SCENE_GAS_ALARM_PPM)
  {
    add_signal(SCENE_ID_GAS_ALARM,
               SCENE_ACTION_ACK,
               3U,
               95U,
               sensor->gas_ppm_est,
               sensor->gas_delta_mv);
  }
  else if (sensor->gas_ppm_est >= SCENE_GAS_WARN_PPM)
  {
    add_signal(SCENE_ID_GAS_WARN,
               SCENE_ACTION_ACK,
               2U,
               85U,
               sensor->gas_ppm_est,
               sensor->gas_delta_mv);
  }
}

static void detect_radar(const SensorMvp_Status_t *sensor)
{
  uint8_t still_evidence_ok;

  if (sensor == NULL)
  {
    return;
  }

  still_evidence_ok = ((sensor->radar_valid != 0U) &&
                       (sensor->radar_presence != 0U) &&
                       (sensor->rd03_ot2_presence != 0U) &&
                       (sensor->radar_distance_cm != 0U) &&
                       (sensor->radar_active_gate_count >= SCENE_LONG_STILL_MIN_ACTIVE_GATES)) ? 1U : 0U;

  if (sensor->presence != 0)
  {
    add_signal(SCENE_ID_RADAR_PRESENCE,
               SCENE_ACTION_OBSERVE,
               0U,
               (sensor->radar_valid != 0U) ? 85U : 55U,
               sensor->radar_distance_cm,
               sensor->radar_active_gate_count);
  }

  if ((sensor->radar_valid != 0U) &&
      ((sensor->rd03_ot2_presence != sensor->radar_presence) ||
       ((sensor->pir_presence != 0U) && (sensor->radar_presence == 0U))))
  {
    add_signal(SCENE_ID_PRESENCE_CONFLICT,
               SCENE_ACTION_REPORT,
               1U,
               75U,
               (uint16_t)((sensor->pir_presence << 2U) |
                          (sensor->rd03_ot2_presence << 1U) |
                          sensor->radar_presence),
               sensor->radar_distance_cm);
  }

  if (still_evidence_ok != 0U)
  {
    if (sensor->radar_still_seconds >= SCENE_LONG_STILL_RISK_SECONDS)
    {
      add_signal(SCENE_ID_LONG_STILL_RISK,
                 SCENE_ACTION_ACK,
                 2U,
                 90U,
                 (uint16_t)sensor->radar_still_seconds,
                 sensor->radar_distance_cm);
    }
    else if (sensor->radar_still_seconds >= SCENE_LONG_STILL_WATCH_SECONDS)
    {
      add_signal(SCENE_ID_LONG_STILL_WATCH,
                 SCENE_ACTION_REPORT,
                 1U,
                 70U,
                 (uint16_t)sensor->radar_still_seconds,
                 sensor->radar_distance_cm);
    }
  }

  if ((sensor->radar_valid != 0U) &&
      ((sensor->radar_motion_score >= SCENE_MOTION_BURST_SCORE) ||
       (sensor->radar_active_gate_count >= SCENE_MOTION_BURST_GATES)))
  {
    add_signal(SCENE_ID_MOTION_BURST,
               SCENE_ACTION_REPORT,
               1U,
               60U,
               (uint16_t)((sensor->radar_motion_score > 65535UL) ? 65535UL : sensor->radar_motion_score),
               sensor->radar_active_gate_count);
  }
}

void SceneEngine_Init(void)
{
  memset(&s_status, 0, sizeof(s_status));
}

void SceneEngine_Update(const SensorMvp_Status_t *sensor, const AppStatus_t *app_status, uint32_t now_ms)
{
  (void)now_ms;

  memset(&s_status, 0, sizeof(s_status));
  detect_app_context(app_status);
  detect_sensor_health(sensor);
  detect_environment(sensor);
  detect_gas(sensor);
  detect_radar(sensor);
}

void SceneEngine_GetStatus(SceneEngine_Status_t *status)
{
  if (status == NULL)
  {
    return;
  }
  *status = s_status;
}

const char *SceneEngine_IdToText(SceneId_t id)
{
  switch (id)
  {
    case SCENE_ID_SENSOR_FAULT:
      return "SENSOR_FAULT";
    case SCENE_ID_GAS_WARN:
      return "GAS_WARN";
    case SCENE_ID_GAS_ALARM:
      return "GAS_ALARM";
    case SCENE_ID_LONG_STILL_WATCH:
      return "LONG_STILL_WATCH";
    case SCENE_ID_LONG_STILL_RISK:
      return "LONG_STILL_RISK";
    case SCENE_ID_RADAR_PRESENCE:
      return "RADAR_PRESENCE";
    case SCENE_ID_PRESENCE_CONFLICT:
      return "PRESENCE_CONFLICT";
    case SCENE_ID_MOTION_BURST:
      return "MOTION_BURST";
    case SCENE_ID_HEAT_STRESS:
      return "HEAT_STRESS";
    case SCENE_ID_COLD_RISK:
      return "COLD_RISK";
    case SCENE_ID_HUMIDITY_HIGH:
      return "HUMIDITY_HIGH";
    case SCENE_ID_HUMIDITY_LOW:
      return "HUMIDITY_LOW";
    case SCENE_ID_NETWORK_OFFLINE:
      return "NETWORK_OFFLINE";
    case SCENE_ID_ACTIVE_ACK:
      return "ACTIVE_ACK";
    case SCENE_ID_NONE:
    default:
      return "NONE";
  }
}

const char *SceneEngine_ActionToText(SceneActionHint_t action)
{
  switch (action)
  {
    case SCENE_ACTION_OBSERVE:
      return "OBSERVE";
    case SCENE_ACTION_REPORT:
      return "REPORT";
    case SCENE_ACTION_NOTICE:
      return "NOTICE";
    case SCENE_ACTION_ACK:
      return "ACK";
    case SCENE_ACTION_ALARM:
      return "ALARM";
    case SCENE_ACTION_NONE:
    default:
      return "NONE";
  }
}
