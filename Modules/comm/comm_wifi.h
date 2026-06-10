#ifndef STM32_CARING_SYSTEM_PROJECT_COMM_WIFI_H
#define STM32_CARING_SYSTEM_PROJECT_COMM_WIFI_H

#include <stdint.h>

#define COMM_WIFI_DEFAULT_CLOUD_PERM_MASK 0x0FU

typedef enum {
    COMM_WIFI_OK = 0,
    COMM_WIFI_ERR_NO_DATA,
    COMM_WIFI_ERR_NOT_INITIALIZED,
    COMM_WIFI_ERR_INVALID_ARG,
    COMM_WIFI_ERR_FRAME_TOO_LONG,
    COMM_WIFI_ERR_RX_QUEUE_FULL,
    COMM_WIFI_ERR_TX_QUEUE_FULL,
    COMM_WIFI_ERR_TX_START_FAILED,
    COMM_WIFI_ERR_RX_START_FAILED
} CommWifi_Result;

typedef enum {
    COMM_WIFI_RELAY_ACTION_OFF = 0,
    COMM_WIFI_RELAY_ACTION_ON = 1
} CommWifi_RelayAction_t;

typedef enum {
    COMM_WIFI_RELAY_RESULT_OK = 0,
    COMM_WIFI_RELAY_RESULT_DENY,
    COMM_WIFI_RELAY_RESULT_ERR
} CommWifi_RelayResult_t;

typedef enum {
    COMM_WIFI_RELAY_REASON_NONE = 0,
    COMM_WIFI_RELAY_REASON_CLOUD_DISABLED,
    COMM_WIFI_RELAY_REASON_INVALID_ID,
    COMM_WIFI_RELAY_REASON_INVALID_ACTION,
    COMM_WIFI_RELAY_REASON_HARDWARE_FAULT,
    COMM_WIFI_RELAY_REASON_BUSY
} CommWifi_RelayReason_t;

typedef struct {
    uint32_t request_id;
    uint8_t relay_id;
    CommWifi_RelayAction_t action;
} CommWifi_RelayCommand_t;

/*
 * USART2 uses DMA for TX and interrupt-driven byte reception for cloud commands.
 *
 * Integration contract:
 * - Configure USART2 TX DMA in CubeMX.
 * - The current module masks GPDMA1_Channel0_IRQn and USART2_IRQn during
 *   shared queue updates.
 * - If CubeMX assigns USART2 TX DMA or the UART to another instance, update
 *   COMM_WIFI_TX_DMA_IRQn and COMM_WIFI_UART_IRQn in comm_wifi.c.
 * - Call CommWifi_OnTxComplete() from the UART TX-complete callback.
 * - Call CommWifi_OnRxComplete() from the UART RX-complete callback.
 * - Call CommWifi_OnUartError() from the UART error callback for USART2.
 * - Call CommWifi_SendStatus() from the main loop, not from an ISR.
 */
CommWifi_Result CommWifi_Init(void);

/* Compatibility wrapper. Sends Status V2 with relay_state_mask=0 and
 * cloud_perm_mask=COMM_WIFI_DEFAULT_CLOUD_PERM_MASK.
 */
CommWifi_Result CommWifi_SendStatus(float temperature,
                                    float humidity,
                                    int gas,
                                    int presence,
                                    int risk);

CommWifi_Result CommWifi_SendStatusV2(float temperature,
                                      float humidity,
                                      int gas,
                                      int presence,
                                      int risk,
                                      uint8_t relay_state_mask,
                                      uint8_t cloud_perm_mask);

CommWifi_Result CommWifi_PollRelayCommand(CommWifi_RelayCommand_t *cmd);

CommWifi_Result CommWifi_SendRelayResult(uint32_t request_id,
                                         uint8_t relay_id,
                                         CommWifi_RelayResult_t result,
                                         CommWifi_RelayAction_t state,
                                         CommWifi_RelayReason_t reason);

void CommWifi_OnTxComplete(void);
void CommWifi_OnRxComplete(void);
void CommWifi_OnUartError(void);

#endif //STM32_CARING_SYSTEM_PROJECT_COMM_WIFI_H
