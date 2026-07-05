#ifndef APP_LOG_H
#define APP_LOG_H

#include "app_types.h"

#include <stdbool.h>
#include <stdint.h>

#define APP_LOG_CAPACITY 16U

void AppLog_Init(void);
void AppLog_Append(const AppEventRecord_t *event);
uint32_t AppLog_Count(void);
bool AppLog_GetLatest(AppEventRecord_t *event);

#endif
