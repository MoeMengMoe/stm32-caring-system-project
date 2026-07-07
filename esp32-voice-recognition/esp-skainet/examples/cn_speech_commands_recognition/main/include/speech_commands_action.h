/* 
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#ifndef _SPEECH_COMMANDS_ACTION_H_
#define _SPEECH_COMMANDS_ACTION_H_

#include <stdbool.h>

typedef enum {
    CARE_VOICE_PROMPT_WAKEUP = 0,
    CARE_VOICE_PROMPT_COMMAND_RECEIVED,
    CARE_VOICE_PROMPT_RECORDED,
    CARE_VOICE_PROMPT_VERIFY,
    CARE_VOICE_PROMPT_VERIFY_SHORT,
    CARE_VOICE_PROMPT_CANCEL,
    CARE_VOICE_PROMPT_SOS_ALARM,
    CARE_VOICE_PROMPT_TIMEOUT_ESCALATE,
} care_voice_prompt_t;

void care_voice_prompt_init(void);
void care_voice_prompt_play(care_voice_prompt_t prompt);
void care_voice_prompt_request(care_voice_prompt_t prompt);
void care_voice_prompt_request_with_followup(care_voice_prompt_t prompt, care_voice_prompt_t followup);
void care_voice_prompt_set_followup(care_voice_prompt_t followup);
bool care_voice_is_busy(void);
bool care_voice_is_in_cooldown(void);

void wake_up_action(void);

void led_Task(void *arg);
void speech_commands_action(int command_id);

#endif
