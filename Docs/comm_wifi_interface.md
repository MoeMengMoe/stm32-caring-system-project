# Comm WiFi Interface

## Boundary

`Modules/comm/comm_wifi.c` owns status frame formatting, sequence numbers, the TX ring buffer, and starting the next DMA transfer.

The main application only needs to:

- enable USART2 TX DMA in CubeMX
- call `CommWifi_Init()` after USART2/DMA init
- call `CommWifi_SendStatus(...)` from the main loop
- call `CommWifi_OnTxComplete()` from the UART TX-complete callback

## DMA interrupt contract

The module protects its ring-buffer state by temporarily masking only the TX DMA completion interrupt, not global interrupts.

Current setting:

```c
#define COMM_WIFI_TX_DMA_IRQn GPDMA1_Channel0_IRQn
```

This assumes USART2 TX DMA is assigned to `GPDMA1_Channel0_IRQn`.

If CubeMX assigns USART2 TX DMA to a different DMA channel, update `COMM_WIFI_TX_DMA_IRQn` in `Modules/comm/comm_wifi.c` to match the generated DMA IRQ.

## Protocol

STM32 sends one CSV line per status frame:

```text
seq,temperature,humidity,gas,presence,risk\r\n
```

Example:

```text
0,25.6,61.0,120,1,0\r\n
```
