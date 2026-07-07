# Serial Log Analysis Guide

本文给串口日志分析机器人使用，用于分析 STM32 调试串口输出，重点判断 Rd-03 V2 雷达 UART 链路、传感器采集、TFT/HA 上报前状态是否正常。

## 1. 串口采集参数

STM32 调试串口当前使用 USART1/ST-LINK VCP。

```text
baudrate: 115200
bytesize: 8
parity: none
stopbits: 1
line ending: CRLF or LF
encoding: utf-8 / gbk fallback
```

推荐记录方式：

```powershell
python scripts/serial_log.py --port COM6 --baudrate 115200 --duration 120 --output .embeddedskills/logs/serial/COM6-115200.log
```

如果端口不确定，先执行串口扫描，再指定实际 COM 口。

## 2. 分析目标

分析日志时优先回答这些问题：

- 系统是否正常启动。
- BME280 温湿度是否持续有效。
- MQ2 ADC 是否持续有效。
- Rd-03 V2 命令模式、上报模式 ACK 是否成功。
- Rd-03 V2 UART 是否收到合法上报帧。
- 雷达 `presence / distance_cm / gate_energy` 是否随场景变化。
- 雷达特征 `zone / motion_score / occupied_s / still_s` 是否可信。
- STM32 发给 ESP8266 的状态帧是否持续输出。
- 当前 `risk` 是否仍为占位规则，而不是正式风险状态机。

## 3. 启动阶段日志

### 3.1 系统启动

正常应看到：

```text
[INFO] system boot
[INFO] comm wifi init ok
[INFO] status display ready
[INFO] sensor mvp init
```

判断：

- 缺少 `[INFO] system boot`：串口接错、波特率错误、固件未运行或 MCU 未复位。
- 出现 `[FAIL] error handler`：代码进入错误处理，应先看上一条外设初始化日志。
- `status display init failed`：TFT 初始化失败，但不直接证明雷达失败。

### 3.2 BME280 与 ADC

正常应看到：

```text
[INFO] bme280 ready id=0x60 addr=0x76
[INFO] adc1 calibration ok
```

异常判断：

- `[WARN] bme280 init failed`：I2C、供电、地址或接线问题。
- `[WARN] adc1 calibration failed`：ADC 初始化或校准失败。
- 后续长期没有 `[ENV]`：环境采集未进入周期更新。

## 4. Rd-03 V2 配置 ACK

正常应看到：

```text
[INFO] rd03 config ack open=1 report=1 close=1 init_rx=...
```

字段含义：

```text
open   打开命令模式 ACK 是否成功
report 切换上报模式 ACK 是否成功
close  关闭命令模式 ACK 是否成功
init_rx 初始化和命令阶段收到的 UART 字节数
```

判断规则：

- `open=1 report=1 close=1`：命令链路与 ACK 解析通过。
- `open=0`：雷达未正确进入命令模式，检查 PB10 -> 雷达 RX、波特率、供电、共地。
- `report=0`：上报模式设置失败，后续可能只有默认运行模式文本或无完整能量帧。
- `close=0`：可能未恢复工作模式，后续上报不稳定。
- `init_rx=0`：初始化期间完全没收到雷达回包，优先怀疑 RX/TX 接线、供电、共地。

协议依据：

```text
命令帧头: FD FC FB FA
命令帧尾: 04 03 02 01
上报模式命令: FD FC FB FA 08 00 12 00 00 00 04 00 00 00 04 03 02 01
```

## 5. Rd-03 V2 UART 上报诊断

### 5.1 无有效帧

无有效帧时会看到：

```text
[RADAR] valid=0 rx_bytes=... init_rx=... ack=.../.../... header_sync=... bad_len=... bad_footer=... uart_err=... last_err=0x...
```

字段含义：

```text
rx_bytes     运行阶段收到的雷达 UART 字节总数
init_rx      初始化阶段收到的 UART 字节总数
ack          open/report/close 三个 ACK 状态
header_sync  识别到 F4 F3 F2 F1 上报帧头的次数
bad_len      非法 payload 长度次数
bad_footer   帧尾 F8 F7 F6 F5 不匹配次数
uart_err     USART3 非 timeout 错误次数
last_err     最近一次 HAL UART ErrorCode
```

诊断规则：

- `rx_bytes=0 init_rx=0`：STM32 完全没收到雷达串口数据，优先检查 OT1 -> PB11。
- `init_rx>0 rx_bytes=0`：配置阶段有回包，但退出命令模式后没有上报，检查 `report` 和 `close` ACK。
- `rx_bytes` 增加但 `header_sync=0`：收到字节但不是上报模式二进制帧，可能仍在运行模式文本输出或协议模式不对。
- `header_sync` 增加但 `bad_len` 增加：长度字段解析不符合当前代码假设，需核对协议版本。
- `header_sync` 增加但 `bad_footer` 增加：帧尾不匹配，可能丢字节、串口干扰、轮询接收被阻塞。
- `uart_err` 增加：检查电平、接触、波特率、串口配置和 HAL 错误码。

### 5.2 有效帧

正常有效帧示例：

```text
[RADAR] valid=1 frames=123 presence=1 distance_cm=85 peak_gate=8 peak_gate_cm=80 peak_energy=4567
```

判断规则：

- `valid=1`：至少解析到一帧合法上报。
- `frames` 应持续递增。
- `presence` 应随有人/无人场景变化。
- `distance_cm` 应随人与雷达距离变化。
- `peak_gate_cm` 只是 `peak_gate * 10cm` 的近似值，用于辅助观察，不等同于严格测距。
- `peak_energy` 是相对能量，不可直接解释为风险等级。

协议依据：

```text
上报帧头: F4 F3 F2 F1
payload length: 2 bytes little-endian
payload:
  result: 1 byte
  distance_cm: 2 bytes little-endian
  gate_energy: 32 * 4 bytes little-endian
上报帧尾: F8 F7 F6 F5
```

## 6. 雷达特征层日志

特征日志示例：

```text
[RADAR_F] zone=2/ACTIVE dist_cm=120 peak_gate=12 peak_cm=120 energy=... sum=... motion=... active_gates=... occupied_s=... still_s=...
```

字段含义：

```text
zone          项目自定义一维区域，不是雷达原生字段
dist_cm       雷达上报距离
peak_gate     32 个距离门中能量最高的 gate
peak_cm       peak_gate * 10cm
energy        peak gate 能量
sum           32 个 gate 能量总和
motion        相邻采样的 gate 能量差分总和 / 1000
active_gates  高于 peak_energy/6 的 gate 数量
occupied_s    radar_presence 连续为 1 的秒数
still_s       presence=1 且 motion<=30 的连续秒数
```

当前 zone 阈值：

```text
UNKNOWN: radar_valid=0
CLEAR:   radar_presence=0
NEAR:    distance_cm < 80
ACTIVE:  distance_cm < 180
REST:    distance_cm < 300
FAR:     distance_cm >= 300
```

分析注意：

- `zone` 是软件层规则，后续需要按实际安装位置重新标定。
- `motion` 是项目自定义特征，不能当作雷达官方运动量。
- `still_s` 对轮询丢帧、屏幕刷新阻塞和阈值 `30` 敏感，需要用实测日志标定。

## 7. 距离门能量日志

能量日志示例：

```text
[RADAR_E] g00=... g01=... g02=... g03=... g04=... g05=... g06=... g07=...
[RADAR_E] g08=... g09=... g10=... g11=... g12=... g13=... g14=... g15=...
[RADAR_E] g16=... g17=... g18=... g19=... g20=... g21=... g22=... g23=...
[RADAR_E] g24=... g25=... g26=... g27=... g28=... g29=... g30=... g31=...
```

判断规则：

- 每轮完整雷达日志应覆盖 `g00` 到 `g31`。
- 所有 gate 长期为 0：上报内容异常或解析错误。
- 某些 gate 随人体靠近/远离明显变化：说明距离门数据可用于特征分析。
- 能量值不能直接称为“人体活动强度”或“风险等级”，必须经过特征层和状态机解释。

## 8. 人体存在合成日志

当前检测日志示例：

```text
[DETECT] pir=0 rd03_ot2=1 radar_valid=1 radar_presence=1 radar_cm=85 mq_raw=... mq_adc_mv=... mq_ao_est_mv=...
```

字段含义：

```text
pir              PIR GPIO 状态，当前项目计划废弃
rd03_ot2         雷达 OT2 数字有人/无人输出
radar_valid      UART 上报是否有效
radar_presence   UART 上报有人/无人
radar_cm         UART 上报距离
mq_raw           ADC 原始值
mq_adc_mv        STM32 ADC 管脚电压
mq_ao_est_mv     按分压反推的 MQ2 AO 电压
mq_filtered_mv   经过多次采样和 EMA 滤波后的 AO 反推电压，对外 status gas 使用这个值
mq_base_mv       运行中学习到的背景基线电压
mq_delta_mv      当前滤波值相对基线升高的电压，用于判断相对变化
mq_ppm_est       基于 Rs/R0 = 11.5428 * ppm^(-0.6549) 和当前环境基线推算的 ppm 估算值，不等于经过标准气体标定的计量值
mq_ppm_dbg_offset 调试/演示专用 ppm 偏移量；采集真实 MQ 数据时应为 0
```

当前代码中 `presence` 合成逻辑仍然是：

```text
presence = pir || rd03_ot2 || (radar_valid && radar_presence)
```

分析日志时必须标注：

- 如果 `pir=1` 但雷达两路都为 0，当前系统仍会把 `presence` 当成 1。
- 由于 PIR 已计划废弃，正式规则上线前应改为只使用 `rd03_ot2` 和 UART 雷达状态，或只使用 UART 主数据源。

## 9. Wi-Fi 上报前状态日志

状态发送日志示例：

```text
[INFO] status tx temp=25.6 hum=61.0 gas_ppm_est=1 gas_dbg_offset=0 gas_mv=1235 presence=1 risk=1 state=NORMAL relay=0 env_valid=1 gas_valid=1
```

判断规则：

- 该日志表示 STM32 已将状态交给 ESP8266 UART 网关发送队列。
- `risk` 当前仍是占位规则：

```text
gas_ppm_est >= 300 -> risk 3
gas_ppm_est >= 100 -> risk 2
presence=1  -> risk 1
else        -> risk 0
```

- 不要把当前 `risk=1` 解释成真实老人风险，它目前只表示检测到 presence。
- 如果 HA 数据异常，先对比该日志和 ESP8266 MQTT payload 是否一致。
- `gas_dbg_offset` 表示远程/本地演示注入的 ppm 偏移量。若它不为 0，当前 gas ppm 和风险等级包含人为调试注入，不适合作为真实 MQ 标定样本。

## 10. 快速结论模板

分析完日志后，按以下格式输出：

```text
结论:
- 系统启动: 正常/异常
- 环境采集: 正常/异常
- MQ2 采集: 正常/异常
- Rd-03 配置 ACK: open/report/close = x/x/x
- Rd-03 UART 上报: valid=0/1, frames=...
- 雷达存在状态: presence 是否随场景变化
- 雷达距离: distance_cm 是否随距离变化
- 雷达能量门: g00-g31 是否完整且有变化
- 雷达特征: zone/motion/still_s 是否可用于状态机
- STM32->ESP 状态发送: 正常/异常
- 主要风险: ...
- 建议下一步: ...
```

## 11. 常见故障表

| 日志现象 | 高概率原因 | 下一步 |
| --- | --- | --- |
| 没有任何日志 | 串口号/波特率/固件运行问题 | 扫描串口，确认 USART1/ST-LINK VCP |
| `[FAIL] error handler` | 外设初始化或运行错误 | 查看前一条日志 |
| `bme280 init failed` | I2C/供电/地址问题 | 检查 PB8/PB9、3V3、GND |
| `rd03 config ack open=0` | 命令链路不通 | 检查 PB10 -> 雷达 RX |
| `valid=0 rx_bytes=0` | 雷达 UART 输出没进 STM32 | 检查 OT1 -> PB11 |
| `rx_bytes>0 header_sync=0` | 不是上报模式二进制帧 | 检查 report mode ACK |
| `bad_footer` 增加 | 丢字节或串口干扰 | 减少阻塞，后续升级 DMA |
| `presence` 长期为 1 | 目标消失延迟、OT2 残留或 PIR 污染 | 对照 `rd03_ot2/radar_presence/pir` |
| `distance_cm` 不随距离变化 | 安装角度/目标位置/协议解析问题 | 采集分距离标定日志 |
| `still_s` 不稳定 | motion 阈值未标定或丢帧 | 用场景日志调整阈值 |

## 12. 分析优先级

优先看协议链路，再看特征，再看上层业务：

```text
rd03 ACK -> valid frames -> presence/distance -> gate energy -> radar_features -> status tx -> HA
```

只有前面的链路稳定后，后续规则状态机才有可靠输入。

## 13. 本地联调快捷键

当前 COM6 调试入口既用于现场验收，也用于没有外设时的兜底模拟。常用命令如下：

```text
p      打印 app / sensor / radar 三段状态摘要
6      模拟 ESP32-S3 语音 RISK:1，进入短暂 NOTICE
7      模拟 ESP32-S3 语音 RISK:2，进入 ACK_WAIT/SOS 确认流程
8      模拟 ESP32-S3 语音 RISK:3，进入高风险 ACK_WAIT/SOS 确认流程
9      模拟 ESP32-S3 语音 RISK:0，清除语音风险
4      气体 ppm 调试偏移 +150
5      气体 ppm 调试偏移 +350
0      清除气体 ppm 调试偏移
e/w/f/j/g/v/x  设置 AI_SAMPLE 采集标签
```

`p` 输出建议按三层读：

- `console app`：看状态机、风险、事件来源、继电器 manual/auto mask、AI session/label。
- `console sensor`：看环境、气体 ppm、气体基线和调试偏移。
- `console radar`：看 PIR、RD03 OT2、UART radar presence、距离、zone、motion/still/occupied。

如果 `presence=1`，但 `pir/rd03_ot2/radar_presence` 只有一路为 1，需要在结论里写清楚是哪一路触发，不能笼统说“雷达检测到有人”。

`console app` 和 `[AI_SAMPLE]` 中的 `risk_src` 是当前风险来源的可读摘要：

```text
VOICE_ACK / GAS_ACK / RADAR_ACK / BUTTON_ACK / REMOTE_ACK  正在等待确认的高优先级事件
VOICE       语音低风险 NOTICE 或语音风险下限
GAS         MQ ppm 达到气体风险阈值
NETWORK     网络离线导致的保守风险
RADAR_UART  UART 雷达 presence 触发
RD03_OT2    RD03 OT2 数字输出触发
PIR         PIR 输入触发
NONE        当前无主要风险来源
```

`console scene` 是新的多场景引擎摘要：

```text
top       当前融合后最重要的场景
action    建议动作：OBSERVE / REPORT / NOTICE / ACK / ALARM
severity  场景严重程度 0-3
conf      置信度 0-100
count     当前活跃场景数量
mask      当前活跃场景 bitset
evidence  top 场景的两个紧凑证据值
```

第一版场景引擎只做并行分析和记录，不直接接管状态机动作。判断报警行为时仍看 `state/scenario/risk`；判断“系统还能分析出哪些场景”时看 `scene_top/scene_mask`。

## 14. AI_SAMPLE 快速处理

采集日志后，建议先用脚本把原始日志变成可检查数据：

```powershell
python tools\ai_sample_to_csv.py COM6-115200.log ai_samples.csv
python tools\ai_dataset_summary.py ai_samples.csv
python tools\ai_window_features.py ai_samples.csv ai_windows.csv
```

`ai_dataset_summary.py` 用来快速确认采集是否有效，例如每个 label 有多少样本、`env_valid/gas_valid/radar_valid` 是否长期为 0、`gas_ppm/radar_cm/motion/still` 的范围是否合理。

`ai_window_features.py` 会按默认 10 秒窗口、5 秒步长输出训练用特征。后续做跌倒、长静止、离床、气体异常等场景分类时，优先用窗口特征，不直接拿单点样本做判断。
