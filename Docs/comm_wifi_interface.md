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

The frozen cross-device protocol is documented in `Docs/protocol.md`. This file only describes the STM32 communication-module boundary.

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
S,18,25.6,61.0,1,1,0,5,15\r\n
```

`CommWifi_SendStatus(...)` is kept as a compatibility wrapper. It sends Status V2 with `relay_state_mask=0` and `cloud_perm_mask=15`.

For the 6.30 demo protocol, the UART frame types are frozen as:

| Frame | Direction | Purpose |
| --- | --- | --- |
| `S` | STM32 -> ESP8266 | Periodic status |
| `E` | STM32 -> ESP8266 | App event |
| `C` | ESP8266 -> STM32 | Relay command |
| `R` | STM32 -> ESP8266 | Relay result |
| `D` | ESP8266 -> STM32 | Demo/app command |
| `A` | ESP8266 -> STM32 | Status publish heartbeat acknowledgement |

All six frame types are implemented. The gateway returns `A` after every `S`
frame, so STM32 can distinguish a successful MQTT publish from UART-only
transmission.

### Heartbeat acknowledgement frame

```text
A,status_seq,online,rssi_dbm\r\n
```

- `online=1` means Wi-Fi and MQTT were connected and the matching status frame
  was accepted by the MQTT client.
- `online=0` means the gateway received the UART frame but could not publish it.
- STM32 enters local autonomy after an offline acknowledgement or after seven
  seconds without acknowledgements (following the nine-second boot grace).
- A later online acknowledgement generates the network-restored event and
  returns the state machine to online operation.
- The MQTT gateway also publishes retained `online`/`offline` values to
  `eldercare/node01/availability`; an ungraceful disconnect uses MQTT LWT.

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
- The `gas` field in the status frame is the MQ-2 estimated ppm value. STM32 keeps the raw mV chain only in local serial debug logs.

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

## Frozen extension frames

### App event frame

When integrated, STM32 app events should be sent as:

```text
E,event_id,scenario,event_type,trigger_source,state_before,state_after,risk,result,network_state,power_state,flags,timestamp_ms\r\n
```

The integer enum mapping is frozen in `Docs/protocol.md`. The ESP8266 gateway maps these codes to MQTT string fields before publishing `eldercare/node01/event`.

### Demo/app command frame

When integrated, ESP8266 should forward dashboard demo commands to STM32 as:

```text
D,request_id,command_type,scenario,value\r\n
```

This frame is for triggering scenarios, user ack, clear alarm, and network simulation during the 6.30 demo. STM32 remains the owner of local state-machine transitions and local action execution.

Gas-risk remote debug injection:

```text
D,9001,6,4,150\r\n
D,9002,6,4,350\r\n
D,9003,6,4,0\r\n
```

- `command_type=6` means `DEBUG_SET_GAS_PPM_OFFSET`.
- `scenario=4` keeps the command semantically tied to `GAS_RISK`.
- `value` is the ppm offset added to the STM32-side `gas_ppm_est`; `0` clears it.
- This is demo/debug only. It affects TFT, status frame, `AI_SAMPLE`, and the local state machine, but it does not fake ADC/mV fields from the MQ sensor.
