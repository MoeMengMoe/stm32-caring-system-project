# 引脚分配表

本文档记录 MVP 阶段的硬件接口分配。最终配置以 CubeMX `.ioc` 文件为准，修改前需要由硬件负责人 Gary 确认。

| 模块 | 接口 | 引脚 | 方向 | 备注 |
| --- | --- | --- | --- | --- |
| Debug UART | USART1 | PA9 / PA10 | TX / RX | 调试日志 |
| ESP8266 | USART2 | PA2 / PA3 | TX / RX | MQTT 联网上报；检测层完成后再接入 |
| BME280 | I2C1 | PB8 / PB9 | SCL / SDA | 环境采集；地址 `0x76`，芯片 ID `0x60`，替代 AHT20 进入 MVP v1 |
| OLED / SSD1306 | I2C1 | PB8 / PB9 | SCL / SDA | 备选本地显示；MVP v1 暂不优先做 |
| MQ 气体模块 | ADC1 | PA0 | AO | 烟雾/可燃气体模拟量；MVP 必须使用 AO，不只用 DO |
| 蜂鸣器 | GPIO | PB5 | Output | 本地告警；检测模块完成后再接入 |
| LED / LD1 绿灯 | GPIO | PC7 | Output | NUCLEO-U5A5ZJ-Q 板载 LD1 默认连接；PA5 仅为改焊桥后的可选连接 |
| PIR | GPIO | 待 CubeMX 确认 | Input | 人体活动检测，先做基础有人/无人 |
| Rd-03 V2 | UART / GPIO | 待模块接口确认 | RX/TX 或状态输出 | 人体存在检测增强项，和 PIR 都做 |

## 注意

- 所有外设必须共地。
- ESP8266 供电电流需求较高，联调时优先确认供电稳定。
- PIR 不再默认占用 `PC13`，因为该脚可能与板载用户按钮或低功耗唤醒语义混淆；正式配置前由 CubeMX 和开发板引脚表共同确认。
