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

## 2026-05-15：安装 U5 固件包

- 安装路径：`C:\Users\0lour\STM32Cube\Repository\STM32Cube_FW_U5_V1.8.0`
- 来源：STMicroelectronics 官方 GitHub `STM32CubeU5`
- 版本：`FW.U5.1.8.0`
- 用途：供 STM32CubeMX 打开 `.ioc` 并重新生成 STM32U5 工程代码。
- 注意：这是 CubeMX 需要的本机 firmware package；项目仓库里的 `Drivers/` 只能保证当前工程可编译，不能替代 CubeMX 的本机包管理。

## 2026-05-17：PA5 LED 与 USART1 配置

- CubeMX 配置：`PA5` 设置为 `GPIO_Output`，User Label 为 `LED_STATUS`。
- CubeMX 配置：`USART1` 设置为异步串口，`PA9` 为 TX，`PA10` 为 RX，波特率 115200。
- 生成文件：`Core/Src/gpio.c`、`Core/Inc/gpio.h`、`Core/Src/usart.c`、`Core/Inc/usart.h`。
- 构建验证：`cmake --build --preset Debug` 通过。
- 生成警告：CubeMX 提示可启用 ICACHE 提高性能、启用 SMPS 改善功耗；MVP 第一关暂不启用，避免同时引入性能/电源配置变量。
- 复盘：该 LED 引脚判断不适用于 NUCLEO-U5A5ZJ-Q 默认硬件连接，后续已修正为 `PC7`。

## 2026-05-17：LED 心跳代码

- 修改位置：`Core/Src/main.c` 的 `USER CODE` 区。
- 启动行为：通过 USART1 输出 `[INFO] system boot`。
- 主循环行为：每 500 ms 翻转一次 `LED_STATUS`。
- 构建验证：`cmake --build --preset Debug` 通过。
- 注意：当前使用 `HAL_Delay(500U)` 是第一关教学验证写法；后续多模块调度时需要改成基于 `HAL_GetTick()` 的非阻塞调度。

## 2026-05-18：修正 LED 引脚为 PC7

- 依据：NUCLEO-U5A5ZJ-Q 用户手册中，板载用户 LED 默认连接为 `LD1 -> PC7`、`LD2 -> PB7`、`LD3 -> PG2`。
- 复核：`PA5` 只是 `LD1` 的可选连接，需要调整焊桥后才成立；默认开发板不应配置为 `PA5`。
- CubeMX 修正：将 `PA5` 恢复为未使用，将 `PC7` 配置为 `GPIO_Output`，User Label 保持 `LED_STATUS`。
- 生成结果：`Core/Inc/main.h` 中 `LED_STATUS_Pin` 为 `GPIO_PIN_7`，`LED_STATUS_GPIO_Port` 为 `GPIOC`。
- 构建验证：`cmake --build --preset Debug` 通过。
- 上板验证：烧录后板载 LED 正常闪烁。
- 架构教训：引脚选择必须优先查开发板用户手册/原理图，而不是只凭 MCU 引脚习惯或其他 Nucleo 板经验。

## 2026-05-18：USART1 串口日志验证

- Windows 识别端口：`COM6`，设备名为 `STMicroelectronics STLink Virtual COM Port`。
- 串口参数：`115200 8N1`，即 115200 波特率、8 数据位、无校验、1 停止位。
- 验证方式：打开 `COM6` 后按下开发板 RESET，读取启动日志。
- 实测输出：`[INFO] system boot`。
- 结论：`USART1 PA9/PA10` 到 ST-LINK 虚拟串口链路正常，后续传感器和 MQTT 调试可以使用串口日志作为主要观测手段。
