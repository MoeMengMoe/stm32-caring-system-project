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
- 新增 MQ ppm 调试注入接口：
  - 云端/ESP8266 可发送 `D,request_id,6,4,value`，将 `value` 作为 ppm 偏移量叠加到 `gas_ppm_est`。
  - `value=0` 清除调试偏移，`value` 上限钳制为 `9999`。
  - 本地 COM6 新增 `4=gas+150ppm`、`5=gas+350ppm`、`0=clear-gas-debug`，用于无危险气体源时的现场验收。
  - 调试偏移影响 TFT、Wi-Fi 状态帧、`AI_SAMPLE` 和状态机风险判断，但不伪造真实 MQ ADC/mV 字段。
- 合并 Simon 的 `esp32-voice-recognition` 工程，并对齐 ESP32-S3 麦克风本地语音协议：
  - ESP32-S3 通过 UART1 `GPIO17/GPIO18` 向 STM32 UART4 `PA1/PA0` 发送 `RISK:<level>` 文本帧。
  - STM32 `Modules/comm/comm_local.c` 已兼容 `RISK:0..3`：`0` 清除告警，`2/3` 触发场景一求助确认流程，`1` 暂作为低风险/家居控制类事件预留。
  - UART4 引脚和 CubeMX 配置保持不变；本次只修改协议解析和文档。
- 强化语音风险和 AI 数据链路：
  - `RISK:0..3` 不再伪装成云端 demo 命令，改为独立 `COMM_WIFI_COMMAND_VOICE_RISK`。
  - 状态机新增 `APP_EVENT_VOICE_RISK(12)` 和 `APP_TRIGGER_VOICE(7)`，事件来源可与 `REMOTE / BUTTON / RADAR / SENSOR` 区分。
  - 语音风险等级作为风险下限保留，`RISK:3` 会让状态上报、TFT 和 `AI_SAMPLE` 保持高风险，直到 ACK/clear。
  - `AI_SAMPLE` 新增最近事件、触发源、flags、ACK 剩余时间、继电器 manual/auto mask 字段。
  - 新增 COM6 AI 采集标签：`e/w/f/j/g/v/x` 可标记正常环境、行走、模拟跌倒、长静止、气体调试、语音风险和清空标签；`AI_SAMPLE` 输出 `session/label`。
  - 对 ESP32-S3 语音 `RISK:x` 做 2 秒重复帧抑制；`RISK:1` 进入 `NOTICE` 并在 10 秒后自动消退，`RISK:2/3` 仍进入确认流程。
  - 新增 `Docs/ai_dataset_collection.md`，定义后续 AI 数据采集命名、标签和干净数据规则。
- 继续增强本地联调和 AI 采集工具链：
  - COM6 新增 `6/7/8/9`，分别模拟 `RISK:1/2/3/0`，不接 ESP32-S3 麦克风时也能走同一条 `VOICE_RISK -> app_state_machine -> event/status/AI_SAMPLE` 路径。
  - `p` 状态打印扩展为 app / sensor / radar 三段摘要，便于现场判断当前风险、气体基线、雷达距离门、motion/still/occupied 等字段是否一致。
  - `AI_SAMPLE` 新增 `pir` 和 `rd03_ot2`，把融合后的 `presence` 与 PIR、RD03 OT2、UART radar presence 三路来源拆开，避免训练集只看到一个混合结果。
  - 新增 `tools/ai_dataset_summary.py`，可直接统计 `[AI_SAMPLE]` 日志或 CSV 的标签分布、有效率、数值范围和采集时长。
  - 新增 `tools/ai_window_features.py`，把逐秒样本聚合成滑动窗口特征 CSV，为后续本地/云端 AI 场景识别训练做准备。
  - `tools/ai_sample_to_csv.py` 已同步新增字段顺序，优先保留 `session/label/presence/pir/rd03_ot2/radar_* /gas_* /state/event/trigger` 等关键列。
  - 状态输出和 `AI_SAMPLE` 新增 `risk_src`，把当前风险来源解释为 `VOICE_ACK / GAS / RADAR_UART / RD03_OT2 / NETWORK / NONE` 等，方便联调和后续训练集回放。
  - 本轮未修改 `.ioc`、CubeMX 配置、引脚或接线。
- TFT 显示方向调整：
  - `Modules/display/tft_lcd.c` 将默认横屏 MADCTL 从原方向改为 180 度中心对称方向，用于适配当前原型机换方向安装后的观看角度。
  - 只修改屏幕控制器方向寄存器，不修改 UI 坐标布局、不修改 `.ioc`、不修改引脚和接线。
  - `cmake --build --preset Debug` 通过。
- 新增多场景引擎 v1：
  - 新增 `Modules/scene/scene_engine.*`，把传感器和 app 状态解释为统一 `SceneSignal`：`scene_top / scene_action / scene_sev / scene_conf / scene_mask / evidence`。
  - 当前覆盖 `SENSOR_FAULT`、`GAS_WARN`、`GAS_ALARM`、`LONG_STILL_WATCH`、`LONG_STILL_RISK`、`RADAR_PRESENCE`、`PRESENCE_CONFLICT`、`MOTION_BURST`、`HEAT_STRESS`、`COLD_RISK`、`HUMIDITY_HIGH/LOW`、`NETWORK_OFFLINE`、`ACTIVE_ACK`。
  - 第一版作为旁路分析层运行，不直接接管继电器、蜂鸣器、ACK 或报警；原状态机中的气体/长静止强规则暂时保留，等日志稳定后再迁移为 scene-driven policy。
  - COM6 `p` 新增 `console scene ...` 摘要；`AI_SAMPLE` 新增场景字段，`tools/ai_sample_to_csv.py`、`tools/ai_dataset_summary.py`、`tools/ai_window_features.py` 已同步。
  - 新增 `Docs/scene_engine_design.md` 记录多场景架构和后续迁移路线。
  - `cmake --build --preset Debug` 通过；本轮未修改 `.ioc`、CubeMX 配置、引脚或接线。
## 2026-07-06: Rd-03 V2 long-still false alarm mitigation

- Problem observed: the radar sometimes entered `Still ACK` even when nobody was intentionally testing nearby. This proved that the first long-still rule was too idealized.
- Firmware mitigation:
  - `APP_LONG_STILL_TRIGGER_SECONDS` changed from `20s` to `120s`.
  - Long-still ACK now requires `radar_valid=1`, UART `radar_presence=1`, `rd03_ot2=1`, non-zero `radar_distance_cm`, and at least two active gates.
  - Scene engine long-still watch/risk thresholds changed to `20s/90s` and use the same cross-check evidence.
- Interpretation: this does not unlock the full Rd-03 V2 capability yet. It only reduces false positives before official upper-computer calibration.
- Next radar-specific work: use `Simon6.4/xend101htool_1_.zip` official client to inspect distance gates, thresholds, and empty-room noise, then convert the measured thresholds into STM32 firmware parameters.

## 2026-07-06: Rd-03 V2 calibration logging path

- Added COM6 debug command `k` to toggle continuous `[RADAR_CAL]` logs at 500 ms intervals.
- Each calibration line includes UART presence, OT2, distance, zone, peak gate, peak energy, active gate count, motion score, occupied/still seconds, overflow count, and all 32 gate energies.
- Added `tools/rd03_calibration_summary.py` to summarize calibration logs and optionally export CSV for later AI/threshold analysis.
- Updated `Docs/rd03_upper_client_calibration.md` with the STM32 calibration workflow and label naming.
- No CubeMX or pin changes.
