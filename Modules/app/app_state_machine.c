#include "app_state_machine.h"

#include "app_log.h"
#include "comm_wifi.h"

#include <string.h>

#define APP_ACK_TIMEOUT_MS                 15000UL
#define APP_ALARM_TO_NO_RESPONSE_MS        15000UL
#define APP_CLEARED_HOLD_MS                3000UL
#define APP_LONG_STILL_TRIGGER_SECONDS     20UL
#define APP_EVENT_QUEUE_DEPTH              8U
#define APP_EVENT_FLAG_ACTIVE_ALARM        (1UL << 2)
#define APP_EVENT_FLAG_LOCAL_ACK           (1UL << 3)
#define APP_GAS_WARN_MV                    2000
#define APP_GAS_ALARM_MV                   3000

static AppStatus_t s_status;
static uint32_t s_next_event_id;
static uint32_t s_ack_deadline_ms;
static uint32_t s_no_response_deadline_ms;
static uint32_t s_cleared_until_ms;
static uint8_t s_long_still_latched;

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

  if ((sensor != NULL) && (sensor->gas_valid != 0U))
  {
    if (sensor->gas >= APP_GAS_ALARM_MV)
    {
      risk = 3;
    }
    else if ((sensor->gas >= APP_GAS_WARN_MV) && (risk < 2))
    {
      risk = 2;
    }
  }

  return risk;
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
  s_status.pending_log_count = AppLog_Count() + 1U;

  AppLog_Append(&event);
  queue_event(&event);
}

static void enter_ack_wait(AppScenario_t scenario,
                           AppEventType_t event_type,
                           AppTriggerSource_t trigger_source,
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
             0UL,
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

  if ((sensor->radar_valid == 0U) || (sensor->radar_presence == 0U) ||
      (sensor->radar_still_seconds < 5UL))
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
  s_status.cloud_perm_mask = COMM_WIFI_DEFAULT_CLOUD_PERM_MASK;
  s_next_event_id = 1000UL;
  s_ack_deadline_ms = 0UL;
  s_no_response_deadline_ms = 0UL;
  s_cleared_until_ms = 0UL;
  s_long_still_latched = 0U;
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
  update_long_still(sensor, now_ms);

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
        (scenario == APP_SCENARIO_LONG_STILL_NO_RESPONSE))
    {
      const AppEventType_t event_type = (scenario == APP_SCENARIO_LONG_STILL_NO_RESPONSE) ?
                                        APP_EVENT_LONG_STILL :
                                        APP_EVENT_REMOTE_TRIGGER;
      const AppTriggerSource_t source = (scenario == APP_SCENARIO_LONG_STILL_NO_RESPONSE) ?
                                        APP_TRIGGER_RADAR :
                                        APP_TRIGGER_REMOTE;
      enter_ack_wait(scenario, event_type, source, now_ms, NULL);
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
    const AppNetworkState_t before_network = s_status.network_state;
    const AppState_t before_state = s_status.state;

    if (value == 0)
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
    }
    else
    {
      s_status.network_state = APP_NETWORK_RESTORED;
      s_status.risk = compute_risk(NULL);
      if (before_network != APP_NETWORK_RESTORED)
      {
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
      if (s_status.state == APP_STATE_NORMAL)
      {
        s_status.scenario = APP_SCENARIO_NONE;
      }
    }
  }
}

void AppStateMachine_HandleLocalSos(uint32_t now_ms)
{
  if ((s_status.state == APP_STATE_NORMAL) || (s_status.state == APP_STATE_CLEARED))
  {
    enter_ack_wait(APP_SCENARIO_SOS_OR_FALL_SIM,
                   APP_EVENT_SOS_BUTTON,
                   APP_TRIGGER_BUTTON,
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
