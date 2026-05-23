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
- [ ] PIR GPIO 输入：`A3 / PB0` 已配置，待人体触发验证
- [ ] Rd-03 V2 人体存在检测：`A4 / PC1` 与 `D0/D1 USART3` 已配置，待人体触发/串口验证
- [ ] MQ 气体模块 ADC AO 采样：已迁移到 `A2 / PC3 / ADC1_IN4`，待重新上板验证
- [ ] OLED / SSD1306 显示：备选，MVP v1 暂缓
- [ ] 蜂鸣器 GPIO 告警：检测模块完成后再做
- [ ] USART2 连接 ESP8266
- [ ] ESP8266 MQTT 上报
- [ ] Home Assistant 状态展示
- [ ] STM32 本地风险分级

## Phase 2：比赛版 1.0

- [ ] 事件分级与事件码
- [ ] 异常场景演示脚本
- [ ] Home Assistant 自动化联动
- [ ] 调试日志沉淀
- [ ] 答辩素材整理
