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
    case APP_SCENARIO_NONE:
    default:
      return "NONE";
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
  }

  if (status->state == APP_STATE_ALARM)
  {
    if (status->scenario == APP_SCENARIO_SOS_OR_FALL_SIM)
    {
      return "SOS ALARM";
    }
    return "ALARM";
  }

  if (status->state == APP_STATE_NO_RESPONSE)
  {
    return "NO RESP";
  }

  return AppState_ToText(status->state);
}
