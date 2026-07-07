#include "speech_commands_action.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_board_init.h"

#define CARE_VOICE_QUEUE_DEPTH 1
#define CARE_VOICE_TASK_STACK_SIZE 4096
#define CARE_VOICE_TASK_PRIORITY 4
#define CARE_VOICE_PLAY_TIMEOUT_MS 4000
#define CARE_VOICE_COOLDOWN_MS 700

#define DECLARE_EMBEDDED_PROMPT(name) \
    extern const uint8_t name##_start[]; \
    extern const uint8_t name##_end[]

DECLARE_EMBEDDED_PROMPT(care_prompt_cancel);
DECLARE_EMBEDDED_PROMPT(care_prompt_command_received);
DECLARE_EMBEDDED_PROMPT(care_prompt_recorded);
DECLARE_EMBEDDED_PROMPT(care_prompt_sos_alarm);
DECLARE_EMBEDDED_PROMPT(care_prompt_timeout_escalate);
DECLARE_EMBEDDED_PROMPT(care_prompt_verify);
DECLARE_EMBEDDED_PROMPT(care_prompt_verify_short);
DECLARE_EMBEDDED_PROMPT(care_prompt_wakeup);

typedef struct {
    const char *name;
    const unsigned char *data;
    const unsigned char *end;
} care_voice_item_t;

typedef struct {
    care_voice_prompt_t prompt;
    int followup;
} care_voice_request_t;

static const char *TAG = "CARE_VOICE";
static QueueHandle_t s_voice_queue = NULL;
static bool s_voice_task_started = false;
static volatile bool s_voice_playing = false;
static volatile TickType_t s_voice_cooldown_until = 0;
static volatile int s_pending_prompt = -1;
static volatile int s_followup_prompt = -1;
static volatile int s_active_priority = 0;

static const care_voice_item_t s_voice_items[] = {
    [CARE_VOICE_PROMPT_WAKEUP] = {
        .name = "wakeup",
        .data = care_prompt_wakeup_start,
        .end = care_prompt_wakeup_end,
    },
    [CARE_VOICE_PROMPT_COMMAND_RECEIVED] = {
        .name = "command_received",
        .data = care_prompt_command_received_start,
        .end = care_prompt_command_received_end,
    },
    [CARE_VOICE_PROMPT_RECORDED] = {
        .name = "recorded",
        .data = care_prompt_recorded_start,
        .end = care_prompt_recorded_end,
    },
    [CARE_VOICE_PROMPT_VERIFY] = {
        .name = "verify",
        .data = care_prompt_verify_start,
        .end = care_prompt_verify_end,
    },
    [CARE_VOICE_PROMPT_VERIFY_SHORT] = {
        .name = "verify_short",
        .data = care_prompt_verify_short_start,
        .end = care_prompt_verify_short_end,
    },
    [CARE_VOICE_PROMPT_CANCEL] = {
        .name = "cancel",
        .data = care_prompt_cancel_start,
        .end = care_prompt_cancel_end,
    },
    [CARE_VOICE_PROMPT_SOS_ALARM] = {
        .name = "sos_alarm",
        .data = care_prompt_sos_alarm_start,
        .end = care_prompt_sos_alarm_end,
    },
    [CARE_VOICE_PROMPT_TIMEOUT_ESCALATE] = {
        .name = "timeout_escalate",
        .data = care_prompt_timeout_escalate_start,
        .end = care_prompt_timeout_escalate_end,
    },
};

static int care_voice_prompt_priority(care_voice_prompt_t prompt)
{
    switch (prompt) {
        case CARE_VOICE_PROMPT_SOS_ALARM:
        case CARE_VOICE_PROMPT_TIMEOUT_ESCALATE:
            return 4;
        case CARE_VOICE_PROMPT_CANCEL:
            return 3;
        case CARE_VOICE_PROMPT_VERIFY:
        case CARE_VOICE_PROMPT_VERIFY_SHORT:
        case CARE_VOICE_PROMPT_RECORDED:
            return 2;
        case CARE_VOICE_PROMPT_WAKEUP:
        case CARE_VOICE_PROMPT_COMMAND_RECEIVED:
        default:
            return 1;
    }
}

static void care_voice_task(void *arg)
{
    (void)arg;

    while (true) {
        care_voice_request_t request;
        if (xQueueReceive(s_voice_queue, &request, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        s_pending_prompt = -1;

        /*
         * Keep s_voice_playing true across the entire prompt chain so the
         * detect loop skips all AFE frames during playback.  This prevents
         * the speaker output from being picked up by the microphone and
         * recognised as a false command (echo / feedback loop).
         */
        s_voice_playing = true;
        s_active_priority = care_voice_prompt_priority(request.prompt);
        care_voice_prompt_t prompt = request.prompt;
        int followup = request.followup;

        while (true) {
            int prompt_id = (int)prompt;
            if (prompt_id < 0 || prompt_id >= (int)(sizeof(s_voice_items) / sizeof(s_voice_items[0]))) {
                ESP_LOGW(TAG, "ignore invalid prompt id=%d", prompt_id);
                break;
            }

            const care_voice_item_t *item = &s_voice_items[prompt];
            int length = (int)(item->end - item->data);
            if (!item->data || !item->end || length <= 0) {
                ESP_LOGW(TAG, "ignore empty prompt id=%d", prompt_id);
                break;
            }

            ESP_LOGI(TAG, "play prompt: %s bytes=%d", item->name, length);
            esp_err_t ret = esp_audio_play((const int16_t *)item->data,
                                           length,
                                           pdMS_TO_TICKS(CARE_VOICE_PLAY_TIMEOUT_MS));
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "prompt play failed: %s ret=%s", item->name, esp_err_to_name(ret));
            }

            /*
             * Decide what to play next.  A higher-priority prompt that
             * arrived via xQueueOverwrite during playback takes precedence
             * over any follow-up that was set before the chain started.
             */
            care_voice_request_t next;
            if (xQueueReceive(s_voice_queue, &next, 0) == pdTRUE) {
                s_pending_prompt = -1;
                s_followup_prompt = -1;
                prompt = next.prompt;
                followup = next.followup;
                s_active_priority = care_voice_prompt_priority(prompt);
                continue;
            }

            if (followup >= 0) {
                prompt = (care_voice_prompt_t)followup;
                followup = -1;
                continue;
            }

            if (s_followup_prompt >= 0) {
                prompt = (care_voice_prompt_t)s_followup_prompt;
                s_followup_prompt = -1;
                continue;
            }

            break;
        }

        s_active_priority = 0;
        s_voice_playing = false;
        s_voice_cooldown_until = xTaskGetTickCount() + pdMS_TO_TICKS(CARE_VOICE_COOLDOWN_MS);
    }
}

void care_voice_prompt_init(void)
{
    if (s_voice_task_started) {
        return;
    }

    s_voice_queue = xQueueCreate(CARE_VOICE_QUEUE_DEPTH, sizeof(care_voice_request_t));
    if (!s_voice_queue) {
        ESP_LOGE(TAG, "voice queue create failed");
        return;
    }

    BaseType_t created = xTaskCreate(care_voice_task,
                                    "care_voice",
                                    CARE_VOICE_TASK_STACK_SIZE,
                                    NULL,
                                    CARE_VOICE_TASK_PRIORITY,
                                    NULL);
    if (created != pdPASS) {
        ESP_LOGE(TAG, "voice task create failed");
        vQueueDelete(s_voice_queue);
        s_voice_queue = NULL;
        return;
    }

    s_voice_task_started = true;
}

static bool care_voice_prompt_is_valid(care_voice_prompt_t prompt)
{
    return (int)prompt >= 0 && prompt < (int)(sizeof(s_voice_items) / sizeof(s_voice_items[0]));
}

static void care_voice_prompt_request_internal(care_voice_prompt_t prompt, int followup)
{
    care_voice_prompt_init();

    if (!s_voice_queue) {
        return;
    }

    if (!care_voice_prompt_is_valid(prompt)) {
        ESP_LOGW(TAG, "ignore invalid prompt request id=%d", (int)prompt);
        return;
    }

    if (followup >= 0 && !care_voice_prompt_is_valid((care_voice_prompt_t)followup)) {
        ESP_LOGW(TAG, "ignore invalid followup prompt id=%d", followup);
        followup = -1;
    }

    int priority = care_voice_prompt_priority(prompt);
    if (s_voice_playing && priority < s_active_priority) {
        ESP_LOGW(TAG, "drop lower priority prompt id=%d active_priority=%d", (int)prompt, s_active_priority);
        return;
    }

    if (s_pending_prompt >= 0) {
        care_voice_prompt_t pending = (care_voice_prompt_t)s_pending_prompt;
        if (priority < care_voice_prompt_priority(pending)) {
            ESP_LOGW(TAG, "drop lower priority prompt id=%d pending=%d", (int)prompt, s_pending_prompt);
            return;
        }
    }

    care_voice_request_t request = {
        .prompt = prompt,
        .followup = followup,
    };

    if (xQueueOverwrite(s_voice_queue, &request) == pdTRUE) {
        s_pending_prompt = (int)prompt;
    } else {
        ESP_LOGW(TAG, "voice queue overwrite failed, prompt id=%d", (int)prompt);
    }
}

void care_voice_prompt_request(care_voice_prompt_t prompt)
{
    care_voice_prompt_request_internal(prompt, -1);
}

void care_voice_prompt_request_with_followup(care_voice_prompt_t prompt, care_voice_prompt_t followup)
{
    care_voice_prompt_request_internal(prompt, (int)followup);
}

void care_voice_prompt_play(care_voice_prompt_t prompt)
{
    care_voice_prompt_request(prompt);
}

void care_voice_prompt_set_followup(care_voice_prompt_t followup)
{
    if ((int)followup < 0 || followup >= (int)(sizeof(s_voice_items) / sizeof(s_voice_items[0]))) {
        ESP_LOGW(TAG, "ignore invalid followup prompt id=%d", (int)followup);
        return;
    }
    s_followup_prompt = (int)followup;
}

bool care_voice_is_busy(void)
{
    return s_voice_playing || s_pending_prompt >= 0;
}

bool care_voice_is_in_cooldown(void)
{
    TickType_t now = xTaskGetTickCount();
    return ((int32_t)(now - s_voice_cooldown_until) < 0);
}

void wake_up_action(void)
{
    care_voice_prompt_request(CARE_VOICE_PROMPT_WAKEUP);
}

void speech_commands_action(int command_id)
{
    (void)command_id;
    care_voice_prompt_request(CARE_VOICE_PROMPT_COMMAND_RECEIVED);
}

void led_Task(void *arg)
{
    vTaskDelete(NULL);
}
