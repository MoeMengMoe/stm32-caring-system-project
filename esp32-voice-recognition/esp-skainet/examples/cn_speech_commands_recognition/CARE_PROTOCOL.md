# Elder Care Voice Alarm Protocol

This ESP32-S3 app uses ESP-SR WakeNet9 and MultiNet7 Chinese command recognition to map elder-care phrases to risk levels.

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

The configured wake word is "小冰小冰" using the ESP-SR `wn9_xiaobinxiaobin_tts` model entry.

Runtime flow:

1. Say the configured wake word.
2. The ESP32-S3 prints `WAKEWORD DETECTED`.
3. Say one supported care command phrase.
4. If the phrase is medium risk or a cancellation phrase, the ESP32-S3 sends the final risk frame immediately.
5. If the phrase is high risk, the ESP32-S3 enters a 5-second pending alarm state.
6. During the 5-second window, "取消报警" or "我没事" cancels the alarm and sends `RISK:0`.
7. During the same window, another high-risk phrase or an explicit confirmation phrase confirms the alarm and sends `RISK:3`.
8. If there is no cancellation within 5 seconds, the ESP32-S3 confirms the alarm and sends `RISK:3`.
9. If the phrase is not mapped, it is logged and ignored.

USB serial is only for debug logs. The STM32 risk protocol is sent on UART1 GPIO17/GPIO18, not on the USB serial log port.

Mapped command USB echo format:

```text
USB_ECHO keyword="<recognized phrase>" command_id=<id> prob=<probability> risk=<level>
```

Pending high-risk alarm debug format:

```text
PENDING_ALARM keyword="<recognized phrase>" command_id=<id> prob=<probability> timeout_ms=5000
```

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

## UART Startup Self-Test

For hardware bring-up, the app currently sends a startup self-test frame after UART1 initialization:

```text
RISK:0\n
```

It is sent 3 times, once per second, starting about 1 second after boot. This is only to verify that ESP32-S3 GPIO17 reaches the STM32 UART RX pin before speech recognition is tested. Disable `CARE_UART_BOOT_TEST_ENABLED` in `main/app_care_logic.c` after the UART link is confirmed.

## Care Command Mapping

The application registers these pinyin command phrases directly with MultiNet7 at startup. It maps by `command_id` first, and keeps phrase text fallback matching for debug and compatibility.

| Command ID | Pinyin phrase | Chinese phrase | Behavior | UART frame |
| --- | --- | --- | --- | --- |
| `1001` | `jiu ming` | 救命 | Start 5-second pending high-risk alarm | delayed `RISK:3` |
| `1002` | `wo shuai dao le` | 我摔倒了 | Start 5-second pending high-risk alarm | delayed `RISK:3` |
| `1003` | `xiong kou teng` | 胸口疼 | Start 5-second pending high-risk alarm | delayed `RISK:3` |
| `1005` | `li ji bao jing` | 立即报警 | Confirm high-risk alarm immediately | `RISK:3` |
| `1006` | `que ren bao jing` | 确认报警 | Confirm high-risk alarm immediately | `RISK:3` |
| `2001` | `wo bu shu fu` | 我不舒服 | Medium-risk warning | `RISK:2` |
| `2002` | `tou yun` | 头晕 | Medium-risk warning | `RISK:2` |
| `2003` | `wo yao bang zhu` | 我要帮助 | Medium-risk warning | `RISK:2` |
| `0` | `qu xiao bao jing` | 取消报警 | Cancel pending alarm/no risk | `RISK:0` |
| `1` | `wo mei shi` | 我没事 | Cancel pending alarm/no risk | `RISK:0` |

Unmapped commands are ignored.

## Debug Log Examples

On the USB serial monitor, a successful mapped recognition should look similar to:

```text
WAKEWORD DETECTED
TOP 1, command_id: 1002, phrase_id: ..., string:wo shuai dao le prob: ...
I CARE: pending high-risk alarm: id=1002 phrase="wo shuai dao le" prob=... timeout_ms=5000
PENDING_ALARM keyword="wo shuai dao le" command_id=1002 prob=... timeout_ms=5000
I CARE: high-risk alarm confirmed: reason=timeout id=1002 phrase="wo shuai dao le" prob=...
USB_ECHO keyword="wo shuai dao le" command_id=1002 prob=... risk=3
I CARE: stm32 uart -> RISK:3
```

Cancellation within 5 seconds should look similar to:

```text
PENDING_ALARM keyword="jiu ming" command_id=1001 prob=... timeout_ms=5000
TOP 1, command_id: 0, phrase_id: ..., string:qu xiao bao jing prob: ...
I CARE: pending high-risk alarm cancelled: id=0 phrase="qu xiao bao jing" prob=...
USB_ECHO keyword="qu xiao bao jing" command_id=0 prob=... risk=0
I CARE: stm32 uart -> RISK:0
```

An unmapped command should look similar to:

```text
I CARE: command ignored: id=... phrase="..." prob=...
```
