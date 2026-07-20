#include "app_state_machine.h"

#include "app_log.h"
#include "comm_wifi.h"

#include <string.h>

#define APP_ACK_TIMEOUT_MS                 15000UL
#define APP_ALARM_TO_NO_RESPONSE_MS        15000UL
#define APP_CLEARED_HOLD_MS                3000UL
#define APP_LONG_STILL_TRIGGER_SECONDS     120UL
#define APP_LONG_STILL_ARM_SECONDS         15UL
#define APP_LONG_STILL_MIN_ACTIVE_GATES    2U
#define APP_EVENT_QUEUE_DEPTH              8U
#define APP_EVENT_FLAG_ACTIVE_ALARM        (1UL << 2)
#define APP_EVENT_FLAG_LOCAL_ACK           (1UL << 3)
#define APP_EVENT_FLAG_VOICE_RISK_SHIFT    8U
#define APP_EVENT_FLAG_VOICE_RISK_MASK     (3UL << APP_EVENT_FLAG_VOICE_RISK_SHIFT)
#define APP_EVENT_FLAG_EDGE_AI_SCENE_SHIFT 12U
#define APP_EVENT_FLAG_EDGE_AI_RAW_SHIFT   28U
#define APP_EVENT_FLAG_EDGE_AI_RISK_SHIFT  16U
#define APP_EVENT_FLAG_EDGE_AI_EVID_SHIFT  20U
#define APP_GAS_WARN_PPM_EST               100U
#define APP_GAS_ALARM_PPM_EST              300U
#define APP_VOICE_RISK_REPEAT_HOLD_MS      2000UL
#define APP_VOICE_LOW_RISK_HOLD_MS         10000UL
#define APP_EDGE_AI_RISK1_CONF_MIN         60U
#define APP_EDGE_AI_RISK2_CONF_MIN         70U
#define APP_EDGE_AI_RISK3_CONF_MIN         80U
#define APP_EDGE_AI_NOTICE_CONF_MIN        72U
#define APP_EDGE_AI_ACK_CONF_MIN           84U
#define APP_EDGE_AI_NOTICE_STABILITY_MIN   3U
#define APP_EDGE_AI_ACK_STABILITY_MIN      4U

static AppStatus_t s_status;
static uint32_t s_next_event_id;
static uint32_t s_ack_deadline_ms;
static uint32_t s_no_response_deadline_ms;
static uint32_t s_cleared_until_ms;
static uint8_t s_long_still_latched;
static uint8_t s_gas_risk_latched;
static uint8_t s_voice_risk_floor;
static uint8_t s_last_voice_risk_level;
static uint8_t s_last_voice_risk_valid;
static uint32_t s_last_voice_risk_ms;
static uint32_t s_voice_risk_floor_until_ms;
static uint8_t s_edge_ai_latched;
static uint8_t s_edge_ai_latched_scene;

static AppEventRecord_t s_event_queue[APP_EVENT_QUEUE_DEPTH];
static uint8_t s_event_head;
static uint8_t s_event_tail;

static uint8_t queue_next(uint8_t index)
{
  index++;
  if (index >= APP_EVENT_QUEUE_DEPTH)
  {
    index = 0U;
  }
  return index;
}

static void queue_event(const AppEventRecord_t *event)
{
  uint8_t next;

  if (event == NULL)
  {
    return;
  }

  next = queue_next(s_event_head);
  if (next == s_event_tail)
  {
    s_event_tail = queue_next(s_event_tail);
  }

  s_event_queue[s_event_head] = *event;
  s_event_head = next;
}

static int state_base_risk(AppState_t state)
{
  switch (state)
  {
    case APP_STATE_ACK_WAIT:
      return 2;
    case APP_STATE_ALARM:
    case APP_STATE_NO_RESPONSE:
      return 3;
    case APP_STATE_NOTICE:
      return 1;
    case APP_STATE_CLEARED:
    case APP_STATE_NORMAL:
    default:
      return 0;
  }
}

static int compute_risk(const SensorMvp_Status_t *sensor)
{
  int risk = state_base_risk(s_status.state);

  if ((s_status.network_state == APP_NETWORK_OFFLINE) && (risk < 1))
  {
    risk = 1;
  }

  if ((s_voice_risk_floor != 0U) && (risk < (int)s_voice_risk_floor))
  {
    risk = (int)s_voice_risk_floor;
  }

  if (s_status.edge_ai_valid != 0U)
  {
    uint8_t ai_conf_min = APP_EDGE_AI_RISK1_CONF_MIN;

    if (s_status.edge_ai_risk >= 3U)
    {
      ai_conf_min = APP_EDGE_AI_RISK3_CONF_MIN;
    }
    else if (s_status.edge_ai_risk >= 2U)
    {
      ai_conf_min = APP_EDGE_AI_RISK2_CONF_MIN;
    }

    if ((s_status.edge_ai_risk > (uint8_t)risk) &&
        (s_status.edge_ai_confidence >= ai_conf_min) &&
        (s_status.edge_ai_stability >= 2U))
    {
      risk = (int)s_status.edge_ai_risk;
    }
  }

  if ((sensor != NULL) && (sensor->gas_valid != 0U))
  {
    if (sensor->gas_ppm_est >= APP_GAS_ALARM_PPM_EST)
    {
      risk = 3;
    }
    else if ((sensor->gas_ppm_est >= APP_GAS_WARN_PPM_EST) && (risk < 2))
    {
      risk = 2;
    }
  }

  return risk;
}

static uint32_t voice_risk_flags(uint8_t risk_level)
{
  return (((uint32_t)risk_level << APP_EVENT_FLAG_VOICE_RISK_SHIFT) & APP_EVENT_FLAG_VOICE_RISK_MASK);
}

static uint32_t edge_ai_flags(void)
{
  return (((uint32_t)(s_status.edge_ai_scene & 0x0FU)) << APP_EVENT_FLAG_EDGE_AI_SCENE_SHIFT) |
         (((uint32_t)(s_status.edge_ai_raw_scene & 0x0FU)) << APP_EVENT_FLAG_EDGE_AI_RAW_SHIFT) |
         (((uint32_t)(s_status.edge_ai_risk & 0x03U)) << APP_EVENT_FLAG_EDGE_AI_RISK_SHIFT) |
         (((uint32_t)s_status.edge_ai_evidence_mask) << APP_EVENT_FLAG_EDGE_AI_EVID_SHIFT);
}

static AppScenario_t edge_ai_scene_to_scenario(uint8_t scene)
{
  switch (scene)
  {
    case 2U:
      return APP_SCENARIO_GAS_RISK;
    case 3U:
      return APP_SCENARIO_LONG_STILL_NO_RESPONSE;
    case 4U:
      return APP_SCENARIO_SOS_OR_FALL_SIM;
    case 5U:
      return APP_SCENARIO_OFFLINE_AUTONOMY;
    default:
      return APP_SCENARIO_NONE;
  }
}

static void emit_event(AppScenario_t scenario,
                       AppEventType_t event_type,
                       AppTriggerSource_t trigger_source,
                       AppState_t state_before,
                       AppState_t state_after,
                       AppResult_t result,
                       uint32_t flags,
                       uint32_t now_ms,
                       const SensorMvp_Status_t *sensor)
{
  AppEventRecord_t event;

  memset(&event, 0, sizeof(event));
  event.event_id = s_next_event_id++;
  event.scenario = scenario;
  event.event_type = event_type;
  event.trigger_source = trigger_source;
  event.state_before = state_before;
  event.state_after = state_after;
  event.result = result;
  event.network_state = s_status.network_state;
  event.power_state = s_status.power_state;
  event.flags = flags;
  event.timestamp_ms = now_ms;
  event.risk = compute_risk(sensor);

  s_status.last_event_id = event.event_id;
  s_status.last_event_type = event.event_type;
  s_status.last_trigger_source = event.trigger_source;
  s_status.last_event_flags = event.flags;
  s_status.pending_log_count = AppLog_Count() + 1U;

  AppLog_Append(&event);
  queue_event(&event);
}

static void enter_ack_wait(AppScenario_t scenario,
                           AppEventType_t event_type,
                           AppTriggerSource_t trigger_source,
                           uint32_t flags,
                           uint32_t now_ms,
                           const SensorMvp_Status_t *sensor)
{
  const AppState_t before = s_status.state;

  s_status.scenario = scenario;
  s_status.state = APP_STATE_ACK_WAIT;
  s_status.risk = compute_risk(sensor);
  s_ack_deadline_ms = now_ms + APP_ACK_TIMEOUT_MS;
  s_no_response_deadline_ms = 0UL;
  s_cleared_until_ms = 0UL;

  emit_event(scenario,
              event_type,
              trigger_source,
              before,
              s_status.state,
              APP_RESULT_WAITING_ACK,
              flags,
              now_ms,
              sensor);
}

static void clear_current(AppEventType_t event_type,
                          AppTriggerSource_t trigger_source,
                          AppResult_t result,
                          uint32_t flags,
                          uint32_t now_ms,
                          const SensorMvp_Status_t *sensor)
{
  const AppState_t before = s_status.state;
  const AppScenario_t scenario = s_status.scenario;

  s_status.state = APP_STATE_CLEARED;
  s_status.risk = 0;
  s_voice_risk_floor = 0U;
  s_voice_risk_floor_until_ms = 0UL;
  s_edge_ai_latched = 0U;
  s_edge_ai_latched_scene = 0U;
  s_ack_deadline_ms = 0UL;
  s_no_response_deadline_ms = 0UL;
  s_cleared_until_ms = now_ms + APP_CLEARED_HOLD_MS;

  emit_event(scenario,
             event_type,
             trigger_source,
             before,
             s_status.state,
             result,
             flags,
             now_ms,
             sensor);
}

static void update_timeout(uint32_t now_ms, const SensorMvp_Status_t *sensor)
{
  if ((s_status.state == APP_STATE_ACK_WAIT) && (s_ack_deadline_ms != 0UL) &&
      ((int32_t)(now_ms - s_ack_deadline_ms) >= 0))
  {
    const AppState_t before = s_status.state;
    s_status.state = (s_status.scenario == APP_SCENARIO_LONG_STILL_NO_RESPONSE) ?
                     APP_STATE_NO_RESPONSE :
                     APP_STATE_ALARM;
    s_status.risk = compute_risk(sensor);
    s_ack_deadline_ms = 0UL;
    s_no_response_deadline_ms = now_ms + APP_ALARM_TO_NO_RESPONSE_MS;

    emit_event(s_status.scenario,
               APP_EVENT_ACK_TIMEOUT,
               APP_TRIGGER_LOCAL,
               before,
               s_status.state,
               APP_RESULT_ESCALATED,
               APP_EVENT_FLAG_ACTIVE_ALARM,
               now_ms,
               sensor);
    return;
  }

  if ((s_status.state == APP_STATE_ALARM) && (s_no_response_deadline_ms != 0UL) &&
      ((int32_t)(now_ms - s_no_response_deadline_ms) >= 0))
  {
    const AppState_t before = s_status.state;
    s_status.state = APP_STATE_NO_RESPONSE;
    s_status.risk = compute_risk(sensor);
    s_no_response_deadline_ms = 0UL;

    emit_event(s_status.scenario,
               APP_EVENT_ACK_TIMEOUT,
               APP_TRIGGER_LOCAL,
               before,
               s_status.state,
               APP_RESULT_ESCALATED,
               APP_EVENT_FLAG_ACTIVE_ALARM,
               now_ms,
               sensor);
  }
}

static void update_long_still(const SensorMvp_Status_t *sensor, uint32_t now_ms)
{
  if (sensor == NULL)
  {
    return;
  }

  if ((sensor->radar_valid == 0U) ||
      (sensor->radar_presence == 0U) ||
      (sensor->rd03_ot2_presence == 0U) ||
      (sensor->radar_distance_cm == 0U) ||
      (sensor->radar_active_gate_count < APP_LONG_STILL_MIN_ACTIVE_GATES) ||
      (sensor->radar_still_seconds < APP_LONG_STILL_ARM_SECONDS))
  {
    s_long_still_latched = 0U;
    return;
  }

  if ((sensor->radar_still_seconds >= APP_LONG_STILL_TRIGGER_SECONDS) &&
      (s_long_still_latched == 0U) &&
      ((s_status.state == APP_STATE_NORMAL) || (s_status.state == APP_STATE_CLEARED)))
  {
    s_long_still_latched = 1U;
    enter_ack_wait(APP_SCENARIO_LONG_STILL_NO_RESPONSE,
                   APP_EVENT_LONG_STILL,
                   APP_TRIGGER_RADAR,
                   0UL,
                   now_ms,
                   sensor);
  }
}

static void update_gas_risk(const SensorMvp_Status_t *sensor, uint32_t now_ms)
{
  if (sensor == NULL)
  {
    return;
  }

  if ((sensor->gas_valid == 0U) || (sensor->gas_ppm_est < APP_GAS_WARN_PPM_EST))
  {
    s_gas_risk_latched = 0U;
    return;
  }

  if ((s_gas_risk_latched == 0U) &&
      ((s_status.state == APP_STATE_NORMAL) || (s_status.state == APP_STATE_CLEARED)))
  {
    s_gas_risk_latched = 1U;
    enter_ack_wait(APP_SCENARIO_GAS_RISK,
                   APP_EVENT_GAS_RISK,
                   APP_TRIGGER_SENSOR,
                   0UL,
                   now_ms,
                   sensor);
  }
}

static uint8_t edge_ai_scene_can_ack(uint8_t scene)
{
  return ((scene == 2U) || (scene == 3U)) ? 1U : 0U;
}

static void update_edge_ai_risk(const SensorMvp_Status_t *sensor, uint32_t now_ms)
{
  AppScenario_t scenario;
  const uint32_t flags = edge_ai_flags();

  if ((s_status.edge_ai_valid == 0U) ||
      (s_status.edge_ai_risk == 0U) ||
      (s_status.edge_ai_confidence < APP_EDGE_AI_NOTICE_CONF_MIN))
  {
    s_edge_ai_latched = 0U;
    s_edge_ai_latched_scene = 0U;
    return;
  }

  if ((s_edge_ai_latched != 0U) && (s_edge_ai_latched_scene == s_status.edge_ai_scene))
  {
    return;
  }

  scenario = edge_ai_scene_to_scenario(s_status.edge_ai_scene);

  if ((s_status.edge_ai_risk >= 2U) &&
      (s_status.edge_ai_confidence >= APP_EDGE_AI_ACK_CONF_MIN) &&
      (s_status.edge_ai_stability >= APP_EDGE_AI_ACK_STABILITY_MIN) &&
      (edge_ai_scene_can_ack(s_status.edge_ai_scene) != 0U) &&
      ((s_status.state == APP_STATE_NORMAL) ||
       (s_status.state == APP_STATE_CLEARED) ||
       (s_status.state == APP_STATE_NOTICE)))
  {
    if (scenario == APP_SCENARIO_NONE)
    {
      scenario = APP_SCENARIO_SOS_OR_FALL_SIM;
    }
    s_edge_ai_latched = 1U;
    s_edge_ai_latched_scene = s_status.edge_ai_scene;
    enter_ack_wait(scenario,
                   APP_EVENT_EDGE_AI_RISK,
                   APP_TRIGGER_AI,
                   flags,
                   now_ms,
                   sensor);
    return;
  }

  if ((s_status.edge_ai_risk >= 1U) &&
      (s_status.edge_ai_stability >= APP_EDGE_AI_NOTICE_STABILITY_MIN) &&
      ((s_status.state == APP_STATE_NORMAL) || (s_status.state == APP_STATE_CLEARED)))
  {
    const AppState_t before = s_status.state;

    s_status.state = APP_STATE_NOTICE;
    s_status.scenario = scenario;
    s_edge_ai_latched = 1U;
    s_edge_ai_latched_scene = s_status.edge_ai_scene;
    emit_event(s_status.scenario,
               APP_EVENT_EDGE_AI_RISK,
               APP_TRIGGER_AI,
               before,
               s_status.state,
               APP_RESULT_CREATED,
               flags,
               now_ms,
               sensor);
  }
}

void AppStateMachine_Init(void)
{
  memset(&s_status, 0, sizeof(s_status));
  memset(s_event_queue, 0, sizeof(s_event_queue));
  s_status.state = APP_STATE_NORMAL;
  s_status.scenario = APP_SCENARIO_NONE;
  s_status.network_state = APP_NETWORK_ONLINE;
  s_status.power_state = APP_POWER_NORMAL;
  s_status.last_trigger_source = APP_TRIGGER_LOCAL;
  s_status.last_event_flags = 0UL;
  s_status.cloud_perm_mask = COMM_WIFI_DEFAULT_CLOUD_PERM_MASK;
  s_next_event_id = 1000UL;
  s_ack_deadline_ms = 0UL;
  s_no_response_deadline_ms = 0UL;
  s_cleared_until_ms = 0UL;
  s_long_still_latched = 0U;
  s_gas_risk_latched = 0U;
  s_voice_risk_floor = 0U;
  s_last_voice_risk_level = 0U;
  s_last_voice_risk_valid = 0U;
  s_last_voice_risk_ms = 0UL;
  s_voice_risk_floor_until_ms = 0UL;
  s_edge_ai_latched = 0U;
  s_edge_ai_latched_scene = 0U;
  s_event_head = 0U;
  s_event_tail = 0U;
  AppLog_Init();
}

void AppStateMachine_Update(const SensorMvp_Status_t *sensor, uint32_t now_ms)
{
  if ((s_status.state == APP_STATE_CLEARED) && (s_cleared_until_ms != 0UL) &&
      ((int32_t)(now_ms - s_cleared_until_ms) >= 0))
  {
    s_status.state = APP_STATE_NORMAL;
    s_status.scenario = APP_SCENARIO_NONE;
    s_cleared_until_ms = 0UL;
  }

  update_timeout(now_ms, sensor);
  if ((s_voice_risk_floor == 1U) &&
      (s_voice_risk_floor_until_ms != 0UL) &&
      ((int32_t)(now_ms - s_voice_risk_floor_until_ms) >= 0))
  {
    s_voice_risk_floor = 0U;
    s_voice_risk_floor_until_ms = 0UL;
    if ((s_status.state == APP_STATE_NOTICE) && (s_status.scenario == APP_SCENARIO_NONE))
    {
      s_status.state = APP_STATE_NORMAL;
    }
  }
  update_long_still(sensor, now_ms);
  update_gas_risk(sensor, now_ms);
  update_edge_ai_risk(sensor, now_ms);

  s_status.risk = compute_risk(sensor);
  if ((s_status.state == APP_STATE_ACK_WAIT) && (s_ack_deadline_ms != 0UL) &&
      ((int32_t)(s_ack_deadline_ms - now_ms) > 0))
  {
    s_status.ack_remaining_ms = s_ack_deadline_ms - now_ms;
  }
  else
  {
    s_status.ack_remaining_ms = 0UL;
  }
  s_status.pending_log_count = AppLog_Count();
}

void AppStateMachine_HandleDemoCommand(uint32_t request_id,
                                       AppCommandType_t command_type,
                                       AppScenario_t scenario,
                                       int value,
                                       uint32_t now_ms)
{
  (void)request_id;

  if (command_type == APP_COMMAND_TRIGGER_SCENARIO)
  {
    if (scenario == APP_SCENARIO_NONE)
    {
      scenario = APP_SCENARIO_SOS_OR_FALL_SIM;
    }
    if ((scenario == APP_SCENARIO_SOS_OR_FALL_SIM) ||
        (scenario == APP_SCENARIO_LONG_STILL_NO_RESPONSE) ||
        (scenario == APP_SCENARIO_GAS_RISK))
    {
      AppEventType_t event_type = APP_EVENT_REMOTE_TRIGGER;
      AppTriggerSource_t source = APP_TRIGGER_REMOTE;

      if (scenario == APP_SCENARIO_LONG_STILL_NO_RESPONSE)
      {
        event_type = APP_EVENT_LONG_STILL;
        source = APP_TRIGGER_RADAR;
      }
      else if (scenario == APP_SCENARIO_GAS_RISK)
      {
        event_type = APP_EVENT_GAS_RISK;
        source = APP_TRIGGER_SENSOR;
      }

      enter_ack_wait(scenario, event_type, source, 0UL, now_ms, NULL);
    }
    else if (scenario == APP_SCENARIO_OFFLINE_AUTONOMY)
    {
      AppStateMachine_HandleDemoCommand(request_id,
                                        APP_COMMAND_SIMULATE_NETWORK,
                                        scenario,
                                        0,
                                        now_ms);
    }
    return;
  }

  if (command_type == APP_COMMAND_USER_ACK)
  {
    if (s_status.state != APP_STATE_NORMAL)
    {
      clear_current(APP_EVENT_USER_ACK,
                    APP_TRIGGER_REMOTE,
                    APP_RESULT_ACKNOWLEDGED,
                    0UL,
                    now_ms,
                    NULL);
    }
    return;
  }

  if (command_type == APP_COMMAND_CLEAR_ALARM)
  {
    clear_current(APP_EVENT_CLEAR_ALARM,
                  APP_TRIGGER_REMOTE,
                  APP_RESULT_CLEARED,
                  0UL,
                  now_ms,
                  NULL);
    return;
  }

  if (command_type == APP_COMMAND_SIMULATE_NETWORK)
  {
    AppStateMachine_SetNetworkAvailable(value != 0, now_ms);
  }
}

void AppStateMachine_SetNetworkAvailable(bool online, uint32_t now_ms)
{
  const AppNetworkState_t before_network = s_status.network_state;
  const AppState_t before_state = s_status.state;

  if (!online)
  {
    s_status.network_state = APP_NETWORK_OFFLINE;
    s_status.scenario = APP_SCENARIO_OFFLINE_AUTONOMY;
    s_status.risk = compute_risk(NULL);
    if (before_network != APP_NETWORK_OFFLINE)
    {
      emit_event(APP_SCENARIO_OFFLINE_AUTONOMY,
                 APP_EVENT_NETWORK_LOST,
                 APP_TRIGGER_NETWORK,
                 before_state,
                 s_status.state,
                 APP_RESULT_OFFLINE_CACHED,
                 0UL,
                 now_ms,
                 NULL);
    }
    return;
  }

  if (before_network == APP_NETWORK_OFFLINE)
  {
    s_status.network_state = APP_NETWORK_RESTORED;
    s_status.risk = compute_risk(NULL);
    emit_event(APP_SCENARIO_OFFLINE_AUTONOMY,
               APP_EVENT_NETWORK_RESTORED,
               APP_TRIGGER_NETWORK,
               before_state,
               s_status.state,
               APP_RESULT_BACKFILLED,
               0UL,
               now_ms,
               NULL);
  }
  s_status.network_state = APP_NETWORK_ONLINE;
  s_status.risk = compute_risk(NULL);
  if (s_status.state == APP_STATE_NORMAL)
  {
    s_status.scenario = APP_SCENARIO_NONE;
  }
}

void AppStateMachine_HandleVoiceRisk(uint32_t request_id, uint8_t risk_level, uint32_t now_ms)
{
  const uint32_t flags = voice_risk_flags(risk_level);

  (void)request_id;

  if (risk_level > 3U)
  {
    return;
  }

  if ((s_last_voice_risk_valid != 0U) &&
      (risk_level == s_last_voice_risk_level) &&
      ((now_ms - s_last_voice_risk_ms) < APP_VOICE_RISK_REPEAT_HOLD_MS))
  {
    return;
  }
  s_last_voice_risk_valid = 1U;
  s_last_voice_risk_level = risk_level;
  s_last_voice_risk_ms = now_ms;

  if (risk_level == 0U)
  {
    clear_current(APP_EVENT_CLEAR_ALARM,
                  APP_TRIGGER_VOICE,
                  APP_RESULT_CLEARED,
                  flags,
                  now_ms,
                  NULL);
    return;
  }

  s_voice_risk_floor = risk_level;
  s_voice_risk_floor_until_ms = (risk_level == 1U) ? (now_ms + APP_VOICE_LOW_RISK_HOLD_MS) : 0UL;

  if ((risk_level == 1U) &&
      ((s_status.state == APP_STATE_NORMAL) || (s_status.state == APP_STATE_CLEARED)))
  {
    const AppState_t before = s_status.state;

    s_status.state = APP_STATE_NOTICE;
    s_status.scenario = APP_SCENARIO_NONE;
    emit_event(s_status.scenario,
               APP_EVENT_VOICE_RISK,
               APP_TRIGGER_VOICE,
               before,
               s_status.state,
               APP_RESULT_CREATED,
               flags,
               now_ms,
               NULL);
    return;
  }

  if ((risk_level >= 2U) &&
      ((s_status.state == APP_STATE_NORMAL) ||
       (s_status.state == APP_STATE_CLEARED) ||
       (s_status.state == APP_STATE_NOTICE)))
  {
    enter_ack_wait(APP_SCENARIO_SOS_OR_FALL_SIM,
                   APP_EVENT_VOICE_RISK,
                   APP_TRIGGER_VOICE,
                   flags,
                   now_ms,
                   NULL);
    return;
  }

  emit_event(s_status.scenario,
             APP_EVENT_VOICE_RISK,
             APP_TRIGGER_VOICE,
             s_status.state,
             s_status.state,
             APP_RESULT_CREATED,
             flags,
             now_ms,
             NULL);
}

void AppStateMachine_HandleLocalSos(uint32_t now_ms)
{
  if ((s_status.state == APP_STATE_NORMAL) || (s_status.state == APP_STATE_CLEARED))
  {
    enter_ack_wait(APP_SCENARIO_SOS_OR_FALL_SIM,
                   APP_EVENT_SOS_BUTTON,
                   APP_TRIGGER_BUTTON,
                   0UL,
                   now_ms,
                   NULL);
  }
}

void AppStateMachine_HandleLocalAck(uint32_t now_ms)
{
  if (s_status.state != APP_STATE_NORMAL)
  {
    clear_current(APP_EVENT_USER_ACK,
                  APP_TRIGGER_BUTTON,
                  APP_RESULT_ACKNOWLEDGED,
                  APP_EVENT_FLAG_LOCAL_ACK,
                  now_ms,
                  NULL);
  }
}

void AppStateMachine_SetEdgeAiHint(uint8_t valid,
                                   uint8_t scene,
                                   uint8_t raw_scene,
                                   uint8_t risk_level,
                                   uint8_t confidence,
                                   uint8_t stability,
                                   uint8_t evidence_mask,
                                   uint16_t anomaly_score,
                                   uint16_t trend_score)
{
  s_status.edge_ai_valid = (valid != 0U) ? 1U : 0U;
  s_status.edge_ai_scene = scene;
  s_status.edge_ai_raw_scene = raw_scene;
  s_status.edge_ai_risk = (risk_level > 3U) ? 3U : risk_level;
  s_status.edge_ai_confidence = (confidence > 100U) ? 100U : confidence;
  s_status.edge_ai_stability = stability;
  s_status.edge_ai_evidence_mask = evidence_mask;
  s_status.edge_ai_anomaly_score = anomaly_score;
  s_status.edge_ai_trend_score = trend_score;
}

void AppStateMachine_SetRelayStateMask(uint8_t relay_state_mask)
{
  s_status.relay_state_mask = (uint8_t)(relay_state_mask & 0x0FU);
}

void AppStateMachine_GetStatus(AppStatus_t *status)
{
  if (status == NULL)
  {
    return;
  }

  *status = s_status;
}

bool AppStateMachine_PollEvent(AppEventRecord_t *event)
{
  if ((event == NULL) || (s_event_head == s_event_tail))
  {
    return false;
  }

  *event = s_event_queue[s_event_tail];
  s_event_tail = queue_next(s_event_tail);
  return true;
}
