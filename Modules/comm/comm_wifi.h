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

typedef struct {
    uint32_t request_id;
    int command_type;
    int scenario;
    int value;
} CommWifi_DemoCommand_t;

typedef struct {
    uint32_t request_id;
    uint8_t risk_level;
} CommWifi_VoiceRiskCommand_t;

typedef struct {
    uint32_t seq;
    uint8_t online;
    uint8_t wifi_connected;
    uint8_t mqtt_connected;
} CommWifi_NetworkHeartbeat_t;

typedef enum {
    COMM_WIFI_COMMAND_RELAY = 0,
    COMM_WIFI_COMMAND_DEMO,
    COMM_WIFI_COMMAND_VOICE_RISK,
    COMM_WIFI_COMMAND_NETWORK_HEARTBEAT
} CommWifi_CommandType_t;

typedef struct {
    CommWifi_CommandType_t type;
    union {
        CommWifi_RelayCommand_t relay;
        CommWifi_DemoCommand_t demo;
        CommWifi_VoiceRiskCommand_t voice_risk;
        CommWifi_NetworkHeartbeat_t heartbeat;
    } data;
} CommWifi_Command_t;

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
 * cloud_perm_mask=COMM_WIFI_DEFAULT_CLOUD_PERM_MASK. The gas argument is
 * the MQ-2 estimated ppm value, not the raw AO millivolt value.
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
CommWifi_Result CommWifi_PollCommand(CommWifi_Command_t *cmd);

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
                                   uint32_t timestamp_ms);

CommWifi_Result CommWifi_SendRelayResult(uint32_t request_id,
                                         uint8_t relay_id,
                                         CommWifi_RelayResult_t result,
                                         CommWifi_RelayAction_t state,
                                         CommWifi_RelayReason_t reason);

void CommWifi_OnTxComplete(void);
void CommWifi_OnRxComplete(void);
void CommWifi_OnUartError(void);

#endif //STM32_CARING_SYSTEM_PROJECT_COMM_WIFI_H
