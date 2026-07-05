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
  - 蜂鸣器第一版使用有源蜂鸣器，`3.3V` 供电，低电平触发；后续喇叭/语音模块另做。
  - 继电器实物为 HW-280 四路光耦隔离继电器驱动模块，控制端丝印为 `DC+ / DC- / IN1 / IN2 / IN3 / IN4`，板上支持高/低电平触发；第一版按低电平触发设计，实际接线前仍需在 `Docs/pinmap.md` 固化 GPIO 分配。
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
