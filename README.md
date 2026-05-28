# Stm32 Caring System Project

面向独居老人居家安全的边缘智能看护节点。

本项目参加 2026 全国大学生嵌入式芯片与系统设计竞赛芯片应用赛道，当前方向为 ST 赛道 IoT。作品基于 NUCLEO-U5A5ZJ-Q，完成多源传感器采集、STM32 本地风险判断、本地告警、MQTT 上报和 Home Assistant 可视化联动。

## MVP 闭环

```text
传感器采集 -> STM32 本地规则判断 -> OLED/蜂鸣器告警 -> ESP8266/MQTT 上报 -> Home Assistant 展示
```

## 当前硬件

- 主控板：NUCLEO-U5A5ZJ-Q
- 主控芯片：STM32U5A5ZJ
- 开发工具：CubeMX + CLion + arm-gcc
- 联网模块：ESP8266，USART AT/MQTT 方案

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
- AI 参与的提交信息中注明 `AI-assisted`。

