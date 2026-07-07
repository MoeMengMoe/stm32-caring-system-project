#ifndef APP_TYPES_H
#define APP_TYPES_H

#include <stdint.h>

typedef enum
{
  APP_SCENARIO_NONE = 0,
  APP_SCENARIO_SOS_OR_FALL_SIM = 1,
  APP_SCENARIO_LONG_STILL_NO_RESPONSE = 2,
  APP_SCENARIO_OFFLINE_AUTONOMY = 3,
  APP_SCENARIO_GAS_RISK = 4
} AppScenario_t;

typedef enum
{
  APP_STATE_NORMAL = 0,
  APP_STATE_NOTICE = 1,
  APP_STATE_ACK_WAIT = 2,
  APP_STATE_ALARM = 3,
  APP_STATE_NO_RESPONSE = 4,
  APP_STATE_CLEARED = 5
} AppState_t;

typedef enum
{
  APP_EVENT_STATUS_ONLY = 0,
  APP_EVENT_REMOTE_TRIGGER = 1,
  APP_EVENT_SOS_BUTTON = 2,
  APP_EVENT_LONG_STILL = 3,
  APP_EVENT_USER_ACK = 4,
  APP_EVENT_ACK_TIMEOUT = 5,
  APP_EVENT_CLEAR_ALARM = 6,
  APP_EVENT_NETWORK_LOST = 7,
  APP_EVENT_NETWORK_RESTORED = 8,
  APP_EVENT_POWER_BACKUP_ENTER = 9,
  APP_EVENT_POWER_NORMAL_RESTORED = 10,
  APP_EVENT_GAS_RISK = 11,
  APP_EVENT_VOICE_RISK = 12
} AppEventType_t;

typedef enum
{
  APP_TRIGGER_LOCAL = 0,
  APP_TRIGGER_REMOTE = 1,
  APP_TRIGGER_BUTTON = 2,
  APP_TRIGGER_RADAR = 3,
  APP_TRIGGER_NETWORK = 4,
  APP_TRIGGER_POWER = 5,
  APP_TRIGGER_SENSOR = 6,
  APP_TRIGGER_VOICE = 7
} AppTriggerSource_t;

typedef enum
{
  APP_RESULT_CREATED = 0,
  APP_RESULT_WAITING_ACK = 1,
  APP_RESULT_ACKNOWLEDGED = 2,
  APP_RESULT_ESCALATED = 3,
  APP_RESULT_CLEARED = 4,
  APP_RESULT_OFFLINE_CACHED = 5,
  APP_RESULT_BACKFILLED = 6,
  APP_RESULT_FAILED = 7
} AppResult_t;

typedef enum
{
  APP_NETWORK_ONLINE = 0,
  APP_NETWORK_OFFLINE = 1,
  APP_NETWORK_RESTORED = 2
} AppNetworkState_t;

typedef enum
{
  APP_POWER_NORMAL = 0,
  APP_POWER_BACKUP = 1,
  APP_POWER_LOW = 2
} AppPowerState_t;

typedef enum
{
  APP_COMMAND_TRIGGER_SCENARIO = 1,
  APP_COMMAND_USER_ACK = 2,
  APP_COMMAND_CLEAR_ALARM = 3,
  APP_COMMAND_SIMULATE_NETWORK = 4,
  APP_COMMAND_SET_RELAY = 5,
  APP_COMMAND_DEBUG_SET_GAS_PPM_OFFSET = 6
} AppCommandType_t;

typedef struct
{
  uint32_t event_id;
  AppScenario_t scenario;
  AppEventType_t event_type;
  AppTriggerSource_t trigger_source;
  AppState_t state_before;
  AppState_t state_after;
  int risk;
  AppResult_t result;
  AppNetworkState_t network_state;
  AppPowerState_t power_state;
  uint32_t flags;
  uint32_t timestamp_ms;
} AppEventRecord_t;

typedef struct
{
  AppState_t state;
  AppScenario_t scenario;
  AppEventType_t last_event_type;
  AppNetworkState_t network_state;
  AppPowerState_t power_state;
  int risk;
  AppTriggerSource_t last_trigger_source;
  uint32_t last_event_flags;
  uint8_t relay_state_mask;
  uint8_t cloud_perm_mask;
  uint32_t last_event_id;
  uint32_t pending_log_count;
  uint32_t ack_remaining_ms;
} AppStatus_t;

const char *AppState_ToText(AppState_t state);
const char *AppScenario_ToShortText(AppScenario_t scenario);
const char *AppEventType_ToText(AppEventType_t event_type);
const char *AppTriggerSource_ToText(AppTriggerSource_t trigger_source);
const char *AppResult_ToText(AppResult_t result);
const char *AppNetworkState_ToText(AppNetworkState_t network_state);
const char *AppPowerState_ToText(AppPowerState_t power_state);
const char *AppStatus_ToDisplayText(const AppStatus_t *status);

#endif
