# 引脚分配表

本文档记录 MVP 阶段的硬件接口分配。最终配置以 CubeMX `.ioc` 文件为准，修改前需要由硬件负责人 Gary 确认。

| 模块 | 接口 | 引脚 | 方向 | 备注 |
| --- | --- | --- | --- | --- |
| Debug UART | USART1 | PA9 / PA10 | TX / RX | 调试日志 |
| ESP8266 | USART2 | A1 / PA2, A0 / PA3 | TX / RX | 已预留通信引脚；检测层完成后再接入 MQTT 上报 |
| BME280 | I2C1 | PB8 / PB9 | SCL / SDA | 环境采集；地址 `0x76`，芯片 ID `0x60`，替代 AHT20 进入 MVP v1 |
| OLED / SSD1306 | I2C1 | PB8 / PB9 | SCL / SDA | 备选本地显示；MVP v1 暂不优先做 |
| MQ 气体模块 | ADC1 | A2 / PC3 | AO | `ADC1_IN4`；烟雾/可燃气体模拟量；MVP 必须使用 AO，不只用 DO |
| 蜂鸣器 | GPIO | PB5 | Output | 本地告警；检测模块完成后再接入 |
| LED / LD1 绿灯 | GPIO | PC7 | Output | NUCLEO-U5A5ZJ-Q 板载 LD1 默认连接；PA5 仅为改焊桥后的可选连接 |
| PIR | GPIO | A3 / PB0 | Input | `PIR_IN`，下拉输入；人体活动检测 |
| Rd-03 V2 | GPIO | A4 / PC1 | Input | `RD03_OUT`，下拉输入；人体存在数字状态 |
| Rd-03 V2 | USART3 | D1 / PB10, D0 / PB11 | TX / RX | 当前 CubeMX 生成 `115200 8N1`；后续如需改雷达默认波特率再调整 |

## 注意

- 所有外设必须共地。
- ESP8266 供电电流需求较高，联调时优先确认供电稳定。
- PIR 不再默认占用 `PC13`，因为该脚可能与板载用户按钮或低功耗唤醒语义混淆；MVP v1 使用 Arduino `A3 / PB0`。
- MQ 模块由面包板电源模块供 `5V`，`AO` 通过 `2k/3.3k` 分压后接入 `A2`，日志中 `mq_adc_mv` 是 A2/PC3 电压，`mq_ao_est_mv` 是反推的 MQ AO 电压。
