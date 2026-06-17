# UART / MQTT 协议冻结版

冻结日期：2026-06-16

本文档是 6.30 比赛版本的通信协议唯一依据。后续代码、Home Assistant、dashboard、`analysis_service` 和 ESP8266 网关均以本文档为准。

## 1. 冻结原则

- STM32 与 ESP8266 之间继续使用固定顺序 CSV 行，降低主控侧解析和组包复杂度。
- ESP8266 与 MQTT 之间使用 JSON，便于服务器、Home Assistant 和展示面板读取。
- 周期状态与关键事件分离：`status` 只表示当前状态，`event` 表示发生过的业务事件。
- 所有远程控制必须有回传：继电器以 `state/result` 为最终依据，不能用按钮点击推断执行成功。
- 6.30 前不再新增主场景，不再新增必填字段；需要扩展时只能新增可选字段。

## 2. UART 基本配置

| 项目 | 配置 |
| --- | --- |
| 链路 | STM32 USART2 <-> ESP8266 UART |
| 波特率 | 115200 |
| 数据位 | 8 |
| 停止位 | 1 |
| 校验位 | 无 |
| 结束符 | `\r\n` |
| 编码 | ASCII / UTF-8 |

UART 每一行第一列为帧类型：

| 帧类型 | 方向 | 用途 |
| --- | --- | --- |
| `S` | STM32 -> ESP8266 | 周期状态上报 |
| `E` | STM32 -> ESP8266 | 关键事件上报 |
| `C` | ESP8266 -> STM32 | 继电器云控命令 |
| `R` | STM32 -> ESP8266 | 继电器执行结果 |
| `D` | ESP8266 -> STM32 | 演示/业务控制命令 |

## 3. 枚举冻结

### 3.1 风险等级

| 值 | 含义 | MQTT 字段 |
| --- | --- | --- |
| `0` | 正常 | `normal` |
| `1` | 提醒 | `notice` |
| `2` | 警告 | `warning` |
| `3` | 高风险 | `alarm` |

### 3.2 场景 `scenario`

| 代码 | 字符串 | 含义 |
| --- | --- | --- |
| `0` | `NONE` | 无场景 |
| `1` | `SOS_OR_FALL_SIM` | 主动求助 / 模拟跌倒 |
| `2` | `LONG_STILL_NO_RESPONSE` | 长时间静止无响应 |
| `3` | `OFFLINE_AUTONOMY` | 断网本地自治与恢复补传 |

### 3.3 状态 `state`

| 代码 | 字符串 | 含义 |
| --- | --- | --- |
| `0` | `NORMAL` | 正常 |
| `1` | `NOTICE` | 提醒 |
| `2` | `ACK_WAIT` | 等待用户确认 |
| `3` | `ALARM` | 告警 |
| `4` | `NO_RESPONSE` | 超时无响应 |
| `5` | `CLEARED` | 已清除 |

说明：网络和供电不放进主状态机，分别使用 `network_state` 和 `power_state`。

### 3.4 事件类型 `event_type`

| 代码 | 字符串 | 含义 |
| --- | --- | --- |
| `0` | `STATUS_ONLY` | 仅状态更新，通常不写入事件 |
| `1` | `REMOTE_TRIGGER` | 远程或遥控触发 |
| `2` | `SOS_BUTTON` | 本地求助按钮 |
| `3` | `LONG_STILL` | 久静触发 |
| `4` | `USER_ACK` | 用户确认安全 |
| `5` | `ACK_TIMEOUT` | 确认超时 |
| `6` | `CLEAR_ALARM` | 清除当前告警 |
| `7` | `NETWORK_LOST` | 网络断开 |
| `8` | `NETWORK_RESTORED` | 网络恢复 |
| `9` | `POWER_BACKUP_ENTER` | 进入备用供电 |
| `10` | `POWER_NORMAL_RESTORED` | 市电或正常供电恢复 |

### 3.5 触发源 `trigger_source`

| 代码 | 字符串 | 含义 |
| --- | --- | --- |
| `0` | `LOCAL` | 主控本地逻辑 |
| `1` | `REMOTE` | 云端控制面板 |
| `2` | `BUTTON` | 本地按钮 |
| `3` | `RADAR` | 雷达/久静逻辑 |
| `4` | `NETWORK` | 网络状态变化 |
| `5` | `POWER` | 供电状态变化 |

### 3.6 事件结果 `result`

| 代码 | 字符串 | 含义 |
| --- | --- | --- |
| `0` | `CREATED` | 已创建 |
| `1` | `WAITING_ACK` | 等待确认 |
| `2` | `ACKNOWLEDGED` | 已确认安全 |
| `3` | `ESCALATED` | 已升级 |
| `4` | `CLEARED` | 已清除 |
| `5` | `OFFLINE_CACHED` | 离线缓存 |
| `6` | `BACKFILLED` | 恢复后补传 |
| `7` | `FAILED` | 执行失败 |

### 3.7 网络状态 `network_state`

| 代码 | 字符串 | 含义 |
| --- | --- | --- |
| `0` | `ONLINE` | 在线 |
| `1` | `OFFLINE` | 离线 |
| `2` | `RESTORED` | 刚恢复 |

### 3.8 供电状态 `power_state`

| 代码 | 字符串 | 含义 |
| --- | --- | --- |
| `0` | `NORMAL` | 正常供电 |
| `1` | `BACKUP` | 备用供电 |
| `2` | `LOW` | 低电量或供电不足 |

### 3.9 演示命令 `command_type`

| 代码 | 字符串 | 含义 |
| --- | --- | --- |
| `1` | `TRIGGER_SCENARIO` | 触发指定场景 |
| `2` | `USER_ACK` | 模拟用户确认 |
| `3` | `CLEAR_ALARM` | 清除当前告警 |
| `4` | `SIMULATE_NETWORK` | 模拟网络离线/恢复 |
| `5` | `SET_RELAY` | 设置继电器 |

## 4. UART 上行：状态帧 `S`

STM32 每 2 秒左右发送一行状态：

```text
S,seq,temperature,humidity,gas,presence,risk,relay_state_mask,cloud_perm_mask\r\n
```

示例：

```text
S,18,25.6,61.0,120,1,0,5,15\r\n
```

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `S` | string | 状态帧类型 |
| `seq` | uint32 | STM32 状态帧序号，递增 |
| `temperature` | float | 温度，单位摄氏度 |
| `humidity` | float | 相对湿度，单位 `%` |
| `gas` | int | MQ 模块 AO 反推电压，近似 mV，不是 ppm |
| `presence` | int | 是否检测到人体存在，`0/1` |
| `risk` | int | 风险等级，`0-3` |
| `relay_state_mask` | int | 四路继电器实际状态，bit0-bit3 对应 1-4 路 |
| `cloud_perm_mask` | int | 四路继电器云控权限，bit0-bit3 对应 1-4 路 |

约束：

- `presence` 只能为 `0` 或 `1`。
- `risk` 只能为 `0-3`。
- `relay_state_mask` 和 `cloud_perm_mask` 范围为 `0-15`。
- `cloud_perm_mask` 默认 `15`，服务器和 HA 第一版不根据它隐藏按钮。
- `gas` 不得在 UI 或答辩中称为 ppm 或气体浓度。

## 5. UART 上行：事件帧 `E`

STM32 在关键业务状态变化时发送事件帧：

```text
E,event_id,scenario,event_type,trigger_source,state_before,state_after,risk,result,network_state,power_state,flags,timestamp_ms\r\n
```

示例：

```text
E,1001,1,1,1,0,2,2,1,0,0,0,123456\r\n
```

含义：事件 `1001`，场景一，由远程触发，从 `NORMAL` 进入 `ACK_WAIT`，风险 `2`，等待确认，在线，正常供电。

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `E` | string | 事件帧类型 |
| `event_id` | uint32 | STM32 本地事件 ID，递增，断电后允许重置 |
| `scenario` | int | 见 `scenario` 枚举 |
| `event_type` | int | 见 `event_type` 枚举 |
| `trigger_source` | int | 见 `trigger_source` 枚举 |
| `state_before` | int | 变化前状态 |
| `state_after` | int | 变化后状态 |
| `risk` | int | 事件发生后的风险等级，`0-3` |
| `result` | int | 见 `result` 枚举 |
| `network_state` | int | 见 `network_state` 枚举 |
| `power_state` | int | 见 `power_state` 枚举 |
| `flags` | uint32 | 位标志，当前可为 `0` |
| `timestamp_ms` | uint32 | STM32 启动后的毫秒时间 |

`flags` 冻结位定义：

| bit | 含义 |
| --- | --- |
| `0` | 该事件为离线期间缓存事件 |
| `1` | 该事件为恢复联网后的补传事件 |
| `2` | 该事件需要云端展示为当前告警 |
| `3` | 该事件已在本地确认 |

说明：

- 事件帧只在关键变化时发送，不按周期发送。
- 若 6.30 前 STM32 事件帧来不及实现，服务器允许由控制面板生成同结构 MQTT event 作为演示降级，但字段必须保持一致。

## 6. UART 下行：继电器命令帧 `C`

ESP8266 收到 MQTT 继电器命令后，转发给 STM32：

```text
C,request_id,relay_id,action\r\n
```

示例：

```text
C,102,2,ON\r\n
```

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `C` | string | 继电器命令帧 |
| `request_id` | uint32 | 请求序号，由 ESP8266 或云端生成 |
| `relay_id` | int | 继电器编号，`1-4` |
| `action` | string | `ON` 或 `OFF` |

## 7. UART 上行：继电器结果帧 `R`

STM32 执行或拒绝继电器命令后回传：

```text
R,request_id,relay_id,result,state,reason\r\n
```

示例：

```text
R,102,2,OK,ON,none\r\n
R,103,2,DENY,ON,cloud_disabled\r\n
```

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `R` | string | 继电器结果帧 |
| `request_id` | uint32 | 对应 `C` 帧请求序号 |
| `relay_id` | int | 继电器编号，`1-4` |
| `result` | string | `OK`、`DENY` 或 `ERR` |
| `state` | string | STM32 确认后的最终状态，`ON` 或 `OFF` |
| `reason` | string | 原因码 |

`reason` 固定取值：

```text
none
cloud_disabled
invalid_id
invalid_action
hardware_fault
busy
```

## 8. UART 下行：演示命令帧 `D`

ESP8266 收到 MQTT `demo/command` 后，可转发给 STM32 的业务层：

```text
D,request_id,command_type,scenario,value\r\n
```

示例：

```text
D,2001,1,1,1\r\n
D,2002,2,0,1\r\n
D,2003,3,0,1\r\n
D,2004,4,3,0\r\n
```

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `D` | string | 演示/业务命令帧 |
| `request_id` | uint32 | 请求序号 |
| `command_type` | int | 见 `command_type` 枚举 |
| `scenario` | int | 目标场景；无场景时为 `0` |
| `value` | int | 命令参数；触发/确认/清除用 `1`，网络模拟中 `0=OFFLINE`、`1=ONLINE` |

说明：

- `D` 帧用于比赛演示控制，不替代 STM32 本地状态机。
- STM32 仍是本地告警、确认、继电器实际执行的最终所有者。

## 9. MQTT Topic 冻结

| Topic | 方向 | Payload | 用途 |
| --- | --- | --- | --- |
| `eldercare/node01/status` | ESP8266 -> MQTT | JSON | 周期状态 |
| `eldercare/node01/event` | ESP8266/dashboard -> MQTT | JSON | 关键事件 |
| `eldercare/node01/alarm` | analysis_service -> MQTT | JSON | 当前告警状态 |
| `eldercare/node01/analysis` | analysis_service -> MQTT | JSON | 云端规则/LLM 分析 |
| `eldercare/node01/demo/command` | dashboard -> MQTT | JSON | 演示控制命令 |
| `eldercare/node01/demo/state` | analysis_service/dashboard -> MQTT | JSON | 演示控制状态回显 |
| `eldercare/node01/relay/1/set` ~ `relay/4/set` | HA/dashboard -> MQTT | string 或 JSON | 继电器命令 |
| `eldercare/node01/relay/1/state` ~ `relay/4/state` | ESP8266 -> MQTT | JSON | 继电器确认状态 |
| `eldercare/node01/relay/1/result` ~ `relay/4/result` | ESP8266 -> MQTT | JSON | 继电器执行结果 |

## 10. MQTT Payload：`status`

ESP8266 将 `S` 帧转换为：

```json
{
  "node_id": "node01",
  "seq": 18,
  "temperature": 25.6,
  "humidity": 61.0,
  "gas": 120,
  "presence": 1,
  "risk": 0,
  "event": "normal",
  "relay_state_mask": 5,
  "cloud_perm_mask": 15
}
```

必填字段：

```text
node_id, seq, temperature, humidity, gas, presence, risk, event, relay_state_mask, cloud_perm_mask
```

## 11. MQTT Payload：`event`

ESP8266 将 `E` 帧转换为；dashboard 降级生成事件时也必须使用同一结构：

```json
{
  "node_id": "node01",
  "event_id": 1001,
  "scenario": "SOS_OR_FALL_SIM",
  "event_type": "REMOTE_TRIGGER",
  "trigger_source": "REMOTE",
  "state_before": "NORMAL",
  "state_after": "ACK_WAIT",
  "risk": 2,
  "result": "WAITING_ACK",
  "network_state": "ONLINE",
  "power_state": "NORMAL",
  "flags": 0,
  "timestamp_ms": 123456
}
```

必填字段：

```text
node_id, event_id, scenario, event_type, trigger_source, state_before, state_after, risk, result, network_state, power_state, flags, timestamp_ms
```

服务器入库时可追加以下字段，但设备不需要上报：

```text
received_at, payload_json, is_backfilled
```

## 12. MQTT Payload：`alarm`

`analysis_service` 根据 `event` 或 `analysis` 发布当前告警状态：

```json
{
  "node_id": "node01",
  "active": true,
  "event_id": 1001,
  "scenario": "SOS_OR_FALL_SIM",
  "state": "ALARM",
  "risk": 3,
  "message": "确认超时，已升级为高风险告警",
  "updated_at_ms": 123999
}
```

清除告警时：

```json
{
  "node_id": "node01",
  "active": false,
  "event_id": 1001,
  "scenario": "SOS_OR_FALL_SIM",
  "state": "CLEARED",
  "risk": 0,
  "message": "告警已清除",
  "updated_at_ms": 130000
}
```

## 13. MQTT Payload：`analysis`

现有 `analysis_service` 继续发布：

```json
{
  "node_id": "node01",
  "source_seq": 18,
  "source_risk": 2,
  "cloud_risk": 3,
  "risk_score": 85,
  "summary": "燃气读数偏高，建议立即确认现场安全。",
  "model_used": false,
  "need_family_notice": true,
  "need_community_notice": true,
  "need_hospital_notice": false
}
```

说明：

- `analysis` 是云端辅助分析，不替代 STM32 本地状态机。
- 大模型不得直接主导继电器或告警闭环，只能给出建议。

## 14. MQTT Payload：`demo/command`

dashboard 控制面板发布：

```json
{
  "command_id": "demo-2001",
  "request_id": 2001,
  "command_type": "TRIGGER_SCENARIO",
  "scenario": "SOS_OR_FALL_SIM",
  "value": 1,
  "source": "dashboard"
}
```

常用命令：

| 操作 | `command_type` | `scenario` | `value` |
| --- | --- | --- | --- |
| 触发场景一 | `TRIGGER_SCENARIO` | `SOS_OR_FALL_SIM` | `1` |
| 触发场景二 | `TRIGGER_SCENARIO` | `LONG_STILL_NO_RESPONSE` | `1` |
| 用户确认 | `USER_ACK` | `NONE` | `1` |
| 清除告警 | `CLEAR_ALARM` | `NONE` | `1` |
| 模拟离线 | `SIMULATE_NETWORK` | `OFFLINE_AUTONOMY` | `0` |
| 模拟恢复 | `SIMULATE_NETWORK` | `OFFLINE_AUTONOMY` | `1` |

## 15. MQTT Payload：继电器

### 15.1 `relay/x/set`

兼容 Home Assistant，允许两种输入。

最小字符串格式：

```text
ON
```

推荐 JSON 格式：

```json
{
  "request_id": 3001,
  "action": "ON",
  "source": "dashboard"
}
```

规则：

- `action` 只能为 `ON` 或 `OFF`。
- 如果 payload 是字符串，ESP8266 负责生成 `request_id`。
- ESP8266 必须转发为 UART `C` 帧，除非继电器最终确认直接接在 ESP8266。

### 15.2 `relay/x/state`

```json
{
  "node_id": "node01",
  "relay_id": 1,
  "state": "ON",
  "request_id": 3001
}
```

### 15.3 `relay/x/result`

```json
{
  "node_id": "node01",
  "request_id": 3001,
  "relay_id": 1,
  "result": "OK",
  "state": "ON",
  "reason": "none"
}
```

Home Assistant 和 dashboard 必须以 `relay/x/state` 为最终显示依据。

## 16. 三场景事件流

### 16.1 场景一：主动求助 / 模拟跌倒

```text
TRIGGER_SCENARIO
  -> E(... SOS_OR_FALL_SIM, REMOTE_TRIGGER/SOS_BUTTON, NORMAL -> ACK_WAIT, WAITING_ACK)
  -> USER_ACK: E(... USER_ACK, ACK_WAIT -> CLEARED, ACKNOWLEDGED)
  -> ACK_TIMEOUT: E(... ACK_TIMEOUT, ACK_WAIT -> ALARM/NO_RESPONSE, ESCALATED)
  -> CLEAR_ALARM: E(... CLEAR_ALARM, ALARM -> CLEARED, CLEARED)
```

### 16.2 场景二：长时间静止无响应

```text
LONG_STILL
  -> E(... LONG_STILL_NO_RESPONSE, LONG_STILL, NORMAL/NOTICE -> ACK_WAIT, WAITING_ACK)
  -> USER_ACK: E(... USER_ACK, ACK_WAIT -> CLEARED, ACKNOWLEDGED)
  -> ACK_TIMEOUT: E(... ACK_TIMEOUT, ACK_WAIT -> NO_RESPONSE, ESCALATED)
```

### 16.3 场景三：断网本地自治与补传

```text
NETWORK_LOST
  -> E(... OFFLINE_AUTONOMY, NETWORK_LOST, *, *, OFFLINE_CACHED, network_state=OFFLINE, flags bit0=1)
NETWORK_RESTORED
  -> E(... OFFLINE_AUTONOMY, NETWORK_RESTORED, *, *, BACKFILLED, network_state=RESTORED, flags bit1=1)
```

## 17. 兼容与降级

- `status` topic 是基础链路，不允许破坏。
- `event` topic 是三场景闭环的第一优先级新增 topic。
- 如果 STM32 事件帧未及时完成，dashboard 可以先发布同结构 `event` 做演示降级。
- 如果 dashboard API 未及时完成，可用 `mosquitto_pub` 发布 `demo/command` 和 `event` 测试。
- 如果继电器硬件未稳定，允许用 LED 或低压灯替代，但 `relay/x/state/result` 仍必须按协议发布。

## 18. 后续变更规则

冻结后如需变更：

1. 不删除已冻结字段。
2. 不改变已冻结枚举含义。
3. 新增字段必须为可选字段。
4. 新增 topic 必须先更新本文档。
5. 会影响 STM32、ESP8266、server、HA 任意两端以上的改动，必须先同步全组。
