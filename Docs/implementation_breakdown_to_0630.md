# 6.30 实现拆解与模块任务清单

更新时间：2026-06-16

## 1. 当前结论

当前项目代码不需要推倒重来。最小改动路线是：

- 主控侧保留现有采集、显示驱动、ESP8266 通信链路，在其上新增一层 `app` 业务层。
- 云端侧保留 Mosquitto、Home Assistant、analysis_service 和 SQLite 基础，在其旁边补事件存储、展示面板和控制面板。
- 精确检测、复杂定位、Wi-Fi/BLE 辅助位置判断全部降到后续增强，不进入 6.30 必须完成范围。

当前 6.30 前的唯一主目标：

```text
围绕三场景做成稳定的在线 IoT 闭环展示系统
```

三场景为：

1. 主动求助 / 模拟跌倒后的闭环响应
2. 长时间静止无响应的异常确认与升级
3. 断网或市电异常下的本地自治与日志补传

## 2. 现有代码资产评估

### 2.1 主控侧可复用资产

| 模块 | 文件 | 当前能力 | 处理方式 |
| --- | --- | --- | --- |
| 主循环 | `Core/Src/main.c` | 初始化、周期采集、显示刷新、状态上报 | 保留框架，抽出业务判断 |
| 传感器聚合 | `Modules/sensor/sensor_mvp.*` | 温湿度、气体、presence、雷达特征聚合 | 复用 |
| 雷达驱动 | `Modules/sensor/rd03_v2.*` | UART DMA 接收、presence、distance、gate energy | 复用，不深挖精确检测 |
| 雷达特征 | `Modules/sensor/radar_features.*` | zone、motion score、still seconds | 只作为久静辅助 |
| TFT 驱动 | `Modules/display/tft_lcd.*` | ST7789/TFT 底层绘制 | 复用 |
| 状态显示 | `Modules/display/status_display.*` | 传感器状态页 | 微调为场景状态页 |
| Wi-Fi 通信 | `Modules/comm/comm_wifi.*` | 状态帧、继电器命令、继电器结果 | 复用并扩展事件上报 |

### 2.2 主控侧不适配点

| 位置 | 问题 | 处理方式 |
| --- | --- | --- |
| `Build_PlaceholderRisk()` | 只是临时气体/presence 风险，不符合三场景闭环 | 删除或停止使用 |
| `main.c` | 业务判断、发送、显示都集中在主循环 | 拆成 `app` 层模块 |
| 显示层 | 当前偏传感器数值展示 | 改成场景、风险、确认、离线状态展示 |
| 日志 | 当前无事件日志缓存 | 新增环形事件日志 |
| 输入 | 当前无遥控/按钮确认统一事件接口 | 新增输入事件模块 |
| 离线 | 当前无网络状态机和补传概念 | 新增离线标记与待补传事件队列 |

### 2.3 云端侧可复用资产

| 模块 | 文件 | 当前能力 | 处理方式 |
| --- | --- | --- | --- |
| Mosquitto | `server/docker-compose.yml` | MQTT broker | 复用 |
| HA 配置 | `server/homeassistant/config/configuration.yaml` | status/analysis/relay 实体 | 复用并补实体 |
| MQTT ingest | `analysis_service/src/mqtt_client.py` | 订阅 status，落库，发布 analysis/alarm | 扩展为事件处理 |
| SQLite 仓库 | `analysis_service/src/repository.py` | raw_status、analysis、notification 表 | 增加 event/log 表和查询 |
| 规则分析 | `analysis_service/src/rules_engine.py` | 基于 status 的云端风险评分 | 保留为辅助 |
| 通知决策 | `analysis_service/src/notifier.py` | family/community/hospital 决策日志 | 保留 |

### 2.4 云端侧不适配点

| 位置 | 问题 | 处理方式 |
| --- | --- | --- |
| `analysis_service` | 只处理周期状态，不处理明确事件生命周期 | 增加 event ingest 和 event table |
| HA 页面 | 只是实体列表，不适合比赛展示 | 新增 fancy 展示面板 |
| 控制 | 只能通过 HA switch 零散控制 | 新增简洁控制面板 |
| CRUD | 当前没有 HTTP API，不利于自定义面板 | 新增 dashboard API 服务或扩展服务 |
| 米家联动 | 尚未纳入三场景展示逻辑 | 放到 HA automation 层 |

## 3. 最小改动架构

### 3.1 主控最小改动架构

```text
SensorMvp_Update()
  -> AppInput_Update()
  -> AppEvent_Push(...)
  -> AppStateMachine_Update(...)
  -> AppAction_Apply(...)
  -> AppLog_Append(...)
  -> CommWifi_SendStatusV2(...)
  -> CommWifi_SendEvent(...)   # 新增，若时间不足可先复用 status.event
```

原则：

- 不改 HAL 自动生成区
- 不改 `.ioc`
- 不重写传感器驱动
- `main.c` 只做调度和模块连接
- 新业务代码放进 `Modules/app/`

### 3.2 云端最小改动架构

```text
STM32 -> ESP8266 -> MQTT
                  -> analysis_service 存储 status/event
                  -> Home Assistant 基础实体和自动化
                  -> dashboard API 查询 SQLite / 发布 MQTT 控制命令
                  -> fancy 展示面板
                  -> 简洁控制面板
```

原则：

- 保留现有 Docker Compose
- 新面板独立为 `server/dashboard`，不要塞进 HA 配置
- 控制面板通过 MQTT 发布命令，不直接绕过系统链路改数据库
- 数据查询通过 API 读 SQLite，不让前端直接读数据库

## 4. 主控模块拆分

建议新增目录：

```text
Modules/app/
  app_types.h
  app_input.c/.h
  app_state_machine.c/.h
  app_action.c/.h
  app_log.c/.h
  app_status.c/.h
```

### 4.1 `app_types`

职责：

- 定义三场景通用枚举、状态、事件、日志结构

建议类型：

```text
AppState:
  NORMAL
  NOTICE
  ACK_WAIT
  ALARM
  NO_RESPONSE
  OFFLINE

AppEventType:
  REMOTE_TRIGGER
  SOS_BUTTON
  LONG_STILL
  USER_ACK
  ACK_TIMEOUT
  CLEAR_ALARM
  NETWORK_LOST
  NETWORK_RESTORED

AppScenario:
  NONE
  SOS_OR_FALL_SIM
  LONG_STILL_NO_RESPONSE
  OFFLINE_AUTONOMY
```

最小改动：

- 只新增 `.h`，不依赖 HAL
- 让主控、日志、显示、通信共用同一组枚举

### 4.2 `app_input`

职责：

- 统一遥控、按钮、传感器久静、网络状态输入

输入来源：

- 遥控触发：场景一
- 本地确认按钮：确认/清除
- 雷达 `still_seconds`：场景二
- Wi-Fi/MQTT 状态：场景三

最小实现：

- 先实现软件触发函数，方便调试
- 再接真实遥控和按钮

接口建议：

```text
AppInput_Init()
AppInput_Update(sensor_status, now_ms)
AppInput_PollEvent(event)
AppInput_InjectRemoteTrigger()
AppInput_InjectAck()
```

### 4.3 `app_state_machine`

职责：

- 管理三场景共用状态流转
- 处理确认超时
- 输出当前风险等级和动作请求

核心流转：

```text
NORMAL
  -> ACK_WAIT
  -> NORMAL        # 用户确认
  -> ALARM         # 超时未确认
  -> NO_RESPONSE   # 长时间无响应
```

最小实现：

- 不做复杂检测
- 遥控触发直接进入 `ACK_WAIT`
- 久静触发进入 `ACK_WAIT`
- 离线事件只改变网络标记，不阻断本地状态机

### 4.4 `app_action`

职责：

- 根据状态机输出控制本地动作

动作：

- 蜂鸣器
- 语音提示
- 继电器
- 警示灯
- TFT 状态提示

最小实现：

- 先接继电器状态 mask 和蜂鸣器/LED
- 语音模块可以后补

### 4.5 `app_log`

职责：

- 保存最近事件
- 标记是否已上传
- 支持恢复联网后补传

数据结构：

```text
event_id
scenario
event_type
state_before
state_after
trigger_source
start_ms
ack_ms
clear_ms
result
network_state
uploaded
```

最小实现：

- 固定长度环形缓冲
- 不用动态内存
- 先保存最近 16 条

### 4.6 `app_status`

职责：

- 汇总传感器状态、业务状态、继电器状态、网络状态
- 提供给 TFT 和通信层

输出：

- `risk`
- `event`
- `scenario`
- `relay_state_mask`
- `network_state`
- `pending_log_count`

## 5. 主控重构范围

### 5.1 必须改

- `Core/Src/main.c`
  - 删除或停止使用 `Build_PlaceholderRisk()`
  - 增加 app 层初始化和 update
  - `Send_Status_ToWifi()` 改为读取 `app_status`
  - 轮询云端继电器命令并回传结果

- `CMakeLists.txt`
  - 加入 `Modules/app/*.c`
  - 加入 `Modules/app` include path

- `Modules/display/status_display.*`
  - 从传感器页改为场景状态页
  - 最少显示：状态、场景、risk、是否等待确认、网络状态、最近事件

### 5.2 可先不改

- `sensor_mvp.*`
- `rd03_v2.*`
- `radar_features.*`
- `bme280.*`
- `tft_lcd.*`
- `comm_wifi.*` 的已有状态帧

### 5.3 可选改

- `comm_wifi.*`
  - 新增 `CommWifi_SendEvent(...)`
  - 新增离线/补传 event 帧
  - 若时间不足，可先把 event 压缩进 status 的 `risk/event`

## 6. 云端模块拆分

建议新增：

```text
server/dashboard/
  backend/
    app.py
    repository.py
    mqtt_commands.py
    schemas.py
  frontend/
    ...
```

如果时间紧，可先做一个单体前端 + 简单 API，后续再拆。

### 6.1 `analysis_service` 扩展

职责：

- 继续订阅 `eldercare/node01/status`
- 新增订阅 `eldercare/node01/event`
- 存储事件日志
- 发布 `eldercare/node01/alarm`

最小改动：

- `schemas.py` 增加 `EventPayload`
- `repository.py` 增加 `event_logs` 表
- `mqtt_client.py` 支持多 topic 订阅

### 6.2 `dashboard backend`

职责：

- 给 fancy 展示面板和控制面板提供 HTTP API
- 查询 SQLite
- 发布 MQTT 控制命令

推荐技术：

- `FastAPI`
- `sqlite3`
- `paho-mqtt`

原因：

- 和现有 Python 栈一致
- 改动小
- 适合快速 CRUD

### 6.3 `dashboard frontend`

职责：

- fancy 展示面板
- 简洁控制面板

推荐最小路线：

- `Vite + React`
- 或先用单页 HTML/JS 快速实现

若追求现场观感：

- 展示面板用 React
- 控制面板保持简单、按钮明确

## 7. 云端 CRUD 拆分

### 7.1 数据实体

#### `DeviceStatus`

来源：`eldercare/node01/status`

字段：

- `id`
- `received_at`
- `node_id`
- `seq`
- `temperature`
- `humidity`
- `gas`
- `presence`
- `risk`
- `event`
- `relay_state_mask`
- `cloud_perm_mask`

CRUD：

- `Create`：MQTT ingest 自动写入
- `Read`：面板读取最新状态、状态历史
- `Update`：不需要
- `Delete`：演示阶段不需要，可后续做清理

#### `EventLog`

来源：STM32 event 或云端生成

字段：

- `id`
- `created_at`
- `node_id`
- `event_id`
- `scenario`
- `event_type`
- `trigger_source`
- `state_before`
- `state_after`
- `risk`
- `result`
- `network_state`
- `power_state`
- `payload_json`

CRUD：

- `Create`：MQTT event ingest / 控制面板模拟触发
- `Read`：展示面板时间线、最近事件列表
- `Update`：确认、清除、补传结果
- `Delete`：控制面板可提供“清空演示日志”，仅限演示环境

#### `AlarmState`

来源：状态机和云端分析

字段：

- `node_id`
- `active`
- `scenario`
- `risk`
- `started_at`
- `ack_at`
- `cleared_at`
- `current_message`

CRUD：

- `Create`：触发告警
- `Read`：展示面板读取当前告警
- `Update`：确认、升级、清除
- `Delete`：不物理删除，清除即更新状态

#### `RelayState`

来源：`relay/x/state` 和 `relay/x/result`

字段：

- `node_id`
- `relay_id`
- `state`
- `updated_at`
- `last_result`

CRUD：

- `Create`：首次收到继电器状态
- `Read`：展示面板和控制面板读取
- `Update`：继电器状态变化
- `Delete`：不需要

#### `DemoCommand`

来源：控制面板

字段：

- `command_id`
- `command_type`
- `target`
- `payload`
- `created_at`
- `result`

CRUD：

- `Create`：控制面板发起命令
- `Read`：查看最近命令结果
- `Update`：命令执行结果回填
- `Delete`：演示阶段不需要

### 7.2 API 建议

最小 API：

```text
GET  /api/status/latest
GET  /api/events/recent
GET  /api/alarm/current
GET  /api/relays
POST /api/demo/trigger
POST /api/demo/ack
POST /api/demo/clear
POST /api/relay/{id}/set
POST /api/demo/network
DELETE /api/demo/logs
```

说明：

- `POST /api/demo/trigger` 用于控制面板主动触发三场景
- `POST /api/demo/network` 用于模拟离线/恢复
- 真机最终应通过 STM32 事件上报为准，控制面板只作为演示入口

### 7.3 MQTT topic 建议

保留：

```text
eldercare/node01/status
eldercare/node01/analysis
eldercare/node01/alarm
eldercare/node01/relay/1/set
eldercare/node01/relay/1/state
```

新增：

```text
eldercare/node01/event
eldercare/node01/demo/command
eldercare/node01/demo/state
```

若时间不足：

- 先只新增 `event`
- 控制面板直接发布到现有 `relay/x/set` 和新增 `demo/command`

## 8. 面板拆分

### 8.1 Fancy 展示面板

用户：评委、观众

目标：

- 一眼看懂系统处于哪个场景
- 一眼看懂是否告警、是否确认、是否联动
- 展示在线 IoT 闭环效果

内容：

- 当前场景
- 当前风险等级
- 设备在线状态
- 当前告警卡片
- 最近事件时间线
- 继电器状态
- 米家联动状态
- 离线补传状态
- 温湿度/气体基础数据

### 8.2 简洁控制面板

用户：演示人员

目标：

- 主动操作演示，不追求复杂视觉

按钮：

- 触发场景一
- 触发场景二
- 模拟离线
- 模拟恢复
- 用户确认
- 清除告警
- 继电器 1 开
- 继电器 1 关
- 继电器 2 开
- 继电器 2 关

规则：

- 控制面板不得作为主展示页
- 控制动作必须留下日志
- 控制命令优先走 MQTT，不直接改前端状态

## 9. 人员职责

参考 `report.md` 原有分工，更新如下。

### 9.1 Gary：主控负责人

负责范围：

- `Modules/app/*`
- `Core/Src/main.c` 调度改造
- 按钮、遥控、继电器、蜂鸣器接入
- 状态机与本地日志
- TFT 场景状态页

交付物：

- 场景一闭环可跑
- 场景二久静触发可跑
- 场景三离线本地自治可跑
- 主控日志可通过串口验证

不负责：

- 云端页面视觉
- 服务器部署
- 米家账号接入

### 9.2 Simon：通信与云端负责人

负责范围：

- ESP8266 MQTT 网关稳定性
- MQTT topic 设计落地
- `analysis_service` 扩展
- SQLite CRUD
- fancy 展示面板
- 简洁控制面板
- HA 自动化与米家联动
- Docker Compose 部署

交付物：

- status/event/alarm 数据链路
- 最近事件与告警记录可查
- fancy 展示面板可展示三场景
- 控制面板可主动触发、确认、清除和控制继电器

不负责：

- STM32 状态机内部实现
- 硬件接线最终确认

### 9.3 Ricky：硬件测试与审查

负责范围：

- 接线确认
- 继电器负载测试
- 蜂鸣器/警示灯/米家插座现场联调
- 场景演示稳定性测试
- 代码审查与调试记录

交付物：

- 三场景硬件验收记录
- 现场接线说明
- 演示失败点清单
- 6.29 彩排问题修复跟踪

## 10. 确切开发计划

### 6.16 - 6.17：设计冻结

Gary：

- 定 `app_types`
- 定状态机状态和事件
- 定主控日志字段

Simon：

- 定云端 CRUD 表
- 定新增 topic
- 定展示面板和控制面板字段

Ricky：

- 确认遥控、按钮、继电器、灯、风扇接线可测

验收：

- 本文档内容冻结
- 不再新增主场景

### 6.18 - 6.20：场景一最小闭环

Gary：

- 新增 `app_input`
- 新增 `app_state_machine`
- 遥控/软件触发进入 `ACK_WAIT`
- 按钮确认和超时升级

Simon：

- `event_logs` 表
- `event` topic ingest
- 控制面板最小按钮：触发、确认、清除

Ricky：

- 验证触发、灯、蜂鸣器或替代输出

验收：

```text
触发场景一 -> 本地提示 -> HA/面板显示 -> 确认或超时 -> 日志可见
```

### 6.21 - 6.23：场景二复用接入

Gary：

- 基于 `radar_still_seconds` 做久静触发
- 复用场景一确认和超时逻辑
- TFT 显示场景二

Simon：

- 展示面板第一版
- 时间线显示场景一/二日志
- HA 自动化初版

Ricky：

- 设计可重复触发场景二的测试流程

验收：

```text
久静触发 -> 确认等待 -> 无响应升级 -> 云端日志和展示同步
```

### 6.24 - 6.26：场景三与离线补传

Gary：

- 网络状态标记
- 本地日志 `uploaded` 标记
- 恢复后补传最近关键事件

Simon：

- 离线/恢复展示
- `POST /api/demo/network`
- 补传日志在面板可见

Ricky：

- 验证断网时本地仍可触发、确认、升级

验收：

```text
断网 -> 本地异常闭环继续 -> 恢复网络 -> 面板看到补传事件
```

### 6.27 - 6.28：联动与视觉收口

Gary：

- 继电器动作稳定
- TFT 文案稳定
- 串口日志清晰

Simon：

- fancy 展示面板视觉收口
- 控制面板操作收口
- 米家联动通过 HA automation 跑通

Ricky：

- 全链路彩排
- 记录现场故障点

验收：

- 三场景连续演示无阻塞
- 控制面板可稳定辅助演示
- 展示面板能给评委观看

### 6.29：只修 bug

所有人：

- 不加新功能
- 不改主流程
- 只修影响演示的问题

验收：

- 三场景完整彩排至少 3 次

### 6.30：冻结交付

交付：

- 代码冻结
- 演示脚本冻结
- HA/面板截图
- 接线照片
- 已知问题清单
- 后续规划：低功耗、语音、精确检测

## 11. 风险与降级方案

### 11.1 主控风险

风险：事件帧来不及做。

降级：

- 继续用 status 的 `risk/event`
- 日志先只在主控串口和云端 status 表体现

风险：久静不稳定。

降级：

- 用控制面板或遥控模拟场景二触发
- 明确说明当前是事件流程演示

### 11.2 云端风险

风险：dashboard API 来不及做完整。

降级：

- 控制面板直接用 MQTT WebSocket 或脚本发布命令
- 展示面板先读 API mock + MQTT 最新状态

风险：米家联动不稳定。

降级：

- 用 HA 控制本地 MQTT relay 替代米家
- 米家作为后续生态联动展示

### 11.3 硬件风险

风险：继电器真实负载不稳定。

降级：

- 用板载 LED 或低压警示灯替代

风险：语音提示来不及。

降级：

- 用蜂鸣器 + TFT 文案替代

## 12. 最小完成标准

6.30 前最低必须做到：

- 场景一完整闭环
- 场景二可通过真实或模拟触发进入同一闭环
- 场景三可展示断网本地自治和恢复补传
- 云端能展示当前状态、最近事件、告警状态
- 控制面板能触发、确认、清除
- 继电器或灯能形成可见联动

达到以上标准，即可作为比赛版收尾。
