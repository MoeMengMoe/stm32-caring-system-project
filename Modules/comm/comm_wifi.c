#include "comm_wifi.h"

#include <stdbool.h>
#include <stdio.h>

#include "main.h"

#define COMM_WIFI_TX_RING_SIZE 256U
#define COMM_WIFI_FRAME_MAX_LEN 96U
#define COMM_WIFI_TX_DMA_IRQn GPDMA1_Channel0_IRQn

static bool is_initialized = false;
static uint8_t tx_ring[COMM_WIFI_TX_RING_SIZE];
static volatile uint16_t tx_head = 0U;
static volatile uint16_t tx_tail = 0U;
static volatile uint16_t tx_dma_len = 0U;
static volatile bool tx_dma_busy = false;
static uint32_t tx_seq = 0U;

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
}

static void exit_critical(void)
{
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

CommWifi_Result CommWifi_Init(void)
{
    tx_head = 0U;
    tx_tail = 0U;
    tx_dma_len = 0U;
    tx_dma_busy = false;
    tx_seq = 0U;
    is_initialized = true;

    return COMM_WIFI_OK;
}

CommWifi_Result CommWifi_SendStatus(float temperature,
                                    float humidity,
                                    int gas,
                                    int presence,
                                    int risk)
{
    char frame[COMM_WIFI_FRAME_MAX_LEN];

    if (!is_initialized) {
        return COMM_WIFI_ERR_NOT_INITIALIZED;
    }

    if (presence < 0 || presence > 1 || risk < 0 || risk > 3) {
        return COMM_WIFI_ERR_INVALID_ARG;
    }

    const int len = snprintf(frame,
                             sizeof(frame),
                             "%lu,%.1f,%.1f,%d,%d,%d\r\n",
                             (unsigned long)tx_seq,
                             temperature,
                             humidity,
                             gas,
                             presence,
                             risk);

    if (len <= 0 || len >= (int)sizeof(frame)) {
        return COMM_WIFI_ERR_FRAME_TOO_LONG;
    }

    CommWifi_Result result = COMM_WIFI_OK;

    enter_critical();

    if (!ring_write((const uint8_t *)frame, (uint16_t)len)) {
        result = COMM_WIFI_ERR_TX_QUEUE_FULL;
    } else {
        tx_seq++;
        result = start_next_tx_if_idle();
    }

    exit_critical();

    return result;
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
