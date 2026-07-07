#include "app_care_logic.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "speech_commands_action.h"

#define CARE_UART_NUM UART_NUM_1
#define CARE_UART_TX_GPIO 17
#define CARE_UART_RX_GPIO 18
#define CARE_UART_BAUDRATE 115200
#define CARE_UART_BOOT_TEST_ENABLED 1
#define CARE_UART_BOOT_TEST_COUNT 3
#define CARE_UART_BOOT_TEST_INTERVAL_MS 1000
#define CARE_UART_BOOT_TEST_STACK_SIZE 4096
#define CARE_ALARM_CONFIRM_TIMEOUT_MS 5000

#define CMD_QU_XIAO_BAO_JING 0
#define CMD_WO_MEI_SHI 1
#define CMD_JIU_MING 1001
#define CMD_WO_SHUAI_DAO_LE 1002
#define CMD_XIONG_KOU_TENG 1003
#define CMD_LI_JI_BAO_JING 1005
#define CMD_QUE_REN_BAO_JING 1006
#define CMD_WO_BU_SHU_FU 2001
#define CMD_TOU_YUN 2002
#define CMD_WO_YAO_BANG_ZHU 2003

typedef enum {
    CARE_RISK_NONE = 0,
    CARE_RISK_LOW = 1,
    CARE_RISK_MEDIUM = 2,
    CARE_RISK_HIGH = 3,
    CARE_RISK_IGNORE = -1,
} care_risk_level_t;

static const char *TAG = "CARE";
static bool pending_alarm = false;
static TickType_t pending_alarm_deadline = 0;
static int pending_alarm_command_id = -1;
static float pending_alarm_probability = 0.0f;
static char pending_alarm_phrase[64];

static bool phrase_is_any(const char *phrase, const char *const *items, int count)
{
    if (!phrase) {
        return false;
    }

    for (int i = 0; i < count; i++) {
        if (strcmp(phrase, items[i]) == 0) {
            return true;
        }
    }
    return false;
}

static bool command_id_is_any(int command_id, const int *items, int count)
{
    for (int i = 0; i < count; i++) {
        if (command_id == items[i]) {
            return true;
        }
    }
    return false;
}

static care_risk_level_t map_command_to_risk(int command_id, const char *phrase)
{
    static const int cancel_commands[] = {
        CMD_QU_XIAO_BAO_JING,
        CMD_WO_MEI_SHI,
    };

    static const int high_risk_commands[] = {
        CMD_JIU_MING,
        CMD_WO_SHUAI_DAO_LE,
        CMD_XIONG_KOU_TENG,
        CMD_LI_JI_BAO_JING,
        CMD_QUE_REN_BAO_JING,
    };

    static const int medium_risk_commands[] = {
        CMD_WO_BU_SHU_FU,
        CMD_TOU_YUN,
        CMD_WO_YAO_BANG_ZHU,
    };

    if (command_id_is_any(command_id, cancel_commands, sizeof(cancel_commands) / sizeof(cancel_commands[0]))) {
        return CARE_RISK_NONE;
    }

    if (command_id_is_any(command_id, high_risk_commands, sizeof(high_risk_commands) / sizeof(high_risk_commands[0]))) {
        return CARE_RISK_HIGH;
    }

    if (command_id_is_any(command_id, medium_risk_commands, sizeof(medium_risk_commands) / sizeof(medium_risk_commands[0]))) {
        return CARE_RISK_MEDIUM;
    }

    static const char *const cancel_phrases[] = {
        "qu xiao bao jing",
        "wo mei shi",
    };

    static const char *const high_risk_phrases[] = {
        "jiu ming",
        "wo shuai dao le",
        "xiong kou teng",
        "li ji bao jing",
        "que ren bao jing",
    };

    static const char *const medium_risk_phrases[] = {
        "wo bu shu fu",
        "tou yun",
        "wo yao bang zhu",
    };

    if (phrase_is_any(phrase, cancel_phrases, sizeof(cancel_phrases) / sizeof(cancel_phrases[0]))) {
        return CARE_RISK_NONE;
    }

    if (phrase_is_any(phrase, high_risk_phrases, sizeof(high_risk_phrases) / sizeof(high_risk_phrases[0]))) {
        return CARE_RISK_HIGH;
    }

    if (phrase_is_any(phrase, medium_risk_phrases, sizeof(medium_risk_phrases) / sizeof(medium_risk_phrases[0]))) {
        return CARE_RISK_MEDIUM;
    }

    return CARE_RISK_IGNORE;
}

static bool command_is_immediate_alarm(int command_id, const char *phrase)
{
    static const int confirm_commands[] = {
        CMD_LI_JI_BAO_JING,
        CMD_QUE_REN_BAO_JING,
    };

    static const char *const confirm_phrases[] = {
        "li ji bao jing",
        "que ren bao jing",
    };

    return command_id_is_any(command_id, confirm_commands, sizeof(confirm_commands) / sizeof(confirm_commands[0])) ||
           phrase_is_any(phrase, confirm_phrases, sizeof(confirm_phrases) / sizeof(confirm_phrases[0]));
}

static void send_risk_level(care_risk_level_t level)
{
    char frame[16];
    int len = snprintf(frame, sizeof(frame), "RISK:%d\n", (int)level);
    if (len <= 0) {
        return;
    }

    printf("SYS_ECHO tx=\"%.*s\"\n", len - 1, frame);
    fflush(stdout);

    int written = uart_write_bytes(CARE_UART_NUM, frame, len);
    if (written != len) {
        ESP_LOGE(TAG, "stm32 uart write failed: expected=%d written=%d", len, written);
        return;
    }

    ESP_LOGI(TAG, "stm32 uart -> RISK:%d", (int)level);
}

static void echo_risk_to_usb(int command_id, const char *phrase, float probability, care_risk_level_t level)
{
    printf("USB_ECHO keyword=\"%s\" command_id=%d prob=%.3f risk=%d\n",
           phrase ? phrase : "", command_id, probability, (int)level);
    fflush(stdout);
}

static void echo_pending_alarm_to_usb(int command_id, const char *phrase, float probability)
{
    printf("PENDING_ALARM keyword=\"%s\" command_id=%d prob=%.3f timeout_ms=%d\n",
           phrase ? phrase : "", command_id, probability, CARE_ALARM_CONFIRM_TIMEOUT_MS);
    fflush(stdout);
}

static void clear_pending_alarm(void)
{
    pending_alarm = false;
    pending_alarm_deadline = 0;
    pending_alarm_command_id = -1;
    pending_alarm_probability = 0.0f;
    pending_alarm_phrase[0] = '\0';
}

static void start_pending_alarm(int command_id, const char *phrase, float probability)
{
    pending_alarm = true;
    pending_alarm_deadline = xTaskGetTickCount() + pdMS_TO_TICKS(CARE_ALARM_CONFIRM_TIMEOUT_MS);
    pending_alarm_command_id = command_id;
    pending_alarm_probability = probability;

    snprintf(pending_alarm_phrase, sizeof(pending_alarm_phrase), "%s", phrase ? phrase : "");
    ESP_LOGI(TAG, "pending high-risk alarm: id=%d phrase=\"%s\" prob=%.3f timeout_ms=%d",
             pending_alarm_command_id, pending_alarm_phrase,
             pending_alarm_probability, CARE_ALARM_CONFIRM_TIMEOUT_MS);
    echo_pending_alarm_to_usb(command_id, phrase, probability);
}

static void raise_high_alarm(int command_id, const char *phrase, float probability, const char *reason)
{
    ESP_LOGI(TAG, "high-risk alarm confirmed: reason=%s id=%d phrase=\"%s\" prob=%.3f",
             reason, command_id, phrase ? phrase : "", probability);
    echo_risk_to_usb(command_id, phrase, probability, CARE_RISK_HIGH);

    if (reason && strcmp(reason, "timeout") == 0) {
        care_voice_prompt_request(CARE_VOICE_PROMPT_TIMEOUT_ESCALATE);
    } else {
        care_voice_prompt_request(CARE_VOICE_PROMPT_SOS_ALARM);
    }
    send_risk_level(CARE_RISK_HIGH);
}

static void confirm_pending_alarm(const char *reason)
{
    int command_id = pending_alarm_command_id;
    float probability = pending_alarm_probability;
    char phrase[sizeof(pending_alarm_phrase)];

    snprintf(phrase, sizeof(phrase), "%s", pending_alarm_phrase);
    clear_pending_alarm();

    raise_high_alarm(command_id, phrase, probability, reason);
}

static void boot_test_task(void *arg)
{
    (void)arg;

    vTaskDelay(pdMS_TO_TICKS(CARE_UART_BOOT_TEST_INTERVAL_MS));
    for (int i = 0; i < CARE_UART_BOOT_TEST_COUNT; i++) {
        ESP_LOGI(TAG, "startup uart self-test %d/%d", i + 1, CARE_UART_BOOT_TEST_COUNT);
        send_risk_level(CARE_RISK_NONE);
        vTaskDelay(pdMS_TO_TICKS(CARE_UART_BOOT_TEST_INTERVAL_MS));
    }

    vTaskDelete(NULL);
}

void app_care_init(void)
{
    care_voice_prompt_init();

    const uart_config_t uart_config = {
        .baud_rate = CARE_UART_BAUDRATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_driver_install(CARE_UART_NUM, 1024, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(CARE_UART_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(CARE_UART_NUM, CARE_UART_TX_GPIO, CARE_UART_RX_GPIO,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    ESP_LOGI(TAG, "UART%d ready: TX=GPIO%d RX=GPIO%d baud=%d",
             CARE_UART_NUM, CARE_UART_TX_GPIO, CARE_UART_RX_GPIO, CARE_UART_BAUDRATE);

#if CARE_UART_BOOT_TEST_ENABLED
    xTaskCreate(boot_test_task, "care_uart_boot_test", CARE_UART_BOOT_TEST_STACK_SIZE, NULL, 3, NULL);
#endif
}

void app_care_on_wake(void)
{
    ESP_LOGI(TAG, "wake word detected, waiting for command");
    care_voice_prompt_play(CARE_VOICE_PROMPT_WAKEUP);
}

void app_care_poll(void)
{
    if (!pending_alarm) {
        return;
    }

    TickType_t now = xTaskGetTickCount();
    if ((int32_t)(now - pending_alarm_deadline) >= 0) {
        confirm_pending_alarm("timeout");
    }
}

void app_care_on_command(int command_id, const char *phrase, float probability)
{
    care_risk_level_t risk = map_command_to_risk(command_id, phrase);

    if (risk == CARE_RISK_IGNORE) {
        ESP_LOGI(TAG, "command ignored: id=%d phrase=\"%s\" prob=%.3f",
                 command_id, phrase ? phrase : "", probability);
        return;
    }

    if (risk == CARE_RISK_NONE) {
        /*
         * Cancel: play the cancel prompt directly (it is self-explanatory,
         * no need for a preceding "command received" acknowledgement).
         */
        care_voice_prompt_request(CARE_VOICE_PROMPT_CANCEL);
        if (pending_alarm) {
            ESP_LOGI(TAG, "pending high-risk alarm cancelled: id=%d phrase=\"%s\" prob=%.3f",
                     command_id, phrase ? phrase : "", probability);
            clear_pending_alarm();
        }
        echo_risk_to_usb(command_id, phrase, probability, CARE_RISK_NONE);
        send_risk_level(CARE_RISK_NONE);
        return;
    }

    if (risk == CARE_RISK_HIGH) {
        bool immediate_alarm = command_is_immediate_alarm(command_id, phrase);
        if (pending_alarm) {
            if (immediate_alarm) {
                confirm_pending_alarm("voice-confirm");
            } else {
                ESP_LOGI(TAG, "ignore repeated high-risk command while pending: id=%d phrase=\"%s\" prob=%.3f",
                         command_id, phrase ? phrase : "", probability);
                echo_pending_alarm_to_usb(command_id, phrase, probability);
            }
            return;
        }

        if (immediate_alarm) {
            /*
             * Immediate alarm commands ("立即报警" / "确认报警"):
             * SOS_ALARM (priority 4) overwrites any queued prompt.
             */
            raise_high_alarm(command_id, phrase, probability, "immediate-command");
            return;
        }

        /*
         * High-risk pending alarm ("我摔倒了" / "救命" / etc):
         * 1. "收到" (COMMAND_RECEIVED)
         * 2. → "需要帮您报警吗？" (VERIFY, chained as follow-up)
         * Then start the 5-second confirmation window.
         */
        care_voice_prompt_request_with_followup(CARE_VOICE_PROMPT_COMMAND_RECEIVED,
                                                CARE_VOICE_PROMPT_VERIFY);
        start_pending_alarm(command_id, phrase, probability);
        return;
    }

    /*
     * Medium risk ("我头晕" / "我不舒服" / "我要帮助"):
     * 1. "收到" (COMMAND_RECEIVED)
     * 2. → "检测到异常情况，请确认是否需要帮助" (VERIFY, chained as follow-up)
     */
    ESP_LOGI(TAG, "command mapped: id=%d phrase=\"%s\" prob=%.3f risk=%d",
             command_id, phrase ? phrase : "", probability, (int)risk);
    care_voice_prompt_request_with_followup(CARE_VOICE_PROMPT_COMMAND_RECEIVED,
                                            CARE_VOICE_PROMPT_VERIFY);
    echo_risk_to_usb(command_id, phrase, probability, risk);
    send_risk_level(risk);
}
