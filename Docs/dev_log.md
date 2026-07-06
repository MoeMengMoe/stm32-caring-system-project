# 开发日志

## 2026-05-15

- 确认项目定位：面向独居老人居家安全的边缘智能看护节点。
- 确认硬件平台：NUCLEO-U5A5ZJ-Q。
- 创建仓库目录和文档骨架。
- 导入 GitHub 下载的 STM32CubeMX/CMake 基础工程。
- 使用 STM32CubeCLT 完成 Debug 配置和编译验证。

## 2026-07-05

- 与 Simon 最新 `origin/exp` 对齐，当前远端最新提交为 `a6fdb8a`。
- 与 Simon 面对面确认当前冲刺边界：
  - 远端最新进度就是 `a6fdb8a`。
  - 新增硬件按“按钮/遥控、蜂鸣器、继电器、其他增强”的顺序推进。
  - 继电器按 4 路能力设计，现场负载先用 2 个 LED 做演示。
  - 场景一触发方式最终希望同时支持 dashboard、实体按钮、遥控和语音；当前代码已先支持 dashboard 下发 `D` 命令。
  - 服务器、Mosquitto、Home Assistant 和 dashboard 当前运行在 Simon 电脑上，ESP8266 的 `MQTT_HOST` 后续应指向 Simon 电脑的局域网 IP。
  - Simon 电脑当前局域网 IP 为 `192.168.10.149`，ESP8266 固件中的 `MQTT_HOST` 已改为该地址。
  - 本地实体按钮计划 2 个：一个用于主动求助/模拟跌倒，一个用于确认“我没事”。
  - 遥控触发暂不直连 STM32，由云端/dashboard 侧转为 `D` 演示命令下发。
  - 蜂鸣器第一版改用无源三针模块，`3.3V` 供电，`IO` 由 STM32 输出软件方波；有源低电平触发蜂鸣器作为后续备用方案。
  - 继电器实物为 HW-280 四路光耦隔离继电器驱动模块，控制端丝印为 `DC+ / DC- / IN1 / IN2 / IN3 / IN4`，板上支持高/低电平触发；第一版按高电平触发测试，GPIO 低电平释放、GPIO 高电平吸合。
- 新增 `Modules/app` 主控业务层第一版：
  - 定义三场景、状态、事件类型、触发源、结果、网络状态和供电状态枚举。
  - 实现固定长度事件日志环形缓冲。
  - 实现三场景共用状态机的第一版：远程触发、雷达久静触发、用户确认、清除、确认超时升级、模拟断网/恢复。
- 扩展 `Modules/comm/comm_wifi.*`：
  - 新增 `D` 演示命令解析，接收 dashboard/ESP8266 转发的场景触发、确认、清除和网络模拟命令。
  - 新增 `E` 事件帧发送，按 `Docs/protocol.md` 冻结协议上报关键业务事件。
  - 保留继电器 `C/R` 命令结果链路，主控侧先用 `relay_state_mask` 做软件状态回传。
- 修复 ESP8266 继电器 MQTT 回包缓冲区：
  - Simon 侧已经能收到 `eldercare/node01/status`，说明 STM32 -> ESP8266 -> MQTT 上行链路已通。
  - 继电器硬件尚未接入不会阻止云端命令闭环，当前 STM32 会用 `relay_state_mask` 做软件状态模拟并回 `R` 帧。
  - 实测下发继电器命令时出现 relay payload overflow，根因是 ESP8266 中 `relay/x/state` 的 JSON 字符串长度超过原先 `64` 字节缓冲区。
  - 将继电器状态 payload 缓冲区扩大到 `128` 字节，将继电器 result payload 缓冲区扩大到 `160` 字节，并补充 `relay result state payload overflow` 诊断日志。
  - 该修复只需要重新烧录 ESP8266 固件，不需要重新生成 CubeMX，也不需要重新烧 STM32。
- `Core/Src/main.c` 停止使用临时 `Build_PlaceholderRisk()`，状态上报和 TFT 显示改为读取 app 状态。
- `StatusDisplay` 最后一栏改为 `APP STATE`，用于显示 `SOS ACK`、`STILL ACK`、`ALARM`、`NO RESP`、`OFFLINE` 等场景状态。
- `cmake --build --preset Debug` 通过；未修改 `.ioc`，未修改引脚和接线。
- 新增 `Modules/board/board_io.*` 本地 IO 边界层：
  - 预留 `SOS_BUTTON` 与 `ACK_BUTTON` 两个按键输入，按下接地、内部上拉，带 40 ms 软件去抖。
  - `SOS_BUTTON` 触发 `APP_EVENT_SOS_BUTTON`，进入求助/模拟跌倒确认等待。
  - `ACK_BUTTON` 触发 `APP_EVENT_USER_ACK`，确认并清除当前告警；事件源记录为 `APP_TRIGGER_BUTTON`。
  - 预留 `BUZZER_IO` 无源蜂鸣器输出，当前用软件方波在 `ACK_WAIT`、`ALARM`、`NO_RESPONSE` 和高风险时发声。
  - `D0 / PG8 / SOS_BUTTON`、`D1 / PG7 / ACK_BUTTON`、`D6 / PE9 / BUZZER_IO` 已由 CubeMX 生成并上板验收。
  - 当前实体 6 脚自锁按钮暂不作为主验收方式；先使用短接 `D0 -> GND`、`D1 -> GND` 稳定触发。
  - 无源蜂鸣器已随 `ACK_WAIT`、`ALARM` 等状态发声；当前为软件方波，声音质量一般，后续可升级 TIM1 PWM。
- 新增 COM6 调试控制入口：
  - `s` 触发本地 SOS，`a` 触发本地 ACK，`c` 清除当前告警。
  - `1` / `2` 分别触发场景一和场景二，`o` / `n` 模拟网络离线和恢复。
  - `p` 打印当前传感器与 app 状态摘要，`h` 或 `?` 打印帮助。
  - 该入口用于现场排障和兜底演示，不改变正式 dashboard/云端/实体按钮路径。
- 接入 HW-280 四路继电器第一版：
  - 分配 `D3 / PE13 / RELAY1_IN`、`D4 / PF14 / RELAY2_IN`、`D5 / PE11 / RELAY3_IN`、`A5 / PC0 / RELAY4_IN`，CubeMX 待 Gary 配置并重新生成。
  - `Modules/board/board_io.c` 新增 `BoardIo_SetRelayMask()`，统一把 `relay_state_mask` 的 bit0-bit3 映射到四路继电器 GPIO。
  - 云端继电器命令和 COM6 本地调试命令共用 `Apply_Relay_Mask()`，保证硬件输出、app 状态和 MQTT 回传状态一致。
  - COM6 新增 `r/t/y/u`，分别切换继电器 1-4，用于不依赖云端的低压 LED 负载验收。
  - `cmake --build --preset Debug` 通过。
- HW-280 四路继电器已上板验证高电平触发可用，低压 LED 负载可以被 COM6 控制。
- 接入继电器本地自动联动：
  - 新增 `manual mask` 和 `auto mask` 两层继电器状态，最终输出为 `manual | auto`。
  - 继电器 1 作为护理告警灯，`ACK_WAIT / ALARM / NO_RESPONSE` 自动打开，ACK 或清除后自动释放。
  - 继电器 2 作为离线提示灯，网络离线时自动打开，网络恢复后自动释放。
  - 继电器 3/4 保持手动/云端控制预留。
  - 云端请求关闭被本地自动联动占用的继电器时，STM32 保持本地安全优先，回传最终输出状态。
  - 2026-07-06 联动修正：ACK/clear 现在会清空 `manual mask` 并立即刷新继电器输出，本地确认可关闭 Simon 云端或 COM6 手动打开的演示灯；若某一路仍被 `auto mask` 条件占用，例如网络离线灯，则继续保持打开。
  - `cmake --build --preset Debug` 通过。
- 上板复查 HW-280 继电器触发极性：
  - 负载端接法确认使用 `COM + NO`，目标现象为“继电器吸合时 LED 亮”。
  - 现场曾出现“继电器关闭 LED 常亮、继电器打开 LED 熄灭”，根因不是 NO/NC 接反，而是模块跳帽处在低电平触发位置。
  - 将跳帽改回 `H / High Level Trigger` 后，现象与当前代码一致：`GPIO Low = 释放`，`GPIO High = 吸合`。
  - 后续复刻接线时必须先确认跳帽位置，再判断代码极性。

## 2026-07-06

- MQ 展示口径从 AO mV 调试值升级为 ppm 估算值：
  - 底层仍保留 `mq_adc_mv / mq_ao_est_mv / mq_filtered_mv / mq_base_mv / mq_delta_mv`。
  - TFT、Wi-Fi 状态帧和 HA/MQTT 的 `gas` 使用 `gas_ppm_est`。
  - ppm 估算使用 `Rs/R0 = 11.5428 * ppm^(-0.6549)` 和当前环境基线，未经过标准气体标定。
- 新增 `GAS_RISK` 核心场景：
  - `scenario=4` 为 `GAS_RISK`。
  - `event_type=11` 为 `GAS_RISK`。
  - `trigger_source=6` 为 `SENSOR`。
  - `gas_ppm_est >= 100` 自动进入确认流程，`gas_ppm_est >= 300` 风险升至 3。
  - COM6 `3` 可模拟气体风险场景，用于不依赖真实危险气体源的验收。
- 新增 `Docs/state_machine_acceptance.md`，用于本地状态机、蜂鸣器、继电器、TFT 和事件上报的现场验收。
- 新增 `Docs/ai_iot_roadmap.md`，明确 AI+IoT 路线：
  - 本地 AI 作为 `risk_hint`，不直接越过状态机控制执行器。
  - 云端 AI 做历史趋势、告警解释、联动建议和降噪。
  - 当前规则状态机作为 AI 前的数据采集和事件闭环地基。
- `cmake --build --preset Debug` 通过。
