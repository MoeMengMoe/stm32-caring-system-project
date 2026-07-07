# 状态机联动验收说明

更新时间：2026-07-06

## 1. 当前目标

本轮目标不是新增传感器，而是确认当前节点已经从“模块能跑”进入“产品闭环能跑”：

```text
传感器 / 本地命令 / 云端命令
  -> AppStateMachine
  -> TFT 状态展示
  -> 蜂鸣器提示
  -> 继电器联动
  -> ESP8266 / MQTT 状态和事件上报
  -> ACK / clear 解除
```

## 2. 串口命令

COM6 使用 `115200 8N1`。

```text
s  本地 SOS
1  模拟主动求助 / 跌倒场景
2  模拟长时间静止无响应场景
3  模拟 MQ 气体风险场景
4  MQ ppm 调试偏移 +150
5  MQ ppm 调试偏移 +350
0  清除 MQ ppm 调试偏移
a  本地 ACK
c  清除告警
o  模拟网络离线
n  模拟网络恢复
r  手动切换 relay1
t  手动切换 relay2
y  手动切换 relay3
u  手动切换 relay4
p  打印当前状态
b  蜂鸣器单独测试
```

## 3. 预期现象

### 3.1 SOS / 跌倒演示

操作：

```text
1
```

预期：

- 串口出现 `state=SOS ACK` 或 `state=ACK_WAIT`。
- `scenario=SOS/FALL`。
- 蜂鸣器按等待确认节奏响。
- relay1 自动打开。
- TFT 右下角状态进入 SOS/ACK 类提示。
- 串口出现可读 `app event`，例如 `scenario=SOS/FALL(1) type=REMOTE_TRIGGER(1) source=REMOTE state=NORMAL->ACK WAIT`，事件帧发给 ESP8266。

再输入：

```text
a
```

预期：

- 状态进入 `CLEARED`，随后回到 `NORMAL`。
- relay1 自动关闭。
- 若之前云端/手动打开过继电器，ACK 会清空 manual mask。

### 3.2 长时间静止演示

操作：

```text
2
```

预期：

- `scenario=LONGSTILL`。
- 进入等待确认。
- 超过 ACK 时间后进入 `NO RESP`，风险升高。

### 3.3 气体风险演示

操作：

```text
3
```

预期：

- `scenario=GAS`。
- 进入 `GAS ACK`。
- relay1 自动打开。
- 蜂鸣器提示。
- 事件日志包含 `scenario=GAS(4) type=GAS_RISK(11) source=SENSOR`。

真实传感器触发时，规则为：

```text
gas_ppm_est >= 100 -> risk 2，并进入 GAS_RISK 确认流程
gas_ppm_est >= 300 -> risk 3
```

### 3.3.1 远程 MQ ppm 调试注入

没有安全气体源时，Simon 可以通过云端/ESP8266 直接下发 ppm 偏移量，用于验收气体风险闭环：

```text
D,9001,6,4,150
D,9002,6,4,350
D,9003,6,4,0
```

预期：

- `D,9001,6,4,150` 后，COM6 出现 `[INFO] cloud gas ppm debug offset=150`。
- 后续 `[DETECT]` 中 `mq_ppm_dbg_offset=150`，`mq_ppm_est` 会在真实 MQ 估算值基础上增加 150。
- 后续 `[INFO] status tx` 中 `gas_dbg_offset=150`，状态帧 `S` 的 gas 字段同步升高。
- 如果 `gas_ppm_est >= 100`，状态机进入 `GAS_RISK` 确认流程；如果 `gas_ppm_est >= 300`，风险等级为 3。
- 演示完成必须发送 `D,9003,6,4,0`，或本地 COM6 发送 `0`，清除调试偏移。
- 该调试注入不改变 `mq_adc_mv / mq_ao_est_mv / mq_filtered_mv`，采集真实标定数据时必须记录 `mq_ppm_dbg_offset=0`。

### 3.4 网络离线演示

操作：

```text
o
```

预期：

- `state=OFFLINE` 或显示离线提示。
- relay2 自动打开。
- 风险至少为 1。

再输入：

```text
n
```

预期：

- 网络状态恢复。
- relay2 自动关闭，除非 manual mask 或其他自动规则仍在保持继电器。

### 3.5 ESP32-S3 本地语音联动演示

接线：

```text
ESP32-S3 GPIO17 / UART1_TX -> STM32 A8 / CN10 pin 11 / PA1 / UART4_RX
ESP32-S3 GPIO18 / UART1_RX <- STM32 CN10 pin 29 / D32 / PA0 / UART4_TX
ESP32-S3 GND -> STM32 GND
```

ESP32-S3 识别到 Simon 当前映射的语音命令后，会向 STM32 发送：

```text
RISK:0
RISK:1
RISK:2
RISK:3
```

如果 ESP32-S3 暂时没有接好，也可以直接在 COM6 发送单字符模拟同一条 STM32 语音风险链路：

```text
6 -> RISK:1
7 -> RISK:2
8 -> RISK:3
9 -> RISK:0
```

预期：

- `RISK:3` 或 `RISK:2` 后，COM6 出现 `[INFO] voice risk cmd id=... level=3` 或 `level=2`。
- 状态机进入场景一 `SOS/FALL` 的确认等待流程，TFT 显示 `SOS ACK`，蜂鸣器按等待确认节奏提示，relay1 自动打开。
- `RISK:0` 后，COM6 出现 `[INFO] voice risk cmd id=... level=0`，当前告警被清除。
- `RISK:1` 当前作为低风险/家居控制类语音事件预留，STM32 不触发硬告警，会进入 `NOTICE`，`risk` 短暂保持为 1，约 10 秒后自动退回；`RISK:0` 或 ACK/clear 会立即清除。
- 同一个 `RISK:x` 在 2 秒内重复发送时只记录第一次，避免语音模块连续输出导致事件刷屏。
- 事件日志应出现 `type=VOICE_RISK(12) source=VOICE`；`flags` 的 bit8-bit9 保存原始语音风险等级。

## 4. 验收关注点

- `p` 输出中的 `state / scenario / risk / risk_src / ack_ms / relay / manual / auto` 是否一致。
- TFT 状态是否和串口状态一致。
- relay1 是否只跟随告警/等待确认自动打开。
- relay2 是否只跟随网络离线自动打开。
- ACK 是否能清除本地状态，并清空 manual mask。
- `status tx` 是否继续每约 2 秒出现。
- ESP8266 / MQTT 是否收到 `S` 状态帧和 `E` 事件帧。
