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

## 4. 验收关注点

- `p` 输出中的 `state / scenario / risk / ack_ms / relay / manual / auto` 是否一致。
- TFT 状态是否和串口状态一致。
- relay1 是否只跟随告警/等待确认自动打开。
- relay2 是否只跟随网络离线自动打开。
- ACK 是否能清除本地状态，并清空 manual mask。
- `status tx` 是否继续每约 2 秒出现。
- ESP8266 / MQTT 是否收到 `S` 状态帧和 `E` 事件帧。
