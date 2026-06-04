# Stm32 Caring System Project

面向独居老人居家安全的边缘智能看护节点。

本项目参加 2026 全国大学生嵌入式芯片与系统设计竞赛芯片应用赛道，当前方向为 ST 赛道 IoT。作品基于 NUCLEO-U5A5ZJ-Q，目标是完成多源传感器采集、STM32 本地风险判断、本地告警、MQTT 上报和 Home Assistant 可视化联动。

当前阶段为“联网监测 MVP 原型”：传感器采集已基本完成，通信与服务器组件已具备，真实数据全链路验收、正式风险判断和本地告警仍待完成。详细状态见 `Docs/progress_report.md`。

当前上板验收入口：`Docs/acceptance_1_2.md`。

## 目标 MVP 闭环

```text
传感器采集 -> STM32 本地规则判断 -> TFT/蜂鸣器告警 -> ESP8266/MQTT 上报 -> Home Assistant 展示
```

## 当前硬件

- 主控板：NUCLEO-U5A5ZJ-Q
- 主控芯片：STM32U5A5ZJ
- 开发工具：CubeMX + CLion + arm-gcc
- 联网模块：ESP8266 D1 mini，自定义 UART CSV 转 MQTT JSON 网关固件
- 计划显示：2.0 英寸 240 x 320 TFT，控制器型号待核对

实物器件识别与状态见 `Docs/hardware_inventory.md`。

## 目录结构

```text
Core/       CubeMX 生成的核心代码
Drivers/    STM32 HAL/CMSIS 驱动
Modules/    项目自定义模块
Docs/       项目文档
Test/       测试代码与实验片段
```

## 协作约定

- 不直接修改 HAL/CubeMX 自动生成区。
- AI 不直接修改 `.ioc`，涉及引脚、时钟、外设配置时由硬件负责人确认后在 CubeMX 中修改。
- 自定义功能模块放入 `Modules/`。
- 关键接口、引脚、协议、调试问题同步更新 `Docs/`。
- `Docs/pinmap.md` 是引脚分配、CubeMX 配置与接线方式的唯一权威来源；任何引脚变化必须在同一次提交中更新。
- 引脚与接线必须以 ST 官方手册、对应修订版原理图和实物可见位置为依据；Zio `Dxx` 编号不得单独用于指导实物接线。
- AI 参与的提交信息中注明 `AI-assisted`。
