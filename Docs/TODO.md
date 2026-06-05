# TODO

## Phase 0：工程地基

- [x] 创建项目目录结构
- [x] 创建基础文档骨架
- [x] 导入 GitHub 下载的 CubeMX/CMake 基础工程
- [x] 安装 STM32Cube FW_U5 V1.8.0 固件包
- [x] 初始化 CubeMX 工程
- [x] 配置 CLion CMake 构建
- [x] 跑通 LED + UART 日志：LED 已实测，USART1 日志已通过 COM6 验证

## Phase 1：MVP 闭环

- [x] LED 点灯测试：`PC7 / LD1` 已烧录上板验证，正常闪烁
- [x] USART1 调试串口 CubeMX 配置与日志验证
- [x] I2C1 总线扫描：PB8/PB9 初始化与空总线扫描已验证
- [x] BME280 环境采集：`0x76 / 0x60`，温湿度/气压日志已上板验证
- [x] PIR GPIO 输入：`A3 / PB0` 已上板触发验证
- [x] Rd-03 V2 OT2 人体存在检测：`A4 / PC1` 已上板触发验证
- [x] Rd-03 V2 UART 协议验证：`CN10 pin 32 / 黑色排母外侧列倒数第二孔 / PB10 -> RX`、`CN10 pin 34 / 黑色排母外侧列最下面孔 / PB11 <- OT1`；`COM6-115200.log` 已验证状态、距离和 32 个距离门能量
- [x] Rd-03 V2 雷达特征层 V1：从 `presence / distance / 32 gate_energy` 计算 zone、peak gate、energy sum、motion score、occupied/still seconds，并在 TFT 显示距离与区域
- [ ] Rd-03 V2 USART3 RX DMA：UART 链路验收后，将轮询接收升级为 DMA 环形缓冲区或 Receive-to-Idle
- [x] MQ 气体模块 ADC AO 采样：`A2 / PC3 / ADC1_IN4` 已重新上板验证
- [ ] 2.0 英寸 240 x 320 TFT 显示：SPI1、GPIO、ST7789 默认驱动和本地状态页已实现；ILI9341 试验更差，当前回到 ST7789。纯色诊断确认长连续填充会失步，已改为分块写入，待正式状态页上板复验和坐标/颜色微调
- [ ] OLED / SSD1306 显示：备选，当前不计划使用
- [ ] 蜂鸣器 GPIO 告警：检测模块完成后再做
- [x] STM32 USART2 TX DMA 状态发送：代码已实现，发送 `seq,temp,hum,gas,presence,risk`
- [x] ESP8266 UART CSV 解析与 MQTT JSON 转换：代码已实现
- [x] ESP8266 假数据 -> Mosquitto -> Home Assistant 基础实体展示：已验证
- [ ] 真实传感器数据 -> STM32 -> ESP8266 -> MQTT -> Home Assistant 全链路验收
- [x] STM32 临时风险分级：仅用于链路测试，不作为正式产品规则
- [ ] STM32 正式风险状态机与事件码
- [ ] 雷达房间区域标定：按实际安装位置记录 `distance_cm / zone / peak_gate / motion_score`

## Phase 2：比赛版 1.0

- [ ] 事件分级与事件码
- [ ] 异常场景演示脚本
- [ ] Home Assistant 自动化联动
- [ ] 调试日志沉淀
- [ ] 答辩素材整理
