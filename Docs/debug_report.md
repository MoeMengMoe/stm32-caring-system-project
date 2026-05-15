# 调试报告

本文档记录硬件联调、串口日志、CubeMX 配置经验和常见故障。

## 联调前检查清单

- [ ] 当前分支已拉取最新代码
- [ ] `.ioc` 文件版本一致
- [ ] CMake 能正常编译
- [ ] 开发板能正常烧录
- [ ] 串口号确认正确
- [ ] ESP8266 供电稳定
- [ ] GND 已共地
- [ ] MQTT Broker 正常运行
- [ ] Home Assistant 能访问
- [ ] 当前测试用例已记录

## 日志格式建议

```text
[INFO] system boot
[WARN] sensor value out of range
[FAIL] mqtt publish failed
```

## 2026-05-15：导入基础工程

- 来源：`C:\Users\0lour\Downloads\stm32-caring-system-project-main.zip`
- 工程类型：STM32CubeMX 生成的 CMake 工程
- 主控：STM32U5A5ZJTxQ
- 工具链：STM32CubeCLT 1.21.0，`arm-none-eabi-gcc` 14.3.1
- 验证命令：`cmake --preset Debug`
- 验证命令：`cmake --build --preset Debug`
- 结果：Debug 配置和编译均通过，生成 `stm32-caring-system-project.elf`
- 注意：当前 `.ioc` 仍是基础配置，尚未配置 LED、UART、I2C、ADC、PIR、蜂鸣器等 MVP 外设。
