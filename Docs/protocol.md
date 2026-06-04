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

STM32 当前不直接拼接 JSON，而是通过 USART2 TX DMA 发送固定顺序的 CSV 行：

```text
seq,temperature,humidity,gas,presence,risk\r\n
```

示例：

```text
0,25.6,61.0,120,1,2\r\n
```

## 字段说明

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `seq` | number | STM32 状态帧序号，每发送一帧递增 |
| `temperature` | number | 温度，单位摄氏度 |
| `humidity` | number | 相对湿度，单位百分比 |
| `gas` | number | 当前为 MQ 模块 AO 反推电压，近似单位 mV，不是 ppm |
| `presence` | number | 是否检测到人体存在，0/1 |
| `risk` | number | 风险等级，0-3 |

当前不会通过此状态帧上传 BME280 气压、Rd-03 距离或距离门能量。

## ESP8266 发给 MQTT 的状态 JSON

ESP8266 解析 STM32 CSV 后，将其转换为 JSON 并发布到 `eldercare/node01/status`：

```json
{"node_id":"node01","seq":0,"temperature":25.6,"humidity":61.0,"gas":120,"presence":1,"risk":2,"event":"warning"}
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

## 风险等级

| 等级 | 含义 |
| --- | --- |
| 0 | 正常 |
| 1 | 提醒 |
| 2 | 警告 |
| 3 | 高风险 |
