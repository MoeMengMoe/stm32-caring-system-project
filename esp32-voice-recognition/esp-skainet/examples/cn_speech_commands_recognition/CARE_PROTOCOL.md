# Elder Care Voice Alarm Protocol

This ESP32-S3 app uses the current ESP-SR built-in Chinese command model and maps selected built-in phrases to elder-care risk levels.

## Audio Input

The current board is an ESP32-S3 N16R8 with one I2S omnidirectional microphone.

Mic wiring:

- SCK/BCLK -> GPIO5
- WS/LRCLK -> GPIO6
- SD -> GPIO7
- LR -> GND, left channel
- VDD -> 3.3V
- GND -> GND

The ESP-SR AFE task keeps reading microphone audio, then runs wake word detection and command recognition locally on the ESP32-S3.

## Trigger Flow

The current model still uses the ESP-SR built-in wake word model. The intended product wake word is "你好小智", but the first working version keeps the default wake model until the model partition is regenerated.

Runtime flow:

1. Say the configured wake word.
2. The ESP32-S3 prints `WAKEWORD DETECTED`.
3. Say one supported built-in command phrase.
4. If the recognized phrase is mapped below, the ESP32-S3 sends a risk frame to the STM32 over UART1.
5. If the phrase is not mapped, it is logged and ignored.

USB serial is only for debug logs. The STM32 risk protocol is sent on UART1 GPIO17/GPIO18, not on the USB serial log port.

## UART Link To STM32

- UART: UART1
- ESP32-S3 TX: GPIO17
- ESP32-S3 RX: GPIO18
- Baudrate: 115200
- Format: 8N1
- Level: 3.3V TTL
- Wiring:
  - ESP32-S3 GPIO17 -> STM32 UART RX
  - ESP32-S3 GPIO18 <- STM32 UART TX, optional for later ACK/debug
  - ESP32-S3 GND -> STM32 GND

## Frame Format

The ESP32-S3 sends one ASCII line when a mapped command is recognized:

```text
RISK:<level>\n
```

Risk levels:

- `RISK:0` cancel/no risk
- `RISK:1` low risk/control event
- `RISK:2` suspected risk, cloud/HA should verify
- `RISK:3` clear high-risk alarm, cloud/HA may notify relatives

No CRC is used in this first version.

Example high-risk frame:

```text
RISK:3
```

Raw ASCII bytes:

```text
52 49 53 4B 3A 33 0A
```

The STM32 side only needs to read a line ending in `\n`, check that it starts with `RISK:`, then parse the single digit after the colon.

## Temporary Built-In Phrase Mapping

These mappings are temporary because the current MultiNet7 Chinese model uses built-in commands. Later, when trained/custom command models are available, replace the phrases with direct elder-care commands such as "救命", "我摔倒了", and "我不舒服".

The application maps by `command_id` first. The built-in phrase text is kept for debugging and fallback matching.

| Command ID | Built-in phrase | Intended care meaning | UART frame |
| --- | --- | --- | --- |
| `310` | `da kai dian deng` | Confirm high-risk alarm | `RISK:3` |
| `309` | `bang wo kai deng` | Confirm high-risk alarm | `RISK:3` |
| `260` | `tai leng le` | Possible discomfort | `RISK:2` |
| `261` | `tai re le` | Possible discomfort | `RISK:2` |
| `283` | `you dian leng` | Possible discomfort | `RISK:2` |
| `284` | `you dian re` | Possible discomfort | `RISK:2` |
| `216`, `220`, `230`, `234`, `242` | air-conditioner on phrases | Low-risk/control event | `RISK:1` |
| `183`, `202`, `231`, `232`, `233` | air-conditioner off phrases | Low-risk/control event | `RISK:1` |
| `311` | `guan bi dian deng` | Cancel alarm | `RISK:0` |
| `308` | `bang wo guan deng` | Cancel alarm | `RISK:0` |

Unmapped commands are ignored.

## Debug Log Examples

On the USB serial monitor, a successful mapped recognition should look similar to:

```text
WAKEWORD DETECTED
TOP 1, command_id: ..., phrase_id: ..., string:da kai dian deng prob: ...
I CARE: command mapped: id=... phrase="da kai dian deng" prob=... risk=3
I CARE: stm32 uart -> RISK:3
```

An unmapped command should look similar to:

```text
I CARE: command ignored: id=... phrase="..." prob=...
```
