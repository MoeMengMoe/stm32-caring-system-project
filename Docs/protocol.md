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

## STM32 发给 ESP8266 的状态 JSON

```json
{"temperature":25.6,"humidity":61,"gas":120,"presence":1,"risk":2,"event":"normal"}
```

## 字段说明

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `temperature` | number | 温度，单位摄氏度 |
| `humidity` | number | 相对湿度，单位百分比 |
| `gas` | number | MQ2 原始或归一化采样值 |
| `presence` | number | 是否检测到人体活动，0/1 |
| `risk` | number | 风险等级，0-3 |
| `event` | string | 事件名称 |

## MQTT Topic

| Topic | 用途 |
| --- | --- |
| `eldercare/node01/status` | 周期状态 |
| `eldercare/node01/event` | 事件上报 |
| `eldercare/node01/alarm` | 告警状态 |

## 风险等级

| 等级 | 含义 |
| --- | --- |
| 0 | 正常 |
| 1 | 提醒 |
| 2 | 警告 |
| 3 | 高风险 |

