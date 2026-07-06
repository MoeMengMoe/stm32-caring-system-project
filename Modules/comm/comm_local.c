#include "comm_local.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "main.h"

#define COMM_LOCAL_RX_LINE_MAX_LEN 64U
#define COMM_LOCAL_RX_LINE_QUEUE_DEPTH 4U
#define COMM_LOCAL_UART_IRQn UART4_IRQn

static bool s_initialized = false;
static uint8_t s_rx_byte = 0U;
static char s_rx_line[COMM_LOCAL_RX_LINE_MAX_LEN];
static volatile uint16_t s_rx_line_len = 0U;
static char s_rx_line_queue[COMM_LOCAL_RX_LINE_QUEUE_DEPTH][COMM_LOCAL_RX_LINE_MAX_LEN];
static volatile uint8_t s_rx_line_head = 0U;
static volatile uint8_t s_rx_line_tail = 0U;

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
        return COMM_WIFI_ERR_RX_START_FAILED;
    }

    return COMM_WIFI_OK;
#else
    return COMM_WIFI_ERR_NOT_INITIALIZED;
#endif
}

static bool queue_rx_line_from_isr(const char *line)
{
    const uint8_t next = line_queue_next(s_rx_line_head);

    if (next == s_rx_line_tail) {
        return false;
    }

    (void)strncpy(s_rx_line_queue[s_rx_line_head], line, COMM_LOCAL_RX_LINE_MAX_LEN - 1U);
    s_rx_line_queue[s_rx_line_head][COMM_LOCAL_RX_LINE_MAX_LEN - 1U] = '\0';
    s_rx_line_head = next;
    return true;
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

    if (command_type < 1 || command_type > 5 || scenario < 0 || scenario > 4) {
        return false;
    }

    cmd->request_id = (uint32_t)request_id;
    cmd->command_type = command_type;
    cmd->scenario = scenario;
    cmd->value = value;
    return true;
}

CommWifi_Result CommLocal_Init(void)
{
    s_rx_line_len = 0U;
    s_rx_line_head = 0U;
    s_rx_line_tail = 0U;
    s_rx_byte = 0U;

    s_initialized = true;

    return restart_rx_it();
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
        if (parse_relay_command(line, &cmd->data.relay)) {
            cmd->type = COMM_WIFI_COMMAND_RELAY;
            return COMM_WIFI_OK;
        }

        if (parse_demo_command(line, &cmd->data.demo)) {
            cmd->type = COMM_WIFI_COMMAND_DEMO;
            return COMM_WIFI_OK;
        }
    }

    return COMM_WIFI_ERR_NO_DATA;
}

void CommLocal_OnRxComplete(void)
{
    if (!s_initialized) {
        return;
    }

    const char c = (char)s_rx_byte;

    if (c == '\r') {
        (void)restart_rx_it();
        return;
    }

    if (c == '\n') {
        if (s_rx_line_len > 0U) {
            s_rx_line[s_rx_line_len] = '\0';
            if ((s_rx_line[0] == 'C') || (s_rx_line[0] == 'D')) {
                (void)queue_rx_line_from_isr(s_rx_line);
            }
            s_rx_line_len = 0U;
        }
        (void)restart_rx_it();
        return;
    }

    if (s_rx_line_len + 1U >= COMM_LOCAL_RX_LINE_MAX_LEN) {
        s_rx_line_len = 0U;
        (void)restart_rx_it();
        return;
    }

    s_rx_line[s_rx_line_len] = c;
    s_rx_line_len++;
    (void)restart_rx_it();
}

void CommLocal_OnUartError(void)
{
    s_rx_line_len = 0U;
    (void)restart_rx_it();
}
