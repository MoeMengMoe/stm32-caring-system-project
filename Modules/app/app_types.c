#include "app_types.h"

#include <stddef.h>

const char *AppState_ToText(AppState_t state)
{
  switch (state)
  {
    case APP_STATE_NORMAL:
      return "NORMAL";
    case APP_STATE_NOTICE:
      return "NOTICE";
    case APP_STATE_ACK_WAIT:
      return "ACK WAIT";
    case APP_STATE_ALARM:
      return "ALARM";
    case APP_STATE_NO_RESPONSE:
      return "NO RESP";
    case APP_STATE_CLEARED:
      return "CLEARED";
    default:
      return "UNKNOWN";
  }
}

const char *AppScenario_ToShortText(AppScenario_t scenario)
{
  switch (scenario)
  {
    case APP_SCENARIO_SOS_OR_FALL_SIM:
      return "SOS/FALL";
    case APP_SCENARIO_LONG_STILL_NO_RESPONSE:
      return "LONGSTILL";
    case APP_SCENARIO_OFFLINE_AUTONOMY:
      return "OFFLINE";
    case APP_SCENARIO_GAS_RISK:
      return "GAS";
    case APP_SCENARIO_NONE:
    default:
      return "NONE";
  }
}

const char *AppEventType_ToText(AppEventType_t event_type)
{
  switch (event_type)
  {
    case APP_EVENT_STATUS_ONLY:
      return "STATUS_ONLY";
    case APP_EVENT_REMOTE_TRIGGER:
      return "REMOTE_TRIGGER";
    case APP_EVENT_SOS_BUTTON:
      return "SOS_BUTTON";
    case APP_EVENT_LONG_STILL:
      return "LONG_STILL";
    case APP_EVENT_USER_ACK:
      return "USER_ACK";
    case APP_EVENT_ACK_TIMEOUT:
      return "ACK_TIMEOUT";
    case APP_EVENT_CLEAR_ALARM:
      return "CLEAR_ALARM";
    case APP_EVENT_NETWORK_LOST:
      return "NETWORK_LOST";
    case APP_EVENT_NETWORK_RESTORED:
      return "NETWORK_RESTORED";
    case APP_EVENT_POWER_BACKUP_ENTER:
      return "POWER_BACKUP_ENTER";
    case APP_EVENT_POWER_NORMAL_RESTORED:
      return "POWER_NORMAL_RESTORED";
    case APP_EVENT_GAS_RISK:
      return "GAS_RISK";
    case APP_EVENT_VOICE_RISK:
      return "VOICE_RISK";
    case APP_EVENT_EDGE_AI_RISK:
      return "EDGE_AI_RISK";
    default:
      return "UNKNOWN";
  }
}

const char *AppTriggerSource_ToText(AppTriggerSource_t trigger_source)
{
  switch (trigger_source)
  {
    case APP_TRIGGER_LOCAL:
      return "LOCAL";
    case APP_TRIGGER_REMOTE:
      return "REMOTE";
    case APP_TRIGGER_BUTTON:
      return "BUTTON";
    case APP_TRIGGER_RADAR:
      return "RADAR";
    case APP_TRIGGER_NETWORK:
      return "NETWORK";
    case APP_TRIGGER_POWER:
      return "POWER";
    case APP_TRIGGER_SENSOR:
      return "SENSOR";
    case APP_TRIGGER_VOICE:
      return "VOICE";
    case APP_TRIGGER_AI:
      return "AI";
    default:
      return "UNKNOWN";
  }
}

const char *AppResult_ToText(AppResult_t result)
{
  switch (result)
  {
    case APP_RESULT_CREATED:
      return "CREATED";
    case APP_RESULT_WAITING_ACK:
      return "WAITING_ACK";
    case APP_RESULT_ACKNOWLEDGED:
      return "ACKNOWLEDGED";
    case APP_RESULT_ESCALATED:
      return "ESCALATED";
    case APP_RESULT_CLEARED:
      return "CLEARED";
    case APP_RESULT_OFFLINE_CACHED:
      return "OFFLINE_CACHED";
    case APP_RESULT_BACKFILLED:
      return "BACKFILLED";
    case APP_RESULT_FAILED:
      return "FAILED";
    default:
      return "UNKNOWN";
  }
}

const char *AppNetworkState_ToText(AppNetworkState_t network_state)
{
  switch (network_state)
  {
    case APP_NETWORK_ONLINE:
      return "ONLINE";
    case APP_NETWORK_OFFLINE:
      return "OFFLINE";
    case APP_NETWORK_RESTORED:
      return "RESTORED";
    default:
      return "UNKNOWN";
  }
}

const char *AppPowerState_ToText(AppPowerState_t power_state)
{
  switch (power_state)
  {
    case APP_POWER_NORMAL:
      return "NORMAL";
    case APP_POWER_BACKUP:
      return "BACKUP";
    case APP_POWER_LOW:
      return "LOW";
    default:
      return "UNKNOWN";
  }
}

const char *AppStatus_ToDisplayText(const AppStatus_t *status)
{
  if (status == NULL)
  {
    return "UNKNOWN";
  }

  if (status->network_state == APP_NETWORK_OFFLINE)
  {
    return "OFFLINE";
  }

  if (status->state == APP_STATE_ACK_WAIT)
  {
    if (status->scenario == APP_SCENARIO_LONG_STILL_NO_RESPONSE)
    {
      return "STILL ACK";
    }
    if (status->scenario == APP_SCENARIO_SOS_OR_FALL_SIM)
    {
      return "SOS ACK";
    }
    if (status->scenario == APP_SCENARIO_GAS_RISK)
    {
      return "GAS ACK";
    }
  }

  if (status->state == APP_STATE_ALARM)
  {
    if (status->scenario == APP_SCENARIO_SOS_OR_FALL_SIM)
    {
      return "SOS ALARM";
    }
    if (status->scenario == APP_SCENARIO_GAS_RISK)
    {
      return "GAS ALARM";
    }
    return "ALARM";
  }

  if (status->state == APP_STATE_NO_RESPONSE)
  {
    return "NO RESP";
  }

  return AppState_ToText(status->state);
}
