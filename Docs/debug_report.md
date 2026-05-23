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

## 2026-05-22：I2C1 PB8/PB9 扫描验证

- CubeMX 配置：`I2C1` 启用，`PB8` 为 `I2C1_SCL`，`PB9` 为 `I2C1_SDA`。
- 选择依据：NUCLEO-U5A5ZJ-Q 的 Arduino/Zio I2C 引出使用 `PB8/PB9`；`PG14/PG13` 也是 MCU 合法复用脚，但不作为本项目 MVP 外设接线首选。
- 程序行为：启动后通过 USART1 打印系统启动信息，并执行一次 `HAL_I2C_IsDeviceReady()` 地址扫描。
- 实测输出：`[INFO] i2c scan start`、`[WARN] no i2c device found`、`[INFO] i2c scan done`。
- 结论：I2C1 初始化和扫描程序已运行，当前 PB8/PB9 总线上暂未发现响应设备；接入 BME280/OLED 后需要再次扫描确认地址。
- 调试增强：`Error_Handler()` 增加串口失败日志和 LED 快闪，避免初始化失败时静默停机。

## 2026-05-23：MVP v1 检测层器件决策

- 环境传感：使用 `BME280` 替代 AHT20，已确认 I2C 地址为 `0x76`，芯片 ID 为 `0x60`。
- 人体检测：`PIR` 和 `Rd-03 V2` 两个模块都做；PIR 负责基础人体活动，Rd-03 V2 作为更强的人体存在检测。
- 气体检测：MQ 模块必须使用 `AO` 进入 ADC，不能只依赖数字阈值输出 `DO`。
- 本地显示：已有 OLED / SSD1306，但 MVP v1 暂不优先；需要后端显示时优先考虑 Home Assistant 或更高级前端，OLED 可作为临时替代。
- 告警与联网：蜂鸣器和 ESP8266 都有，但先后置；检测模块完成后再接入声音告警和 MQTT 上报。

## 2026-05-23：检测层超级循环代码

- CubeMX 生成：`ADC1` 使用 `A0 / PA0 / ADC1_IN5`，`PIR_IN` 使用 `A1 / PA1`，`RD03_OUT` 使用 `A2 / PA4`，`USART3` 使用 `D1 / PB10 TX` 和 `D0 / PB11 RX`。
- 当前 USART3 参数：CubeMX 生成为 `115200 8N1`；若 Rd-03 V2 后续串口无数据，再回 CubeMX 调整为模块实际波特率。
- 自定义模块：新增 `Modules/sensor/bme280.*` 和 `Modules/sensor/sensor_mvp.*`。
- 程序结构：`main.c` 只在 `USER CODE` 区调用 `SensorMvp_Init()` 和 `SensorMvp_Update()`；LED 心跳改为基于 `HAL_GetTick()` 的非阻塞节拍。
- BME280：实现芯片 ID 检查、校准参数读取、温度/湿度/气压补偿计算。
- MQ：实现 `ADC1` 单次软件触发采样，串口输出原始值和估算毫伏值。
- PIR/Rd-03：实现 GPIO 轮询和边沿状态日志；Rd-03 UART 先做字节级读取日志，后续再解析协议。
- 构建验证：`cmake --build --preset Debug` 通过。

## 2026-05-23：检测层上板日志验证

- 启动日志：`[INFO] sensor mvp init`、`[INFO] bme280 ready id=0x60 addr=0x76`、`[INFO] adc1 calibration ok`。
- BME280 实测：温度约 `24.48~24.50 C`，湿度约 `74~75 %`，气压约 `1004.9~1005.0 hPa`。
- MQ AO 实测：`mq_raw` 约 `550~623`，`mq_mv` 约 `443~502 mV`，当前低于 STM32 ADC 3.3V 上限。
- PIR/Rd-03 实测：当前静止状态下 `pir=0`、`rd03=0`，仍需人体触发测试。
- Rd-03 UART：曾收到 `0x00` 字节，暂未确认有效协议帧；后续需要结合模块波特率和协议再解析。

## 2026-05-23：MQ AO 分压日志修正

- 硬件分压：MQ `AO -> 2k -> A0 节点 -> 3.3k -> GND`。
- 分压比例：`A0 = AO * 3.3k / (2k + 3.3k) ≈ AO * 0.623`。
- 日志字段：`mq_adc_mv` 表示 STM32 A0 实际测得电压，`mq_ao_est_mv` 表示按分压比例反推的 MQ 原始 AO 估计电压。
- 用途：后续风险阈值优先基于 `mq_ao_est_mv` 或基线相对变化，不直接把 ADC 原始值误当成气体浓度。
