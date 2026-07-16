#include "comm_local.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "main.h"

#define COMM_LOCAL_RX_LINE_MAX_LEN 64U
#define COMM_LOCAL_RX_LINE_QUEUE_DEPTH 4U
#define COMM_LOCAL_UART_IRQn UART4_IRQn

static bool s_initialized = false;
static uint32_t s_risk_request_id = 880000UL;
static uint8_t s_rx_byte = 0U;
static char s_rx_line[COMM_LOCAL_RX_LINE_MAX_LEN];
static volatile uint16_t s_rx_line_len = 0U;
static char s_rx_line_queue[COMM_LOCAL_RX_LINE_QUEUE_DEPTH][COMM_LOCAL_RX_LINE_MAX_LEN];
static volatile uint8_t s_rx_line_head = 0U;
static volatile uint8_t s_rx_line_tail = 0U;
static volatile uint32_t s_rx_bytes = 0UL;
static volatile uint32_t s_rx_lines = 0UL;
static volatile uint32_t s_rx_queued = 0UL;
static volatile uint32_t s_rx_filtered = 0UL;
static volatile uint32_t s_rx_queue_full = 0UL;
static volatile uint32_t s_rx_drop_oldest = 0UL;
static volatile uint32_t s_rx_overflow = 0UL;
static volatile uint32_t s_rx_resynced = 0UL;
static volatile uint32_t s_rx_uart_errors = 0UL;
static volatile uint32_t s_rx_restart_fail = 0UL;
static volatile uint32_t s_rx_restart_ok = 0UL;
static volatile uint32_t s_rx_service_rearm = 0UL;
static volatile uint32_t s_last_uart_error = 0UL;
static volatile uint32_t s_parse_ok = 0UL;
static volatile uint32_t s_parse_fail = 0UL;
static volatile uint8_t s_rx_armed = 0U;
static volatile uint8_t s_rx_rearm_needed = 0U;
static volatile uint8_t s_last_rx_byte = 0U;
static char s_last_line[COMM_LOCAL_RX_LINE_MAX_LEN];
static char s_last_parsed_line[COMM_LOCAL_RX_LINE_MAX_LEN];

#ifdef HAL_UART_MODULE_ENABLED
extern UART_HandleTypeDef huart4;
#endif

static uint8_t line_queue_next(uint8_t index)
{
    index++;
    if (index >= COMM_LOCAL_RX_LINE_QUEUE_DEPTH) {
        index = 0U;
    }
    return index;
}

static uint8_t line_queue_count(void)
{
    if (s_rx_line_head >= s_rx_line_tail) {
        return (uint8_t)(s_rx_line_head - s_rx_line_tail);
    }

    return (uint8_t)(COMM_LOCAL_RX_LINE_QUEUE_DEPTH - s_rx_line_tail + s_rx_line_head);
}

static void enter_critical(void)
{
    HAL_NVIC_DisableIRQ(COMM_LOCAL_UART_IRQn);
}

static void exit_critical(void)
{
    HAL_NVIC_EnableIRQ(COMM_LOCAL_UART_IRQn);
}

static CommWifi_Result restart_rx_it(void)
{
#ifdef HAL_UART_MODULE_ENABLED
    if (HAL_UART_Receive_IT(&huart4, &s_rx_byte, 1U) != HAL_OK) {
        s_rx_armed = 0U;
        s_rx_rearm_needed = 1U;
        s_rx_restart_fail++;
        return COMM_WIFI_ERR_RX_START_FAILED;
    }

    s_rx_armed = 1U;
    s_rx_rearm_needed = 0U;
    s_rx_restart_ok++;
    return COMM_WIFI_OK;
#else
    return COMM_WIFI_ERR_NOT_INITIALIZED;
#endif
}

static bool queue_rx_line_from_isr(const char *line)
{
    uint8_t next = line_queue_next(s_rx_line_head);

    if (next == s_rx_line_tail) {
        s_rx_line_tail = line_queue_next(s_rx_line_tail);
        s_rx_queue_full++;
        s_rx_drop_oldest++;
        next = line_queue_next(s_rx_line_head);
    }

    (void)strncpy(s_rx_line_queue[s_rx_line_head], line, COMM_LOCAL_RX_LINE_MAX_LEN - 1U);
    s_rx_line_queue[s_rx_line_head][COMM_LOCAL_RX_LINE_MAX_LEN - 1U] = '\0';
    s_rx_line_head = next;
    s_rx_queued++;
    return true;
}

static const char *find_frame_start(const char *line)
{
    const char *risk;
    const char *relay;
    const char *demo;

    if (line == NULL) {
        return NULL;
    }

    while ((*line == ' ') || (*line == '\t')) {
        line++;
    }

    if ((line[0] == 'R') || (line[0] == 'C') || (line[0] == 'D')) {
        return line;
    }

    risk = strstr(line, "RISK:");
    relay = strstr(line, "C,");
    demo = strstr(line, "D,");

    if (risk != NULL) {
        return risk;
    }
    if (relay != NULL) {
        return relay;
    }
    if (demo != NULL) {
        return demo;
    }

    return NULL;
}

static bool pop_rx_line(char *line, size_t line_size)
{
    if (line == NULL || line_size == 0U) {
        return false;
    }

    enter_critical();

    if (s_rx_line_head == s_rx_line_tail) {
        exit_critical();
        return false;
    }

    (void)strncpy(line, s_rx_line_queue[s_rx_line_tail], line_size - 1U);
    line[line_size - 1U] = '\0';
    s_rx_line_tail = line_queue_next(s_rx_line_tail);

    exit_critical();
    return true;
}

static bool parse_relay_command(const char *line, CommWifi_RelayCommand_t *cmd)
{
    unsigned long request_id = 0UL;
    unsigned int relay_id = 0U;
    char action[8] = {0};
    char extra = '\0';
    const int fields = sscanf(line,
                              " C , %lu , %u , %7[A-Z] %c",
                              &request_id,
                              &relay_id,
                              action,
                              &extra);

    if (fields != 3 || cmd == NULL) {
        return false;
    }

    if (relay_id < 1U || relay_id > 4U) {
        return false;
    }

    if (strcmp(action, "ON") == 0) {
        cmd->action = COMM_WIFI_RELAY_ACTION_ON;
    } else if (strcmp(action, "OFF") == 0) {
        cmd->action = COMM_WIFI_RELAY_ACTION_OFF;
    } else {
        return false;
    }

    cmd->request_id = (uint32_t)request_id;
    cmd->relay_id = (uint8_t)relay_id;
    return true;
}

static bool parse_demo_command(const char *line, CommWifi_DemoCommand_t *cmd)
{
    unsigned long request_id = 0UL;
    int command_type = 0;
    int scenario = 0;
    int value = 0;
    char extra = '\0';
    const int fields = sscanf(line,
                              " D , %lu , %d , %d , %d %c",
                              &request_id,
                              &command_type,
                              &scenario,
                              &value,
                              &extra);

    if (fields != 4 || cmd == NULL) {
        return false;
    }

    if (command_type < 1 || command_type > 6 || scenario < 0 || scenario > 4) {
        return false;
    }

    cmd->request_id = (uint32_t)request_id;
    cmd->command_type = command_type;
    cmd->scenario = scenario;
    cmd->value = value;
    return true;
}

static bool parse_risk_command(const char *line, CommWifi_VoiceRiskCommand_t *cmd)
{
    int risk_level = -1;
    char extra = '\0';
    const int fields = sscanf(line, " RISK : %d %c", &risk_level, &extra);

    if (fields != 1 || cmd == NULL) {
        return false;
    }

    if (risk_level < 0 || risk_level > 3) {
        return false;
    }

    cmd->request_id = s_risk_request_id++;
    cmd->risk_level = (uint8_t)risk_level;
    return true;
}

CommWifi_Result CommLocal_Init(void)
{
    s_rx_line_len = 0U;
    s_rx_line_head = 0U;
    s_rx_line_tail = 0U;
    s_rx_byte = 0U;
    s_rx_armed = 0U;
    s_rx_rearm_needed = 0U;

    s_initialized = true;

    return restart_rx_it();
}

void CommLocal_Service(void)
{
#ifdef HAL_UART_MODULE_ENABLED
    if (!s_initialized) {
        return;
    }

    if ((s_rx_rearm_needed == 0U) && (s_rx_armed != 0U)) {
        return;
    }

    s_rx_service_rearm++;
    (void)HAL_UART_AbortReceive(&huart4);
    (void)restart_rx_it();
#endif
}

CommWifi_Result CommLocal_PollCommand(CommWifi_Command_t *cmd)
{
    char line[COMM_LOCAL_RX_LINE_MAX_LEN];

    if (!s_initialized) {
        return COMM_WIFI_ERR_NOT_INITIALIZED;
    }

    if (cmd == NULL) {
        return COMM_WIFI_ERR_INVALID_ARG;
    }

    while (pop_rx_line(line, sizeof(line))) {
        (void)strncpy(s_last_parsed_line, line, sizeof(s_last_parsed_line) - 1U);
        s_last_parsed_line[sizeof(s_last_parsed_line) - 1U] = '\0';

        if (parse_relay_command(line, &cmd->data.relay)) {
            cmd->type = COMM_WIFI_COMMAND_RELAY;
            s_parse_ok++;
            return COMM_WIFI_OK;
        }

        if (parse_demo_command(line, &cmd->data.demo)) {
            cmd->type = COMM_WIFI_COMMAND_DEMO;
            s_parse_ok++;
            return COMM_WIFI_OK;
        }

        if (parse_risk_command(line, &cmd->data.voice_risk)) {
            cmd->type = COMM_WIFI_COMMAND_VOICE_RISK;
            s_parse_ok++;
            return COMM_WIFI_OK;
        }

        s_parse_fail++;
    }

    return COMM_WIFI_ERR_NO_DATA;
}

void CommLocal_OnRxComplete(void)
{
    if (!s_initialized) {
        return;
    }

    s_rx_armed = 0U;
    const char c = (char)s_rx_byte;
    s_rx_bytes++;
    s_last_rx_byte = s_rx_byte;

    if (c == '\r') {
        (void)restart_rx_it();
        return;
    }

    if (c == '\n') {
        if (s_rx_line_len > 0U) {
            const char *frame_start;
            s_rx_line[s_rx_line_len] = '\0';
            s_rx_lines++;
            (void)strncpy(s_last_line, s_rx_line, sizeof(s_last_line) - 1U);
            s_last_line[sizeof(s_last_line) - 1U] = '\0';

            frame_start = find_frame_start(s_rx_line);
            if (frame_start != NULL) {
                if (frame_start != s_rx_line) {
                    s_rx_resynced++;
                }
                (void)queue_rx_line_from_isr(frame_start);
            } else {
                s_rx_filtered++;
            }
            s_rx_line_len = 0U;
        }
        (void)restart_rx_it();
        return;
    }

    if (s_rx_line_len + 1U >= COMM_LOCAL_RX_LINE_MAX_LEN) {
        s_rx_line_len = 0U;
        s_rx_overflow++;
        (void)restart_rx_it();
        return;
    }

    s_rx_line[s_rx_line_len] = c;
    s_rx_line_len++;
    (void)restart_rx_it();
}

void CommLocal_OnUartError(void)
{
    s_rx_uart_errors++;
    s_rx_armed = 0U;
    s_rx_rearm_needed = 1U;
#ifdef HAL_UART_MODULE_ENABLED
    s_last_uart_error = huart4.ErrorCode;
#endif
    s_rx_line_len = 0U;
}

void CommLocal_GetDiagnostics(CommLocal_Diagnostics_t *diag)
{
    if (diag == NULL) {
        return;
    }

    enter_critical();

    diag->rx_bytes = s_rx_bytes;
    diag->rx_lines = s_rx_lines;
    diag->rx_queued = s_rx_queued;
    diag->rx_filtered = s_rx_filtered;
    diag->rx_queue_full = s_rx_queue_full;
    diag->rx_drop_oldest = s_rx_drop_oldest;
    diag->rx_overflow = s_rx_overflow;
    diag->rx_resynced = s_rx_resynced;
    diag->rx_uart_errors = s_rx_uart_errors;
    diag->rx_restart_fail = s_rx_restart_fail;
    diag->rx_restart_ok = s_rx_restart_ok;
    diag->rx_service_rearm = s_rx_service_rearm;
    diag->last_uart_error = s_last_uart_error;
    diag->parse_ok = s_parse_ok;
    diag->parse_fail = s_parse_fail;
    diag->current_line_len = s_rx_line_len;
    diag->queued_lines = line_queue_count();
    diag->rx_armed = s_rx_armed;
    diag->rearm_needed = s_rx_rearm_needed;
    diag->last_rx_byte = s_last_rx_byte;
    (void)strncpy(diag->last_line, s_last_line, sizeof(diag->last_line) - 1U);
    diag->last_line[sizeof(diag->last_line) - 1U] = '\0';
    (void)strncpy(diag->last_parsed_line, s_last_parsed_line, sizeof(diag->last_parsed_line) - 1U);
    diag->last_parsed_line[sizeof(diag->last_parsed_line) - 1U] = '\0';
    if (s_rx_line_len < sizeof(diag->current_line)) {
        (void)memcpy(diag->current_line, s_rx_line, s_rx_line_len);
        diag->current_line[s_rx_line_len] = '\0';
    } else {
        (void)memcpy(diag->current_line, s_rx_line, sizeof(diag->current_line) - 1U);
        diag->current_line[sizeof(diag->current_line) - 1U] = '\0';
    }

    exit_critical();
}
