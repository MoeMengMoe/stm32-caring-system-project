#ifndef STM32_CARING_SYSTEM_PROJECT_COMM_WIFI_H
#define STM32_CARING_SYSTEM_PROJECT_COMM_WIFI_H

#include <stdint.h>

typedef enum {
    COMM_WIFI_OK = 0,
    COMM_WIFI_ERR_NOT_INITIALIZED,
    COMM_WIFI_ERR_INVALID_ARG,
    COMM_WIFI_ERR_FRAME_TOO_LONG,
    COMM_WIFI_ERR_TX_QUEUE_FULL,
    COMM_WIFI_ERR_TX_START_FAILED

} CommWifi_Result;
/*
 * USART2 TX uses DMA completion as the producer/consumer boundary.
 *
 * Integration contract:
 * - Configure USART2 TX DMA in CubeMX.
 * - The current module masks GPDMA1_Channel0_IRQn during ring updates.
 * - If CubeMX assigns USART2 TX DMA to another channel, update
 *   COMM_WIFI_TX_DMA_IRQn in comm_wifi.c.
 * - Call CommWifi_OnTxComplete() from the UART TX-complete callback.
 * - Call CommWifi_SendStatus() from the main loop, not from an ISR.
 */
CommWifi_Result CommWifi_Init(void);

CommWifi_Result CommWifi_SendStatus(float temperature,
                                    float humidity,
                                    int gas,
                                    int presence,
                                    int risk);

void CommWifi_OnTxComplete(void);

#endif //STM32_CARING_SYSTEM_PROJECT_COMM_WIFI_H
