#include "comm_wifi.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "main.h"

#define COMM_WIFI_TX_RING_SIZE 256U
#define COMM_WIFI_FRAME_MAX_LEN 128U
#define COMM_WIFI_RX_LINE_MAX_LEN 64U
#define COMM_WIFI_RX_LINE_QUEUE_DEPTH 4U
#define COMM_WIFI_TX_DMA_IRQn GPDMA1_Channel0_IRQn
#define COMM_WIFI_UART_IRQn USART2_IRQn

static bool is_initialized = false;
static uint8_t tx_ring[COMM_WIFI_TX_RING_SIZE];
static volatile uint16_t tx_head = 0U;
static volatile uint16_t tx_tail = 0U;
static volatile uint16_t tx_dma_len = 0U;
static volatile bool tx_dma_busy = false;
static uint32_t tx_seq = 0U;

static uint8_t rx_byte = 0U;
static char rx_line[COMM_WIFI_RX_LINE_MAX_LEN];
static volatile uint16_t rx_line_len = 0U;
static char rx_line_queue[COMM_WIFI_RX_LINE_QUEUE_DEPTH][COMM_WIFI_RX_LINE_MAX_LEN];
static volatile uint8_t rx_line_head = 0U;
static volatile uint8_t rx_line_tail = 0U;

#ifdef HAL_UART_MODULE_ENABLED
extern UART_HandleTypeDef huart2;
#endif

static uint16_t ring_next(uint16_t index)
{
    index++;
    if (index >= COMM_WIFI_TX_RING_SIZE) {
        index = 0U;
    }
    return index;
}

static uint8_t line_queue_next(uint8_t index)
{
    index++;
    if (index >= COMM_WIFI_RX_LINE_QUEUE_DEPTH) {
        index = 0U;
    }
    return index;
}

static uint16_t ring_used(void)
{
    if (tx_head >= tx_tail) {
        return (uint16_t)(tx_head - tx_tail);
    }

    return (uint16_t)(COMM_WIFI_TX_RING_SIZE - tx_tail + tx_head);
}

static uint16_t ring_free(void)
{
    return (uint16_t)((COMM_WIFI_TX_RING_SIZE - 1U) - ring_used());
}

static bool ring_write(const uint8_t *data, uint16_t len)
{
    if (data == NULL || len > ring_free()) {
        return false;
    }

    for (uint16_t i = 0U; i < len; i++) {
        tx_ring[tx_head] = data[i];
        tx_head = ring_next(tx_head);
    }

    return true;
}

static void enter_critical(void)
{
    HAL_NVIC_DisableIRQ(COMM_WIFI_TX_DMA_IRQn);
    HAL_NVIC_DisableIRQ(COMM_WIFI_UART_IRQn);
}

static void exit_critical(void)
{
    HAL_NVIC_EnableIRQ(COMM_WIFI_UART_IRQn);
    HAL_NVIC_EnableIRQ(COMM_WIFI_TX_DMA_IRQn);
}

static CommWifi_Result start_next_tx_if_idle(void)
{
    if (!is_initialized) {
        return COMM_WIFI_ERR_NOT_INITIALIZED;
    }

    if (tx_dma_busy) {
        return COMM_WIFI_OK;
    }

    if (tx_head == tx_tail) {
        return COMM_WIFI_OK;
    }

    uint16_t len;
    if (tx_head > tx_tail) {
        len = (uint16_t)(tx_head - tx_tail);
    } else {
        len = (uint16_t)(COMM_WIFI_TX_RING_SIZE - tx_tail);
    }

    tx_dma_len = len;
    tx_dma_busy = true;

#ifdef HAL_UART_MODULE_ENABLED
    if (HAL_UART_Transmit_DMA(&huart2, &tx_ring[tx_tail], len) != HAL_OK) {
        tx_dma_busy = false;
        tx_dma_len = 0U;
        return COMM_WIFI_ERR_TX_START_FAILED;
    }
#else
    tx_dma_busy = false;
    tx_dma_len = 0U;
    return COMM_WIFI_ERR_NOT_INITIALIZED;
#endif

    return COMM_WIFI_OK;
}

static CommWifi_Result enqueue_frame(const char *frame, int len)
{
    CommWifi_Result result = COMM_WIFI_OK;

    if (frame == NULL || len <= 0) {
        return COMM_WIFI_ERR_INVALID_ARG;
    }

    enter_critical();

    if (!ring_write((const uint8_t *)frame, (uint16_t)len)) {
        result = COMM_WIFI_ERR_TX_QUEUE_FULL;
    } else {
        result = start_next_tx_if_idle();
    }

    exit_critical();

    return result;
}

static CommWifi_Result restart_rx_it(void)
{
#ifdef HAL_UART_MODULE_ENABLED
    if (HAL_UART_Receive_IT(&huart2, &rx_byte, 1U) != HAL_OK) {
        return COMM_WIFI_ERR_RX_START_FAILED;
    }

    return COMM_WIFI_OK;
#else
    return COMM_WIFI_ERR_NOT_INITIALIZED;
#endif
}

static bool queue_rx_line_from_isr(const char *line)
{
    const uint8_t next = line_queue_next(rx_line_head);

    if (next == rx_line_tail) {
        return false;
    }

    (void)strncpy(rx_line_queue[rx_line_head], line, COMM_WIFI_RX_LINE_MAX_LEN - 1U);
    rx_line_queue[rx_line_head][COMM_WIFI_RX_LINE_MAX_LEN - 1U] = '\0';
    rx_line_head = next;
    return true;
}

static bool pop_rx_line(char *line, size_t line_size)
{
    if (line == NULL || line_size == 0U) {
        return false;
    }

    enter_critical();

    if (rx_line_head == rx_line_tail) {
        exit_critical();
        return false;
    }

    (void)strncpy(line, rx_line_queue[rx_line_tail], line_size - 1U);
    line[line_size - 1U] = '\0';
    rx_line_tail = line_queue_next(rx_line_tail);

    exit_critical();
    return true;
}

static const char *relay_action_text(CommWifi_RelayAction_t action)
{
    return (action == COMM_WIFI_RELAY_ACTION_ON) ? "ON" : "OFF";
}

static const char *relay_result_text(CommWifi_RelayResult_t result)
{
    switch (result) {
        case COMM_WIFI_RELAY_RESULT_OK:
            return "OK";
        case COMM_WIFI_RELAY_RESULT_DENY:
            return "DENY";
        case COMM_WIFI_RELAY_RESULT_ERR:
        default:
            return "ERR";
    }
}

static const char *relay_reason_text(CommWifi_RelayReason_t reason)
{
    switch (reason) {
        case COMM_WIFI_RELAY_REASON_NONE:
            return "none";
        case COMM_WIFI_RELAY_REASON_CLOUD_DISABLED:
            return "cloud_disabled";
        case COMM_WIFI_RELAY_REASON_INVALID_ID:
            return "invalid_id";
        case COMM_WIFI_RELAY_REASON_INVALID_ACTION:
            return "invalid_action";
        case COMM_WIFI_RELAY_REASON_HARDWARE_FAULT:
            return "hardware_fault";
        case COMM_WIFI_RELAY_REASON_BUSY:
        default:
            return "busy";
    }
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

    if (command_type < 1 || command_type > 5 || scenario < 0 || scenario > 3) {
        return false;
    }

    cmd->request_id = (uint32_t)request_id;
    cmd->command_type = command_type;
    cmd->scenario = scenario;
    cmd->value = value;
    return true;
}

CommWifi_Result CommWifi_Init(void)
{
    tx_head = 0U;
    tx_tail = 0U;
    tx_dma_len = 0U;
    tx_dma_busy = false;
    tx_seq = 0U;

    rx_line_len = 0U;
    rx_line_head = 0U;
    rx_line_tail = 0U;
    rx_byte = 0U;

    is_initialized = true;

    return restart_rx_it();
}

CommWifi_Result CommWifi_SendStatus(float temperature,
                                    float humidity,
                                    int gas,
                                    int presence,
                                    int risk)
{
    return CommWifi_SendStatusV2(temperature,
                                 humidity,
                                 gas,
                                 presence,
                                 risk,
                                 0U,
                                 COMM_WIFI_DEFAULT_CLOUD_PERM_MASK);
}

CommWifi_Result CommWifi_SendStatusV2(float temperature,
                                      float humidity,
                                      int gas,
                                      int presence,
                                      int risk,
                                      uint8_t relay_state_mask,
                                      uint8_t cloud_perm_mask)
{
    char frame[COMM_WIFI_FRAME_MAX_LEN];

    if (!is_initialized) {
        return COMM_WIFI_ERR_NOT_INITIALIZED;
    }

    if (presence < 0 || presence > 1 || risk < 0 || risk > 3 ||
        (relay_state_mask & 0xF0U) != 0U || (cloud_perm_mask & 0xF0U) != 0U) {
        return COMM_WIFI_ERR_INVALID_ARG;
    }

    const int len = snprintf(frame,
                             sizeof(frame),
                             "S,%lu,%.1f,%.1f,%d,%d,%d,%u,%u\r\n",
                             (unsigned long)tx_seq,
                             temperature,
                             humidity,
                             gas,
                             presence,
                             risk,
                             (unsigned int)relay_state_mask,
                             (unsigned int)cloud_perm_mask);

    if (len <= 0 || len >= (int)sizeof(frame)) {
        return COMM_WIFI_ERR_FRAME_TOO_LONG;
    }

    const CommWifi_Result result = enqueue_frame(frame, len);
    if (result == COMM_WIFI_OK) {
        tx_seq++;
    }

    return result;
}

CommWifi_Result CommWifi_PollRelayCommand(CommWifi_RelayCommand_t *cmd)
{
    char line[COMM_WIFI_RX_LINE_MAX_LEN];

    if (!is_initialized) {
        return COMM_WIFI_ERR_NOT_INITIALIZED;
    }

    if (cmd == NULL) {
        return COMM_WIFI_ERR_INVALID_ARG;
    }

    while (pop_rx_line(line, sizeof(line))) {
        if (parse_relay_command(line, cmd)) {
            return COMM_WIFI_OK;
        }
    }

    return COMM_WIFI_ERR_NO_DATA;
}

CommWifi_Result CommWifi_PollCommand(CommWifi_Command_t *cmd)
{
    char line[COMM_WIFI_RX_LINE_MAX_LEN];

    if (!is_initialized) {
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

CommWifi_Result CommWifi_SendEvent(uint32_t event_id,
                                   int scenario,
                                   int event_type,
                                   int trigger_source,
                                   int state_before,
                                   int state_after,
                                   int risk,
                                   int result,
                                   int network_state,
                                   int power_state,
                                   uint32_t flags,
                                   uint32_t timestamp_ms)
{
    char frame[COMM_WIFI_FRAME_MAX_LEN];

    if (!is_initialized) {
        return COMM_WIFI_ERR_NOT_INITIALIZED;
    }

    if (scenario < 0 || scenario > 3 ||
        event_type < 0 || event_type > 10 ||
        trigger_source < 0 || trigger_source > 5 ||
        state_before < 0 || state_before > 5 ||
        state_after < 0 || state_after > 5 ||
        risk < 0 || risk > 3 ||
        result < 0 || result > 7 ||
        network_state < 0 || network_state > 2 ||
        power_state < 0 || power_state > 2) {
        return COMM_WIFI_ERR_INVALID_ARG;
    }

    const int len = snprintf(frame,
                             sizeof(frame),
                             "E,%lu,%d,%d,%d,%d,%d,%d,%d,%d,%d,%lu,%lu\r\n",
                             (unsigned long)event_id,
                             scenario,
                             event_type,
                             trigger_source,
                             state_before,
                             state_after,
                             risk,
                             result,
                             network_state,
                             power_state,
                             (unsigned long)flags,
                             (unsigned long)timestamp_ms);

    if (len <= 0 || len >= (int)sizeof(frame)) {
        return COMM_WIFI_ERR_FRAME_TOO_LONG;
    }

    return enqueue_frame(frame, len);
}

CommWifi_Result CommWifi_SendRelayResult(uint32_t request_id,
                                         uint8_t relay_id,
                                         CommWifi_RelayResult_t result,
                                         CommWifi_RelayAction_t state,
                                         CommWifi_RelayReason_t reason)
{
    char frame[COMM_WIFI_FRAME_MAX_LEN];

    if (!is_initialized) {
        return COMM_WIFI_ERR_NOT_INITIALIZED;
    }

    if (relay_id < 1U || relay_id > 4U ||
        (state != COMM_WIFI_RELAY_ACTION_OFF && state != COMM_WIFI_RELAY_ACTION_ON)) {
        return COMM_WIFI_ERR_INVALID_ARG;
    }

    const int len = snprintf(frame,
                             sizeof(frame),
                             "R,%lu,%u,%s,%s,%s\r\n",
                             (unsigned long)request_id,
                             (unsigned int)relay_id,
                             relay_result_text(result),
                             relay_action_text(state),
                             relay_reason_text(reason));

    if (len <= 0 || len >= (int)sizeof(frame)) {
        return COMM_WIFI_ERR_FRAME_TOO_LONG;
    }

    return enqueue_frame(frame, len);
}

void CommWifi_OnTxComplete(void)
{
    if (!tx_dma_busy) {
        return;
    }

    tx_tail = (uint16_t)((tx_tail + tx_dma_len) % COMM_WIFI_TX_RING_SIZE);
    tx_dma_len = 0U;
    tx_dma_busy = false;

    (void)start_next_tx_if_idle();
}

void CommWifi_OnRxComplete(void)
{
    if (!is_initialized) {
        return;
    }

    const char c = (char)rx_byte;

    if (c == '\r') {
        (void)restart_rx_it();
        return;
    }

    if (c == '\n') {
        if (rx_line_len > 0U) {
            rx_line[rx_line_len] = '\0';
            if ((rx_line[0] == 'C') || (rx_line[0] == 'D')) {
                (void)queue_rx_line_from_isr(rx_line);
            }
            rx_line_len = 0U;
        }
        (void)restart_rx_it();
        return;
    }

    if (rx_line_len + 1U >= COMM_WIFI_RX_LINE_MAX_LEN) {
        rx_line_len = 0U;
        (void)restart_rx_it();
        return;
    }

    rx_line[rx_line_len] = c;
    rx_line_len++;
    (void)restart_rx_it();
}

void CommWifi_OnUartError(void)
{
    rx_line_len = 0U;
    (void)restart_rx_it();
}
