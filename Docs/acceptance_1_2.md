# 验收 1.2：真实数据全链路与 Rd-03 V2 UART

本文档用于让 Gary 在不改代码、不改 CubeMX 引脚的前提下，直接打开 CLion、完成接线与烧录，并进行两项验收：

```text
验收 1：STM32 真实传感器数据 -> ESP8266 -> MQTT -> Home Assistant
验收 2：Rd-03 V2 UART -> 有人/无人、目标距离、32 个距离门能量
```

当前 STM32 工程已包含验收所需代码。引脚与接线的唯一权威来源仍是 `Docs/pinmap.md`；本文件只摘录本次验收需要的部分。

## 1. 验收前不要做的事

- 不要在带电状态下插拔传感器或移动接线。
- 不要修改 `.ioc`，当前 CubeMX 配置已经满足验收要求。
- 不要把 MQ 的 `gas` 数值称为 ppm 或气体浓度；它是 AO 反推电压的近似 mV 值。
- 不要用明火、打火机气体或其他危险气体刺激 MQ 模块。
- 不要只按 `D35/D36` 寻找 Rd-03 接线孔；它们不会印在板子正面。

## 2. 一次性接线检查

所有模块、NUCLEO、ESP8266 和外部面包板电源必须共地。

### 2.1 BME280

```text
BME280 VCC -> NUCLEO 3V3
BME280 GND -> NUCLEO GND
BME280 SCL -> NUCLEO D15 / PB8 / I2C1_SCL
BME280 SDA -> NUCLEO D14 / PB9 / I2C1_SDA
```

### 2.2 MQ 模块 AO

```text
面包板电源 5V  -> MQ VCC
面包板电源 GND -> MQ GND
面包板电源 GND -> NUCLEO GND

MQ AO -> 2k 电阻 -> ADC 节点 -> 3.3k 电阻 -> GND
ADC 节点 -> NUCLEO A2 / PC3 / ADC1_IN4
```

### 2.3 PIR

```text
HC-SR501 PIR VCC -> 面包板电源 5V
PIR GND -> NUCLEO GND
PIR OUT -> NUCLEO A3 / PB0 / PIR_IN
```

### 2.4 Rd-03 V2

```text
Rd-03 3V3 -> NUCLEO 3V3
Rd-03 GND -> NUCLEO GND
Rd-03 OT2 -> NUCLEO A4 / PC1 / RD03_OUT
Rd-03 RX  <- NUCLEO CN10 pin 32 / 黑色排母外侧列倒数第二孔 / PB10 / USART3_TX
Rd-03 OT1 -> NUCLEO CN10 pin 34 / 黑色排母外侧列最下面孔 / PB11 / USART3_RX
```

实物定位：

- 板子正面朝上，ST-LINK USB 口在上方，USB-C 和 RESET 按钮在下方。
- `CN10` 是板子右侧下半段的黑色双排排母，`CN10` 印在它的下方。
- 使用黑色 `CN10` 排母靠板边、靠金色 ST Morpho 排针的外侧列最下面两个孔，不要插到金色 Morpho 排针上。
- 最下面孔是 `CN10 pin 34 / PB11`，上面一个孔是 `CN10 pin 32 / PB10`。
- 靠 MCU 的内侧列最下面两个孔是 `pin 33 / pin 31`，不是 `PB11/PB10`。
- 附近只能看到分组丝印 `TIMER`，板上不会印 `D35/D36`。

### 2.5 ESP8266 D1 mini

```text
面包板电源 5V  -> D1 mini 5V
面包板电源 GND -> D1 mini G
NUCLEO GND      -> D1 mini G

NUCLEO A1 / PA2 / USART2_TX -> D1 mini RX
NUCLEO A0 / PA3 / USART2_RX <- D1 mini TX
```

ESP8266 当前使用硬件 `Serial` 接收 STM32 数据。验收运行时建议先单独烧录好 ESP8266，然后断开 D1 mini 的 USB，仅使用稳定外部 `5V` 供电，避免 USB-UART 与 STM32 同时驱动 D1 mini 的 RX。

## 3. 联网链路预检

验收 1 需要 ESP8266 已烧录仓库中的当前固件，并且它的 Wi-Fi 与 MQTT 地址仍适用于现场网络：

```text
esp8266_firmware/src/main.cpp
```

确认：

- `ENABLE_FAKE_DATA = false`
- `MQTT_HOST` 指向当前可访问 Mosquitto 的地址
- Mosquitto 和 Home Assistant 已启动

服务器端常用命令：

```bash
cd server
docker compose up -d
docker ps
docker exec -it eldercare-mosquitto mosquitto_sub -t eldercare/node01/status -v
```

Home Assistant：

```text
http://<Windows 宿主机 IP>:8123
```

如果 ESP8266 固件、网络地址或服务器环境已经改变，先按 `Docs/esp8266_mqtt_quickstart.md` 恢复网关和服务器；STM32 侧不需要改代码。

## 4. 在 CLion 中构建并烧录 STM32

1. 打开目录：

```text
C:\Users\0lour\Documents\embeded_competition
```

2. 等待 CLion 加载 CMake，选择 `Debug` preset。
3. 构建目标：

```text
stm32-caring-system-project
```

4. 使用 ST-LINK USB 连接 NUCLEO，选择现有 STM32 烧录配置并运行。
5. 打开 ST-LINK 虚拟串口 `COM6`，参数为：

```text
115200 baud
8 data bits
1 stop bit
No parity
No flow control
```

6. 开始保存串口日志后，按一次 NUCLEO `RESET`，确保日志包含启动阶段和雷达模式配置阶段。

正常启动时应看到：

```text
[INFO] system boot
[INFO] comm wifi init ok
[INFO] sensor mvp init
[INFO] bme280 ready id=0x60 addr=0x76
[INFO] adc1 calibration ok
[INFO] rd03 config ack open=1 report=1 close=1 init_rx=...
```

`open/report/close` 分别表示雷达确认进入命令模式、切换到上报模式、退出命令模式。三项都为 `1`，才证明 STM32 到雷达的 TX 线路和雷达到 STM32 的 RX 线路都真正工作。

板载状态 LED 应持续闪烁。

## 5. 验收 2：Rd-03 V2 UART

### 5.1 成功日志

每 2 秒应看到：

```text
[RADAR] valid=1 frames=123 presence=1 distance_cm=85 peak_gate=8 peak_gate_cm=80 peak_energy=4567
[RADAR_E] g00=... g01=... g02=... g03=... g04=... g05=... g06=... g07=...
[RADAR_E] g08=... g09=... g10=... g11=... g12=... g13=... g14=... g15=...
[RADAR_E] g16=... g17=... g18=... g19=... g20=... g21=... g22=... g23=...
[RADAR_E] g24=... g25=... g26=... g27=... g28=... g29=... g30=... g31=...
```

验收动作：

1. 静止观察 5 秒，确认 `frames` 持续递增。
2. 人从较远处走近雷达，确认 `distance_cm` 总体减小。
3. 人从雷达前离开，确认 `presence` 最终变为 `0`。雷达可能受“目标消失延迟”影响，不会立即变为无人。
4. 在不同距离轻微移动，确认部分 `[RADAR_E]` 距离门能量发生变化。
5. 对照 `[DETECT]` 日志，确认 `rd03_ot2` 与 `radar_presence` 大体一致。

通过判据：

- `[RADAR] valid=1`
- `frames` 持续递增
- `distance_cm` 随人员距离变化
- 32 个距离门均有日志，且能量不是永久全零

### 5.2 失败日志与排查

如果持续看到：

```text
[RADAR] valid=0 rx_bytes=0 init_rx=0 ack=0/0/0 header_sync=0 bad_len=0 bad_footer=0 uart_err=0 last_err=0x0
```

先根据计数判断：

- `init_rx=0` 且 `rx_bytes=0`：STM32 从启动到运行阶段都没有收到任何雷达串口字节，优先检查 `OT1 -> PB11`、物理孔位、供电和共地。
- `ack=1/1/1`：三条配置命令均被雷达确认，双向串口链路已成立。
- `ack=0/0/0`：没有确认任何配置命令。若 `init_rx` 也为 `0`，雷达 TX 到 STM32 RX 的链路未成立；若 `init_rx` 大于 `0`，再检查 STM32 TX 到雷达 RX。
- `rx_bytes` 持续增加但 `header_sync=0`：STM32 能收到字节，但雷达可能仍在运行模式，优先检查 `PB10 -> RX` 配置命令链路。
- `header_sync` 增加但 `frames` 始终为 0：数据帧被破坏或解析格式不匹配，检查串口参数、供电和线路质量。
- `bad_len` 或 `bad_footer` 持续增加：存在丢字节、错误字节或协议不匹配。
- `uart_err` 持续增加：USART3 外设遇到非超时错误，记录完整日志后检查电平、接触和串口配置。

随后按顺序检查：

1. Rd-03 使用 `3.3V` 供电并与 NUCLEO 共地。
2. Rd-03 `OT1` 是否接到 `CN10 pin 34 / PB11 / USART3_RX`。
3. Rd-03 `RX` 是否接到 `CN10 pin 32 / PB10 / USART3_TX`。
4. 是否误接到靠 MCU 的 CN10 内侧列，或误接到金色 ST Morpho 排针。
5. 是否使用 `115200 8N1`。
6. 雷达固件是否为 Simon 提供资料对应的 Rd-03 V2 版本。

## 6. 验收 1：真实传感器数据到 Home Assistant

STM32 每 2 秒应输出一条：

```text
[INFO] status tx temp=25.6 hum=61.0 gas_ppm_est=1 gas_mv=1235 presence=1 risk=1 state=NORMAL relay=0 env_valid=1 gas_valid=1
```

同时 Mosquitto 订阅端应看到：

```text
eldercare/node01/status {"node_id":"node01","seq":...,"temperature":...,"humidity":...,"gas":...,"presence":...,"risk":...,"event":"..."}
```

Home Assistant 中应能看到以下实体更新：

```text
Node01 Temperature
Node01 Humidity
Node01 Gas
Node01 Presence
Node01 Risk
Node01 Event
```

验收动作：

1. 用手靠近 BME280，等待温度或湿度产生可见变化。
2. 在 PIR 和 Rd-03 探测区域内移动，再离开探测区域，观察 `presence` 变化。
3. 观察 MQ 的 `gas` 是否为稳定的 ppm 估算值；本次不进行危险气体刺激测试。
4. 对照 COM6 的 `[INFO] status tx`、Mosquitto JSON 和 Home Assistant 实体，确认同一字段数值能够逐级传递。

通过判据：

- COM6 中 `env_valid=1` 且 `gas_valid=1`
- `status tx` 每约 2 秒持续出现
- MQTT JSON 的 `seq` 持续递增
- MQTT JSON 与最近一条 `status tx` 的温度、湿度、gas ppm 估算值、presence、risk 一致
- Home Assistant 实体在数秒内跟随真实传感器变化

## 7. 验收记录

### 7.1 当前状态

| 验收项 | 状态 | 证据 |
| --- | --- | --- |
| 验收 1：真实传感器数据到 Home Assistant | 待完成 | 仍需保留 Mosquitto 订阅到的真实 JSON 和 Home Assistant 实体变化截图 |
| 验收 2：Rd-03 V2 UART | 已通过 | `COM6-115200.log` 中有 24 条 `valid=1` 雷达摘要、持续递增的帧计数、变化的距离以及覆盖 `g00~g31` 的能量日志 |

验收 2 的日志时间为 `2026-06-04 14:03:02`，早于最新 ACK 诊断固件。后续再保留一次包含以下启动行的短日志，作为双向命令确认的补充证据：

```text
[INFO] rd03 config ack open=1 report=1 close=1 init_rx=...
```

### 7.2 需要保留的材料

验收完成后至少保留：

```text
1 条 [INFO] status tx
1 条 [RADAR]
4 条连续的 [RADAR_E]，覆盖 g00 到 g31
1 条 Mosquitto 订阅到的真实 JSON
1 张 Home Assistant 实体变化截图
```

将结果补充到 `Docs/debug_report.md`，这样“代码已实现”才能升级为“已验证产品能力”。
