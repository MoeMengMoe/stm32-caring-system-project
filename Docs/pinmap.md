# 引脚分配表

本文档记录 MVP 阶段的硬件接口分配。最终配置以 CubeMX `.ioc` 文件为准，修改前需要由硬件负责人 Gary 确认。

| 模块 | 接口 | 引脚 | 方向 | 备注 |
| --- | --- | --- | --- | --- |
| Debug UART | USART1 | PA9 / PA10 | TX / RX | 调试日志 |
| ESP8266 | USART2 | PA2 / PA3 | TX / RX | AT 指令或串口 MQTT 桥接 |
| OLED | I2C1 | PB8 / PB9 | SCL / SDA | 与 AHT20 共用总线 |
| AHT20 | I2C1 | PB8 / PB9 | SCL / SDA | 温湿度采集 |
| MQ2 | ADC1 | PA0 | Analog In | 烟雾/可燃气体模拟量 |
| 蜂鸣器 | GPIO | PB5 | Output | 本地告警 |
| LED / LD1 绿灯 | GPIO | PC7 | Output | NUCLEO-U5A5ZJ-Q 板载 LD1 默认连接；PA5 仅为改焊桥后的可选连接 |
| PIR | GPIO | PC13 | Input | 人体活动检测，需确认与板载按钮连接是否冲突 |

## 注意

- 所有外设必须共地。
- ESP8266 供电电流需求较高，联调时优先确认供电稳定。
- `PC13` 在 Nucleo 板上可能与用户按钮相关，使用 PIR 前需要结合原理图确认实际连接。
