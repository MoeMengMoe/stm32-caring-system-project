#include "app_log.h"

#include <string.h>

static AppEventRecord_t s_events[APP_LOG_CAPACITY];
static uint8_t s_next_index;
static uint8_t s_count;

void AppLog_Init(void)
{
  memset(s_events, 0, sizeof(s_events));
  s_next_index = 0U;
  s_count = 0U;
}

void AppLog_Append(const AppEventRecord_t *event)
{
  if (event == NULL)
  {
    return;
  }

  s_events[s_next_index] = *event;
  s_next_index++;
  if (s_next_index >= APP_LOG_CAPACITY)
  {
    s_next_index = 0U;
  }

  if (s_count < APP_LOG_CAPACITY)
  {
    s_count++;
  }
}

uint32_t AppLog_Count(void)
{
  return s_count;
}

bool AppLog_GetLatest(AppEventRecord_t *event)
{
  uint8_t index;

  if ((event == NULL) || (s_count == 0U))
  {
    return false;
  }

  index = (s_next_index == 0U) ? (APP_LOG_CAPACITY - 1U) : (uint8_t)(s_next_index - 1U);
  *event = s_events[index];
  return true;
}
