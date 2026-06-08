# UART / MQTT 协议规范

## UART 基本配置

| 项目 | 配置 |
| --- | --- |
| 波特率 | 115200 |
| 数据位 | 8 |
| 停止位 | 1 |
| 校验位 | 无 |
| 结束符 | `\r\n` |
| 编码 | UTF-8 / ASCII |

## STM32 发给 ESP8266 的状态 CSV

STM32 当前不直接拼接 JSON，而是通过 USART2 TX DMA 发送带帧类型的固定顺序 CSV 行：

```text
S,seq,temperature,humidity,gas,presence,risk,relay_state_mask,cloud_perm_mask\r\n
```

示例：

```text
S,18,25.6,61.0,120,1,0,5,15\r\n
```

## 字段说明

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `S` | string | 状态帧类型 |
| `seq` | number | STM32 状态帧序号，每发送一帧递增 |
| `temperature` | number | 温度，单位摄氏度 |
| `humidity` | number | 相对湿度，单位百分比 |
| `gas` | number | 当前为 MQ 模块 AO 反推电压，近似单位 mV，不是 ppm |
| `presence` | number | 是否检测到人体存在，0/1 |
| `risk` | number | 风险等级，0-3 |
| `relay_state_mask` | number | 四路继电器实际状态，bit0-bit3 对应继电器 1-4，1 表示 ON |
| `cloud_perm_mask` | number | 四路继电器云控权限，bit0-bit3 对应继电器 1-4，1 表示允许云控 |

当前不会通过此状态帧上传 BME280 气压、Rd-03 距离或距离门能量。

当前约定：

- `cloud_perm_mask` 默认值为 `15`，即四路继电器默认允许云控。
- 权限字段作为主控后续策略接口保留，本阶段服务器和 Home Assistant 不展示、不作为 UI 控制条件。
- 继电器实际是否动作必须以 STM32 主控回传结果为准，ESP8266 和服务器不得自行假定执行成功。

## ESP8266 发给 STM32 的云控命令 CSV

ESP8266 订阅 MQTT 继电器命令后，通过 USART2 发给 STM32：

```text
C,request_id,relay_id,action\r\n
```

示例：

```text
C,102,2,ON\r\n
```

字段说明：

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `C` | string | 云控命令帧类型 |
| `request_id` | number | 命令请求序号，由 ESP8266 或云端生成 |
| `relay_id` | number | 继电器编号，范围 `1-4` |
| `action` | string | `ON` 或 `OFF` |

## STM32 发给 ESP8266 的继电器结果 CSV

STM32 主控执行或拒绝命令后回传：

```text
R,request_id,relay_id,result,state,reason\r\n
```

示例：

```text
R,102,2,OK,ON,none\r\n
R,103,2,DENY,ON,cloud_disabled\r\n
```

字段说明：

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `R` | string | 继电器结果帧类型 |
| `request_id` | number | 对应 `C` 帧中的请求序号 |
| `relay_id` | number | 继电器编号，范围 `1-4` |
| `result` | string | `OK`、`DENY` 或 `ERR` |
| `state` | string | STM32 确认后的最终状态，`ON` 或 `OFF` |
| `reason` | string | `none`、`cloud_disabled`、`invalid_id`、`invalid_action`、`hardware_fault`、`busy` |

## ESP8266 发给 MQTT 的状态 JSON

ESP8266 解析 STM32 `S` 帧后，将其转换为 JSON 并发布到 `eldercare/node01/status`：

```json
{"node_id":"node01","seq":18,"temperature":25.6,"humidity":61.0,"gas":120,"presence":1,"risk":0,"event":"normal","relay_state_mask":5,"cloud_perm_mask":15}
```

`event` 由 ESP8266 根据 `risk` 转换：

| risk | event |
| --- | --- |
| 0 | `normal` |
| 1 | `notice` |
| 2 | `warning` |
| 3 | `alarm` |

## MQTT Topic

| Topic | 用途 | 当前状态 |
| --- | --- | --- |
| `eldercare/node01/status` | 周期状态 | ESP8266 当前实际发布 |
| `eldercare/node01/event` | 独立事件上报 | 预留，尚未实现 |
| `eldercare/node01/alarm` | 独立告警状态 | 预留，尚未实现 |
| `eldercare/node01/relay/1/set` ~ `eldercare/node01/relay/4/set` | 四路继电器云控命令 | 后续开发 |
| `eldercare/node01/relay/1/state` ~ `eldercare/node01/relay/4/state` | 四路继电器状态回传 | 后续开发 |
| `eldercare/node01/relay/1/result` ~ `eldercare/node01/relay/4/result` | 四路继电器命令执行结果 | 后续开发 |

## 风险等级

| 等级 | 含义 |
| --- | --- |
| 0 | 正常 |
| 1 | 提醒 |
| 2 | 警告 |
| 3 | 高风险 |
