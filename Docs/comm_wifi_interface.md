# Comm WiFi Interface

## Boundary

`Modules/comm/comm_wifi.c` owns status/result frame formatting, sequence numbers, the TX ring buffer, USART2 byte receive, cloud-command line buffering, and starting the next DMA transfer.

The main application only needs to:

- enable USART2 TX DMA in CubeMX
- call `CommWifi_Init()` after USART2/DMA init
- call `CommWifi_SendStatus(...)` from the main loop
- call `CommWifi_OnTxComplete()` from the UART TX-complete callback
- call `CommWifi_OnRxComplete()` from the UART RX-complete callback
- call `CommWifi_OnUartError()` from the UART error callback for USART2
- poll `CommWifi_PollRelayCommand(...)` from the main loop when relay control is integrated
- call `CommWifi_SendRelayResult(...)` after the main controller accepts, rejects, or fails a relay command

## DMA interrupt contract

The module protects its TX ring-buffer state and RX line queue by temporarily masking the TX DMA interrupt and the USART2 interrupt, not global interrupts. The HAL UART TX-complete callback may be finalized from the USART interrupt after the DMA transfer, and RX line completion also runs from the USART interrupt, so both interrupts are part of the producer/consumer boundary.

Current setting:

```c
#define COMM_WIFI_TX_DMA_IRQn GPDMA1_Channel0_IRQn
#define COMM_WIFI_UART_IRQn USART2_IRQn
```

This assumes USART2 TX DMA is assigned to `GPDMA1_Channel0_IRQn` and the gateway link uses `USART2`.

If CubeMX assigns USART2 TX DMA to a different DMA channel or changes the UART instance, update both IRQ definitions in `Modules/comm/comm_wifi.c` to match the generated IRQs.

## Protocol

STM32 sends one CSV line per status frame:

```text
S,seq,temperature,humidity,gas,presence,risk,relay_state_mask,cloud_perm_mask\r\n
```

Example:

```text
S,18,25.6,61.0,120,1,0,5,15\r\n
```

`CommWifi_SendStatus(...)` is kept as a compatibility wrapper. It sends Status V2 with `relay_state_mask=0` and `cloud_perm_mask=15`.

## Relay control interface

Relay control is implemented as a communication-layer interface. The STM32 main controller remains the final owner of relay execution. The ESP8266 gateway only forwards cloud commands and publishes states confirmed by STM32.

### Status V2 frame

```text
S,seq,temperature,humidity,gas,presence,risk,relay_state_mask,cloud_perm_mask\r\n
```

Default permission behavior:

- `cloud_perm_mask` defaults to `15`, meaning relay 1-4 are all cloud-controllable by default.
- The permission mask is reserved for later local policy work.
- Home Assistant and the server should not display or depend on `cloud_perm_mask` in the first relay-control implementation.

### Cloud command frame

ESP8266 forwards MQTT relay commands to STM32 as:

```text
C,request_id,relay_id,action\r\n
```

`relay_id` is `1-4`; `action` is `ON` or `OFF`.

### Relay result frame

STM32 reports the final execution result as:

```text
R,request_id,relay_id,result,state,reason\r\n
```

Allowed result values:

| Value | Meaning |
| --- | --- |
| `OK` | Command accepted and executed |
| `DENY` | Command rejected by STM32 policy |
| `ERR` | Command failed because of invalid input or hardware state |

Recommended reason values:

| Value | Meaning |
| --- | --- |
| `none` | No error |
| `cloud_disabled` | Cloud control is disabled for this relay |
| `invalid_id` | Relay id is outside `1-4` |
| `invalid_action` | Action is not `ON` or `OFF` |
| `hardware_fault` | Relay driver reported failure |
| `busy` | STM32 cannot execute the command now |

### Interface exposed to main control

C interface exposed to Gary's main-control layer:

```c
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
```

The communication layer does not implement relay GPIO control. The main-control layer supplies `relay_state_mask`, optionally supplies `cloud_perm_mask`, executes commands, and reports the result.
