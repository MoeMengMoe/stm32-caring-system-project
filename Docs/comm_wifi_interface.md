# Comm WiFi Interface

## Boundary

`Modules/comm/comm_wifi.c` owns status frame formatting, sequence numbers, the TX ring buffer, and starting the next DMA transfer.

The main application only needs to:

- enable USART2 TX DMA in CubeMX
- call `CommWifi_Init()` after USART2/DMA init
- call `CommWifi_SendStatus(...)` from the main loop
- call `CommWifi_OnTxComplete()` from the UART TX-complete callback

## DMA interrupt contract

The module protects its ring-buffer state by temporarily masking the TX DMA interrupt and the USART2 interrupt, not global interrupts. The HAL UART TX-complete callback may be finalized from the USART interrupt after the DMA transfer, so both interrupts are part of the producer/consumer boundary.

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
seq,temperature,humidity,gas,presence,risk\r\n
```

Example:

```text
0,25.6,61.0,120,1,0\r\n
```
