#include "app_care_logic.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "driver/uart.h"
#include "esp_log.h"

#define CARE_UART_NUM UART_NUM_1
#define CARE_UART_TX_GPIO 17
#define CARE_UART_RX_GPIO 18
#define CARE_UART_BAUDRATE 115200

typedef enum {
    CARE_RISK_NONE = 0,
    CARE_RISK_LOW = 1,
    CARE_RISK_MEDIUM = 2,
    CARE_RISK_HIGH = 3,
    CARE_RISK_IGNORE = -1,
} care_risk_level_t;

static const char *TAG = "CARE";

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

static care_risk_level_t map_phrase_to_risk(const char *phrase)
{
    static const char *const cancel_phrases[] = {
        "guan bi dian deng",   // 关闭电灯：临时映射为取消报警
        "bang wo guan deng",   // 帮我关灯：临时映射为取消报警
    };

    static const char *const high_risk_phrases[] = {
        "da kai dian deng",    // 打开电灯：临时映射为确认报警
        "bang wo kai deng",    // 帮我开灯：临时映射为确认报警
    };

    static const char *const medium_risk_phrases[] = {
        "tai leng le",
        "tai re le",
        "you dian leng",
        "you dian re",
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

    if (phrase && strstr(phrase, "kong tiao")) {
        return CARE_RISK_LOW;
    }

    return CARE_RISK_IGNORE;
}

static void send_risk_level(care_risk_level_t level)
{
    char frame[16];
    int len = snprintf(frame, sizeof(frame), "RISK:%d\n", (int)level);
    if (len <= 0) {
        return;
    }

    uart_write_bytes(CARE_UART_NUM, frame, len);
    ESP_LOGI(TAG, "stm32 uart -> %s", frame);
}

void app_care_init(void)
{
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
}

void app_care_on_wake(void)
{
    ESP_LOGI(TAG, "wake word detected, waiting for command");
}

void app_care_on_command(int command_id, const char *phrase, float probability)
{
    care_risk_level_t risk = map_phrase_to_risk(phrase);

    if (risk == CARE_RISK_IGNORE) {
        ESP_LOGI(TAG, "command ignored: id=%d phrase=\"%s\" prob=%.3f",
                 command_id, phrase ? phrase : "", probability);
        return;
    }

    ESP_LOGI(TAG, "command mapped: id=%d phrase=\"%s\" prob=%.3f risk=%d",
             command_id, phrase ? phrase : "", probability, (int)risk);
    send_risk_level(risk);
}
