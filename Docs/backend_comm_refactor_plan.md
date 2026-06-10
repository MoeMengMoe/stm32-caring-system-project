# 后端与通信层重构开发文档

## 文档目的

本文档用于记录项目下一阶段的开发基线，覆盖以下内容：

- 在当前已完成 MVP 基础上的最小改动原则
- STM32、Wemos(ESP8266)、MQTT、Home Assistant、后端分析服务之间的职责划分
- 四路继电器的云端控制闭环
- 后端 `analysis_service` 的开发流程
- Home Assistant 面板搭建建议

本文档不替代现有文档：

- `Docs/protocol.md`：当前 UART / MQTT 协议现状
- `Docs/comm_wifi_interface.md`：STM32 `comm_wifi` 模块边界
- `Docs/simon_comm_server_plan.md`：通信链路与服务器原始规划

本文件用于指导后续重构实施与任务拆分。

## 当前已完成基线

截至 2026-06-08，项目已有基础能力如下：

- STM32 侧已完成主要传感器采集
- STM32 已通过 `USART2 TX DMA` 输出状态帧
- 当前 STM32 -> ESP8266 使用 CSV 行协议
- ESP8266 已能解析 UART CSV 并转换为 MQTT JSON
- Mosquitto 已通过 Docker 运行
- Home Assistant 已接入 `eldercare/node01/status`
- MQTT 状态上报主 topic 已可展示温湿度、气体、presence、risk、event

当前有效状态上报链路：

```text
STM32 sensors
  -> USART2 CSV
  -> Wemos / ESP8266
  -> MQTT JSON
  -> Mosquitto
  -> Home Assistant
```

因此，下一阶段应遵循“在现有链路上增量重构”的原则，而不是推倒重来。

## 重构目标

下一阶段重构目标如下：

1. 保留当前 `status` 主上报链路，避免一次性大改 STM32 已稳定部分。
2. 对 Wemos 通信层进行模块化重构，使其从“串口转 MQTT 脚本”升级为“通信网关”。
3. 引入四路继电器控制闭环，支持 Home Assistant 或云端远程操作。
4. 新增后端分析服务，对 MQTT 数据做规则判断和大模型辅助分析。
5. 将分析结果回传到 MQTT，并在 Home Assistant 中展示。

## 总体架构

建议的下一阶段总体架构如下：

```text
STM32
  -> UART status frame
  -> Wemos / ESP8266 gateway
  -> MQTT Broker (Mosquitto)
  -> Home Assistant
  -> analysis_service

Home Assistant
  -> MQTT relay command
  -> Wemos / ESP8266
  -> STM32 or direct relay driver
  -> MQTT relay state
  -> Home Assistant
```

## 最小改动原则

本次开发必须遵循以下原则：

1. 不重做 STM32 当前稳定的状态上报格式。
2. 不打断现有 `eldercare/node01/status` 的 HA 展示链路。
3. 新功能优先通过新增 topic 扩展，而不是修改现有 topic 语义。
4. Wemos 作为 MQTT 网关承担主要改造工作。
5. 后端分析服务只做分析、记录、建议和通知决策，不直接替代设备底层控制。

## 分层职责

### STM32

职责：

- 采集本地传感器数据
- 生成状态帧
- 维护本地基础风险等级 `risk`
- 接收来自 Wemos 的下行控制命令
- 执行继电器动作或本地提示动作
- 回传执行结果或最终状态

本阶段建议：

- 保留当前 `status` CSV 上报格式
- 新增下行命令接收与执行接口
- 为四路继电器预留控制接口

### Wemos / ESP8266

职责：

- 串口读取 STM32 状态帧
- 解析并转换为 MQTT JSON
- 订阅 MQTT 下行控制 topic
- 将继电器控制命令转发到 STM32 或直接驱动继电器
- 发布继电器状态到 MQTT
- 管理 Wi-Fi / MQTT 自动重连

本阶段建议：

- 将 Wemos 代码按模块重构
- 明确区分“上行状态”、“下行命令”、“状态回传”

### Mosquitto

职责：

- 作为系统 MQTT 总线
- 连接设备、Home Assistant、analysis_service

### Home Assistant

职责：

- 展示实时状态
- 展示后端分析结果
- 提供四路继电器开关界面
- 做自动化通知与告警编排

### analysis_service

职责：

- 订阅设备状态
- 存储原始数据
- 执行规则分析
- 必要时调用大模型
- 发布分析结果
- 输出通知建议

## MQTT Topic 规划

### 保留现有 topic

以下 topic 已存在或已预留，继续沿用：

| Topic | 用途 | 备注 |
| --- | --- | --- |
| `eldercare/node01/status` | 周期状态上报 | 当前链路已实现 |
| `eldercare/node01/event` | 设备独立事件上报 | 可继续预留 |
| `eldercare/node01/alarm` | 独立告警事件 | 当前协议中已预留 |

### 新增后端分析 topic

| Topic | 用途 |
| --- | --- |
| `eldercare/node01/analysis` | 后端分析结果 |
| `eldercare/node01/command` | 后端通用回传命令或本地提示建议 |
| `eldercare/node01/notify` | 通知执行结果或通知决策展示 |

### 新增四路继电器 topic

建议每路继电器使用独立 `set/state` topic：

| Topic | 用途 |
| --- | --- |
| `eldercare/node01/relay/1/set` | 下发继电器 1 命令 |
| `eldercare/node01/relay/1/state` | 回传继电器 1 当前状态 |
| `eldercare/node01/relay/2/set` | 下发继电器 2 命令 |
| `eldercare/node01/relay/2/state` | 回传继电器 2 当前状态 |
| `eldercare/node01/relay/3/set` | 下发继电器 3 命令 |
| `eldercare/node01/relay/3/state` | 回传继电器 3 当前状态 |
| `eldercare/node01/relay/4/set` | 下发继电器 4 命令 |
| `eldercare/node01/relay/4/state` | 回传继电器 4 当前状态 |

后续如需更完整闭环，可增加：

| Topic | 用途 |
| --- | --- |
| `eldercare/node01/relay/1/result` | 继电器 1 命令执行结果 |
| `eldercare/node01/relay/2/result` | 继电器 2 命令执行结果 |
| `eldercare/node01/relay/3/result` | 继电器 3 命令执行结果 |
| `eldercare/node01/relay/4/result` | 继电器 4 命令执行结果 |

MVP 后续阶段可选做 `result`，本阶段最小闭环只要求 `set/state`。

## 当前状态上报格式

为减少 STM32 改动，当前 `status` payload 保持不变：

```json
{
  "node_id": "node01",
  "seq": 12,
  "temperature": 25.6,
  "humidity": 61.0,
  "gas": 120,
  "presence": 1,
  "risk": 2,
  "event": "warning"
}
```

说明：

- `risk` 仍作为 STM32 本地基础风险等级
- `event` 仍由 Wemos 根据 `risk` 或本地规则生成
- 本阶段不强制扩展 `fall`、`heart_rate`、`spo2` 等字段

## 分析结果回传格式

建议后端发布到 `eldercare/node01/analysis` 的 payload 如下：

```json
{
  "node_id": "node01",
  "source_risk": 2,
  "cloud_risk": 3,
  "risk_score": 85,
  "summary": "燃气持续偏高，建议立即检查通风和阀门",
  "model_used": true,
  "need_family_notice": true,
  "need_community_notice": false,
  "need_hospital_notice": false
}
```

字段说明：

| 字段 | 含义 |
| --- | --- |
| `source_risk` | STM32 当前上报的本地风险 |
| `cloud_risk` | 后端综合分析后的风险等级 |
| `risk_score` | 0-100 风险分值 |
| `summary` | 一句话分析结论 |
| `model_used` | 是否调用了大模型 |
| `need_family_notice` | 是否建议通知亲人 |
| `need_community_notice` | 是否建议通知社区 |
| `need_hospital_notice` | 是否建议通知医院 |

## 四路继电器控制格式

### 下发命令

建议所有 `relay/x/set` 统一使用如下 payload：

```json
{
  "request_id": "cmd-001",
  "action": "ON"
}
```

约束：

- `action` 只允许 `ON` 或 `OFF`
- `request_id` 由控制端生成，用于日志与结果追踪

### 状态回传

建议所有 `relay/x/state` 统一使用如下 payload：

```json
{
  "relay_id": 1,
  "state": "ON"
}
```

约束：

- `relay_id` 取值 `1-4`
- `state` 只允许 `ON` 或 `OFF`

### 云控权限字段

STM32 后续状态帧会预留 `cloud_perm_mask` 字段，用于表示四路继电器是否允许云端控制。bit0-bit3 分别对应继电器 1-4。

当前阶段约定：

- `cloud_perm_mask` 默认值为 `15`，表示四路继电器默认允许云控。
- 该字段只作为主控后续权限策略接口保留。
- Home Assistant 面板和服务器展示暂不使用该字段，也不根据该字段隐藏或禁用开关。
- 云控是否最终生效以 STM32 的 `relay/x/result` 和 `relay/x/state` 回传为准。

## 继电器控制闭环

四路继电器必须遵循以下闭环流程：

```text
Home Assistant / cloud
  -> publish relay/x/set
  -> Wemos receive command
  -> Wemos direct control relay or forward to STM32
  -> device executes action
  -> device publishes relay/x/state
  -> Home Assistant refreshes switch state
```

关键要求：

1. 不能只做命令下发，不做状态回传。
2. HA 页面显示必须以 `state` topic 为准，而不是以“最后一次按钮点击”推断。
3. Wemos 断线重连后，需尽快重新同步继电器状态。

## Wemos 通信层重构建议

Wemos 需要从当前单体逻辑重构为几个明确模块：

### 1. UART 接收解析模块

职责：

- 接收 STM32 串口数据
- 按行切分
- 校验长度与格式
- 生成内部状态对象

### 2. MQTT 发布模块

职责：

- 将解析后的状态发布到 `eldercare/node01/status`
- 在需要时发布 `event`、`alarm`
- 发布四路继电器 `state`

### 3. MQTT 订阅模块

职责：

- 订阅四路 `relay/x/set`
- 订阅后端 `command`
- 解析控制指令

### 4. 命令分发模块

职责：

- 将 MQTT 下行指令路由到本地执行逻辑
- 若继电器挂在 STM32，则通过 UART 下发命令
- 若继电器直接挂在 Wemos，则直接控制 GPIO

### 5. 连接管理模块

职责：

- Wi-Fi 自动重连
- MQTT 自动重连
- 重连后重新订阅控制 topic

## STM32 通信层改造建议

在 `Modules/comm/comm_wifi.*` 现有边界基础上，建议新增以下能力：

1. 保留现有状态帧发送能力
2. 新增 UART 下行命令接收处理接口
3. 为四路继电器控制提供统一调用接口
4. 支持回传继电器最终状态

如果继电器实际由 STM32 控制，STM32 需要至少支持：

- `relay_set(relay_id, action)`
- `relay_get_state(relay_id)`
- 执行结果或状态回传

如果继电器直接挂在 Wemos，则 STM32 侧改动可减少，仅保留状态采集与本地风险判断。

## 后端开发指南

### 技术栈建议

为减少复杂度，建议使用：

- Python
- `paho-mqtt`
- SQLite
- OpenAI SDK 或兼容 OpenAI 的 HTTP API
- Docker

### 目录建议

建议新增目录：

```text
server/
  analysis_service/
    Dockerfile
    requirements.txt
    .env.example
    src/
      main.py
      config.py
      mqtt_client.py
      rules_engine.py
      llm_service.py
      notifier.py
      repository.py
      schemas.py
      prompts.py
```

### 后端职责边界

`analysis_service` 只负责：

1. 订阅 `eldercare/node01/status`
2. 存储原始状态
3. 执行规则分析
4. 必要时调用大模型
5. 发布 `eldercare/node01/analysis`
6. 必要时发布 `eldercare/node01/alarm`
7. 输出通知建议

`analysis_service` 不负责：

- 替代 MQTT Broker
- 直接驱动继电器硬件
- 替代 HA 做控制页面

### 数据库存储建议

最小表结构建议如下：

| 表名 | 用途 |
| --- | --- |
| `raw_status` | 原始状态记录 |
| `analysis_results` | 分析结果记录 |
| `relay_states` | 四路继电器状态记录 |
| `notification_logs` | 通知执行记录 |

### 后端开发顺序

建议严格按以下顺序推进：

1. 实现 MQTT 订阅 `status`
2. 实现 SQLite 落库
3. 实现规则引擎
4. 发布 `analysis` topic
5. 接入 Home Assistant 展示
6. 仅在异常条件下接入大模型
7. 接入通知通道

### 大模型调用策略

不建议每一条状态都调用大模型。

推荐触发条件：

- `risk >= 2`
- `gas` 连续超阈值
- 多个关键指标同时异常
- 持续长时间无人活动且环境异常

模型输出必须限制为结构化 JSON，不能允许自由文本主导核心流程。

### 通知决策建议

建议由后端输出三个布尔建议字段：

- `need_family_notice`
- `need_community_notice`
- `need_hospital_notice`

Home Assistant 再根据这些建议字段和自动化规则触发实际通知。

## Home Assistant 面板搭建建议

Home Assistant 只做三类事情：

1. 展示实时状态
2. 展示分析结果
3. 提供继电器开关控制

不建议把复杂业务逻辑写在 HA 中。

### 建议实体分组

#### 1. 实时状态区

- 温度
- 湿度
- 燃气
- 人体存在
- 本地风险等级
- 当前事件

#### 2. AI 分析区

- 云端风险等级
- 风险分值
- 分析摘要
- 是否建议通知亲人
- 是否建议通知社区
- 是否建议通知医院

#### 3. 继电器控制区

- 继电器 1 开关
- 继电器 2 开关
- 继电器 3 开关
- 继电器 4 开关

#### 4. 告警记录区

- 当前告警状态
- 最近分析结果
- 最近一次继电器状态变化

### 面板布局建议

建议采用以下布局：

1. 第一行：核心环境状态卡
2. 第二行：风险与分析卡
3. 第三行：四路继电器控制卡
4. 第四行：告警与日志卡

### MQTT 实体建议

在现有 `server/homeassistant/config/configuration.yaml` 基础上，下一步建议增加：

- `mqtt sensor`：analysis 相关字段
- `mqtt switch`：四路继电器
- `mqtt binary_sensor`：通知建议或告警状态

## 分阶段实施计划

### 阶段 1：协议冻结

输出：

- 确认 `status` 字段不变
- 确认四路继电器 topic
- 确认 `analysis` payload

### 阶段 2：Wemos 网关重构

输出：

- 上行状态稳定发布
- 下行四路继电器命令可订阅
- 四路状态可回传

### 阶段 3：HA 控制闭环

输出：

- 四路继电器 UI 开关可用
- 状态能实时刷新
- `status` 与 `analysis` 可视化

### 阶段 4：后端分析服务

输出：

- MQTT 订阅与存储
- 规则分析
- 分析结果发布

### 阶段 5：大模型与通知

输出：

- 异常场景调用大模型
- 亲人 / 社区 / 医院通知建议
- HA 自动化联动

## 风险与注意事项

1. 不要在协议尚未冻结前开发大量 HA 自动化。
2. 不要让后端直接承担继电器控制主逻辑，控制主链路仍应保持 MQTT 闭环。
3. 不要在 STM32 尚未明确下行协议前大规模重写 `comm_wifi` 模块。
4. 若继电器最终挂在 Wemos，需重新评估 GPIO、电源与上电默认状态。
5. 若继电器最终挂在 STM32，则 Wemos 必须支持稳定的 UART 下行命令转发。

## 建议的下一步落地动作

建议按以下顺序执行：

1. 更新 `Docs/protocol.md`，补充 `analysis` 和四路继电器 MQTT topic
2. 明确四路继电器实际接在 STM32 还是 Wemos
3. 重构 Wemos 通信层模块
4. 更新 Home Assistant MQTT 实体配置
5. 新建 `server/analysis_service/` 代码骨架

在上述动作完成前，不建议并行开发复杂前端或额外云端控制页面。
