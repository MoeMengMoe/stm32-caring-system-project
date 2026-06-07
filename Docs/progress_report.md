# 项目当前进度与产品能力说明

更新时间：2026-06-04

## 1. 一句话结论

当前产品是一个“独居老人居家环境与人体存在联网监测节点”原型。

它已经能够采集环境和人体存在相关数据，也已经具备将状态经 ESP8266、MQTT 发送到 Home Assistant 的代码与服务器基础；但真实传感器数据的完整联网闭环仍缺少正式验收记录，正式风险判断、本地告警和通知联动尚未完成。

本文中的“已验证”表示已经有上板或链路调试记录；“代码已实现”只表示仓库中存在实现，不能自动等同于实物产品已经可用。Rd-03 UART 驱动已通过 `COM6-115200.log` 完成上板验收。

因此，当前阶段不能称为完整的“看护告警产品”，更准确的名称是：

```text
联网监测 MVP 原型
```

## 2. 现在拿到产品能做什么

### 2.1 已上板验证的主控采集能力

| 功能 | 数据来源 | 当前能做什么 | 限制 |
| --- | --- | --- | --- |
| 温度监测 | BME280 | 读取室内温度并通过调试串口输出 | 尚未定义温度异常规则 |
| 湿度监测 | BME280 | 读取室内相对湿度并通过调试串口输出 | 尚未定义湿度异常规则 |
| 气压监测 | BME280 | 读取气压并通过调试串口输出 | 当前不会上传到 ESP8266/MQTT |
| 气体模块模拟量监测 | MQ 模块 AO | 读取 AO 分压后的 ADC 数值，并反推模块 AO 电压 | 不是 ppm，也不能直接说明某种气体浓度 |
| 人体活动检测 | PIR | 判断是否检测到人体活动 | 只能提供数字状态 |
| 人体存在检测 | Rd-03 V2 `OT2` | 判断雷达是否输出有人状态 | 只提供数字高低电平 |
| 人体存在、距离与距离门能量 | Rd-03 V2 UART | 通过 USART3 RX DMA 解析雷达有人/无人、目标距离和 32 个距离门能量 | 当前仅在调试串口和 TFT 输出，尚未上传到 MQTT |
| 雷达空间特征 | Rd-03 V2 UART | zone、peak gate、energy sum、motion score、occupied/still seconds | 代码已实现，待上板观察和房间标定 |
| 运行状态提示 | 板载 LED | 每 500 ms 翻转，表示主循环仍在运行 | 不是告警灯 |
| 调试信息查看 | USART1 / ST-LINK VCP | 在 COM6 查看启动、采集、事件和发送日志 | 主要用于开发调试 |

### 2.2 当前代码已经具备的联网能力

STM32 当前每 2 秒生成一帧状态数据：

```text
seq,temperature,humidity,gas,presence,risk
```

数据通过 `USART2 TX DMA` 发送给 ESP8266。ESP8266 能够：

- 按行接收并解析 STM32 的 CSV 状态。
- 校验字段数量、`presence` 和 `risk` 范围。
- 将 CSV 转换为 MQTT JSON。
- 连接 Wi-Fi 和 Mosquitto。
- 发布到 `eldercare/node01/status`。
- 在 Wi-Fi 或 MQTT 断开后尝试重连。

Home Assistant 已预置以下 MQTT 实体：

- Temperature
- Humidity
- Gas
- Presence
- Risk
- Event

### 2.3 当前产品对外展示的数据

| 字段 | 当前含义 |
| --- | --- |
| `temperature` | BME280 温度，单位摄氏度 |
| `humidity` | BME280 相对湿度，单位 `%` |
| `gas` | MQ 模块 AO 反推电压，单位近似为 mV，不是气体浓度 |
| `presence` | 当前代码会将 PIR、Rd-03 OT2、有效雷达 UART 状态三者合并 |
| `risk` | 临时风险等级，尚不是正式看护规则 |
| `event` | ESP8266 根据 `risk` 转换出的文本：`normal/notice/warning/alarm` |

## 3. 当前产品逻辑

```text
BME280                 -> 温度、湿度、气压
MQ 模块 AO             -> 模拟电压变化
PIR                    -> 人体活动数字状态
Rd-03 OT2              -> 人体存在数字状态
Rd-03 UART             -> USART3 RX DMA 接收状态、距离、32 个距离门能量
          |
          v
STM32 SensorMvp        -> 汇总 temperature / humidity / gas / presence
          |
          v
临时风险判断            -> risk 0~3
          |
          v
USART2 TX DMA          -> CSV 状态帧
          |
          v
ESP8266                -> CSV 转 MQTT JSON
          |
          v
Mosquitto              -> eldercare/node01/status
          |
          v
Home Assistant         -> 基础状态实体显示
```

当前临时风险判断非常简单：

```text
gas >= 3000 -> risk 3
gas >= 2000 -> risk 2
检测到有人  -> risk 1
其他情况    -> risk 0
```

这只是为了验证数据链路，不是合理的老人看护风险模型。检测到有人本身不应天然代表风险。

## 4. 功能状态分级

### 4.1 已验证，可以当作当前产品能力

- BME280 温度、湿度、气压采集。
- MQ 模块 AO 模拟量采集。
- PIR 人体活动数字检测。
- Rd-03 V2 `OT2` 人体存在数字检测。
- Rd-03 V2 UART 上报模式、目标距离和 32 个距离门能量解析。
- Rd-03 V2 雷达特征层 V1：zone、峰值距离门、能量总量、运动评分、持续有人/静止时间。
- STM32 调试串口日志。
- 板载 LED 心跳。
- ESP8266 到 Mosquitto 到 Home Assistant 的假数据链路。

### 4.2 代码已实现，但还不能当作已验收产品能力

- STM32 真实传感器状态通过 USART2 DMA 发送给 ESP8266。
- ESP8266 解析 STM32 CSV 并发布真实 MQTT JSON。
- 真实传感器数据在 Home Assistant 中随现场变化。
这些功能需要一次完整的上板验收，并在 `Docs/debug_report.md` 中留下日志或截图记录。

### 4.3 尚未实现

- 正式的看护风险状态机和事件码。
- 基于持续时间的异常判断，例如长时间无人活动、气体持续升高。
- MQ 模块标定、气体浓度或可靠告警阈值。
- 本地蜂鸣器告警。
- 2.0 英寸 TFT 本地显示上板验收；SPI1、控制 GPIO、ST7789 默认驱动和状态页代码已实现。ILI9341 已上板试验且画面更差，当前回到 ST7789 路线；纯色诊断确认长连续填充会失步，已改为分块写入，后续复验正式页面并微调坐标/颜色。
- MQTT `event`、`alarm` 独立事件上报。
- Home Assistant 自动化告警、通知和展示面板整理。
- 跌倒检测、生命体征检测、短信通知、云端 AI 分析。

## 5. Simon 已完成的主要工作

Simon 已经将项目从“只有传感器采集”推进到“具备联网链路代码”的阶段：

1. 实现 `Modules/comm/comm_wifi.*`，负责 STM32 状态帧格式化、序号、发送队列和 USART2 TX DMA 发送。
2. 在 STM32 主循环中每 2 秒取得采集状态并发送给 ESP8266。
3. 为 ESP8266 D1 mini 编写 UART CSV 接收、字段校验、JSON 转换、Wi-Fi 连接、MQTT 发布和重连逻辑。
4. 建立 Mosquitto 与 Home Assistant 的 Docker Compose 配置。
5. 在 Home Assistant 中预置温度、湿度、气体、人体存在、风险和事件实体。
6. 完成 ESP8266 假数据到 Mosquitto 再到 Home Assistant 的链路验证。
7. 补充浮点格式化支持，使 STM32 能发送温湿度小数。

## 6. 当前阶段判断

项目目前位于 MVP Phase 1 的中后段：

```text
传感器采集：已基本完成
通信组件：已基本实现
服务器基础：已完成
真实数据全链路验收：待完成
产品风险逻辑：未完成
本地与远程告警：未完成
```

现在最重要的不是继续堆更多传感器，而是把已有能力变成一个真正可演示、可解释、可重复验收的闭环。

当前可直接执行的上板步骤见 `Docs/acceptance_1_2.md`。

## 7. 推荐下一步

1. 完成真实数据全链路验收：让现场温度、MQ 数值和人体存在状态变化能够在 Home Assistant 中同步变化，并记录日志与截图。
2. 决定哪些雷达数据需要上传：建议先上传 `radar_presence` 和 `radar_cm`，32 个距离门能量保留为调试与后续算法输入。
3. 将 `Build_PlaceholderRisk()` 替换为独立的风险模块，先定义 2~3 个能演示的真实场景。
4. 对 MQ 模块进行基线采集和阈值实验，避免把电压值误称为气体浓度。
5. 接入蜂鸣器，保证网络断开时高风险仍有本地告警。
6. 增加 MQTT 事件上报和 Home Assistant 自动化，让状态变化真正触发告警展示。
