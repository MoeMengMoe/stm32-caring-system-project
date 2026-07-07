#ifndef APP_CARE_LOGIC_H
#define APP_CARE_LOGIC_H

#ifdef __cplusplus
extern "C" {
#endif

void app_care_init(void);
void app_care_on_wake(void);
void app_care_poll(void);
void app_care_on_command(int command_id, const char *phrase, float probability);

#ifdef __cplusplus
}
#endif

#endif
