# 调试报告

## 2026-06-04：COM6 雷达 UART 首次验收失败分析

- 日志文件：`COM6-115200.log`，本地调试日志不提交到 Git。
- 日志统计：`[RADAR] waiting for valid UART report` 共 174 次，`radar_valid=1` 为 0 次。
- `rd03_ot2=1` 共 696 次，`rd03_ot2=0` 为 0 次，说明雷达已供电并持续给出数字有人状态，但不能证明 UART 接线正确。
- `status tx` 共 174 次，BME280 没有失败记录，说明 STM32 主循环、环境采集和状态发送仍在运行。
- 软件问题：旧驱动使用 `HAL_UART_Receive(..., timeout=0)` 轮询。雷达以 `115200` 连续输出时，主循环中的阻塞调试日志可能导致无法拼出完整上报帧。
- 修改：雷达 UART 每字节接收等待时间改为 `1 ms`，并新增 `rx_bytes / header_sync / bad_len / bad_footer` 诊断计数。
- 构建结果：`cmake --build --preset Debug` 通过，FLASH `71744 B / 4 MB`，RAM `3864 B / 2496 KB`。
- 下一次烧录后，根据 `[RADAR] valid=0 ...` 中的计数区分接收线、配置命令线和协议丢帧问题。
- 额外观察：MQ 的 `mq_ao_est_mv` 在短时间内波动较大，完成雷达 UART 排查后需要检查 AO 分压节点、面包板接触、模块供电和共地。
- 后续增强：驱动现在会逐条解析打开命令模式、切换上报模式和关闭命令模式的 ACK，并输出 `init_rx` 与 `ack=open/report/close`。最新构建通过，FLASH `72856 B / 4 MB`，RAM `3880 B / 2496 KB`。

## 2026-06-04：Rd-03 V2 官方资料对齐与接线纠正

- 依据：Simon 提供的 `Rd-03_V2版_用户使用手册_V_1.0.0.pdf`、`rd-03_v2_serial_communication_cn.pdf` 和官方上位机工具。
- 纠正：Rd-03 V2 的 `OT1` 是雷达串口数据输出，部分官方表格写作 `TX`；`OT2` 才是根据检测结果输出高低电平的引脚。
- 板级核对：本项目实物为 `MB1549-U5A5ZJQ-C04`。官方原理图显示 `SB61/SB62` 均已装配，`PB10` 可从 `CN10 pin 15 / D27` 或 `CN10 pin 32 / D36` 使用。
- 推荐接线：`CN10 pin 32 / 黑色排母外侧列倒数第二孔 / PB10 / USART3_TX -> 雷达 RX`，`CN10 pin 34 / 黑色排母外侧列最下面孔 / PB11 / USART3_RX <- 雷达 OT1`，`A4 / PC1 / RD03_OUT <- 雷达 OT2`。
- 文档纠正：`D35/D36` 是 ST 手册中的 Zio 逻辑编号，不是板子正面丝印；以后实物接线说明优先使用连接器针脚号和实际位置。
- 二次纠正：ST `UM2861` Table 18 与 C04 原理图表明 `pin 32/pin 34` 是 CN10 偶数脚，位于黑色排母靠板边和金色 Morpho 排针的外侧列。此前写成“靠 MCU 的内侧列”是错误的；内侧列底部是 `pin 31/pin 33`，会导致 USART3 始终收不到雷达字节。
- CubeMX 结论：当前 `PB10/PB11/PC1` 功能配置不需要更换。
- 代码改进：新增 `Modules/sensor/rd03_v2.*`，启动时切换雷达到二进制上报模式，解析有人/无人、目标距离和 32 个距离门能量。
- 验证结果：重新接到 CN10 外侧偶数脚后，`COM6-115200.log` 已确认 `radar_valid=1`、`radar_presence`、`radar_cm` 和 32 个距离门能量正常变化。

## 2026-06-04：Rd-03 V2 UART 协议验收通过

- 验收日志：`COM6-115200.log`，文件时间 `2026-06-04 14:03:02`，本地调试日志不提交到 Git。
- 根因结论：此前 `rx_bytes=0` 的直接原因是误接到 CN10 靠 MCU 的内侧奇数脚。将雷达 `RX` 接到 `CN10 pin 32 / PB10`、`OT1` 接到 `CN10 pin 34 / PB11`，并使用黑色 CN10 排母靠板边的外侧列后，USART3 立即收到有效帧。
- 有效雷达摘要：`[RADAR] valid=1` 共 24 条，`valid=0` 为 0 条；帧计数从 `10` 增长到 `231`。
- 距离与能量摘要：`distance_cm` 在 `35~102 cm` 之间变化，最大 `peak_energy=422349`；`[RADAR_E]` 共 96 条，`g00~g31` 每个距离门均出现且能量不是永久全零。
- 存在状态摘要：日志中 `radar_presence`、`rd03_ot2` 和 PIR 均出现状态变化，证明 UART 存在状态、OT2 数字输出和 PIR 输入都在工作。
- 并行功能摘要：`[ENV]` 共 24 条、`[DETECT]` 共 98 条、`[INFO] status tx` 共 24 条，说明雷达解析没有阻塞环境采集和状态发送。
- 验收结论：Rd-03 V2 UART 的上报模式、有人/无人状态、目标距离和 32 个距离门能量解析已升级为“已验证产品能力”。
- 版本说明：该日志仍包含旧提示 `[INFO] rd03 report mode command sent`，早于最新 ACK 诊断固件。最新固件已构建并烧录，后续只需保留一次按 RESET 后包含 `rd03 config ack open=1 report=1 close=1` 的短日志，用于补充双向命令 ACK 证据；这不影响本次上报数据验收结论。

## 2026-06-07：Rd-03 V2 USART3 RX 升级为 DMA

- 问题：雷达连续输出二进制帧，旧驱动在 `Rd03V2_Update()` 中使用 `HAL_UART_Receive(..., 1ms)` 单字节轮询。TFT 刷新、USART1 日志或其他任务占用主循环时，USART3 可能来不及取字节，导致只解析出部分有效帧。
- 修改：USART3 RX 绑定到 `GPDMA1 Channel 1`，DMA request 为 `GPDMA1_REQUEST_USART3_RX`，接收方式改为 `HAL_UARTEx_ReceiveToIdle_DMA()`。
- 驱动结构：DMA 回调只把新字节搬入软件环形缓冲区，协议状态机仍在主循环 `Rd03V2_Update()` 中解析，避免在中断里做复杂协议处理。
- 新诊断字段：`dma_evt` 表示 DMA 接收事件数，`dma_restart` 表示 Receive-to-Idle 重启次数，`rx_ovf` 表示软件环形缓冲溢出次数。正常验收时 `dma_evt` 应持续增加，`rx_ovf` 应保持为 `0`。
- 接线：无变化，仍为 `PB10 / USART3_TX -> Rd-03 RX`，`PB11 / USART3_RX <- Rd-03 OT1`，`PC1 / RD03_OUT <- Rd-03 OT2`。

## 2026-05-17 ESP8266 到 MQTT 到 HA 全链路联调

### 参与角色

- Simon：ESP8266 固件、MQTT 服务端、Home Assistant 显示链路

### 联调目标

打通最小可演示链路：

```txt
NodeMCU ESP8266 固件假数据
  -> Wi-Fi
  -> VMware NAT 端口转发
  -> Docker Mosquitto
  -> Home Assistant MQTT 实体
  -> HA 面板显示
```

本次联调暂未接入 STM32 USART2 实时数据，ESP8266 使用假 JSON 数据验证通信链路。

### 环境与组件

- ESP8266 开发板：NodeMCU ESP8266，PlatformIO `nodemcuv2`
- ESP8266 框架：Arduino
- MQTT 客户端库：PubSubClient
- 服务器：Ubuntu 虚拟机，VMware NAT 网络
- 容器管理：Docker Compose，DPanel
- MQTT Broker：`eclipse-mosquitto:2`
- 显示面板：`ghcr.io/home-assistant/home-assistant:stable`

### 服务器部署

Compose 配置位于：

```txt
server/docker-compose.yml
```

服务容器：

```txt
eldercare-mosquitto      1883
eldercare-homeassistant  8123
```

Mosquitto 配置位于：

```txt
server/mosquitto/config/mosquitto.conf
```

当前 MVP 配置：

- 监听 `0.0.0.0:1883`
- 允许匿名访问
- 开启持久化
- 日志输出到 stdout 和 `server/mosquitto/log/`

Home Assistant 配置位于：

```txt
server/homeassistant/config/configuration.yaml
```

已配置 MQTT 传感器：

- `Node01 Temperature`
- `Node01 Humidity`
- `Node01 Gas`
- `Node01 Presence`
- `Node01 Risk`
- `Node01 Event`

### 网络配置

VMware 使用 NAT 网络，并通过端口转发暴露服务：

```txt
Windows 宿主机 TCP 1883 -> Ubuntu 虚拟机 TCP 1883
Windows 宿主机 TCP 8123 -> Ubuntu 虚拟机 TCP 8123
```

ESP8266 固件中的 MQTT 主机地址使用 Windows 宿主机的局域网 IP，而不是 `localhost` 或 Ubuntu NAT IP。

### MQTT Topic 与测试数据

状态 topic：

```txt
eldercare/node01/status
```

ESP8266 当前每 5 秒发布一次假数据：

```json
{"node_id":"node01","seq":0,"temperature":25.6,"humidity":61,"gas":120,"presence":1,"risk":0,"event":"normal"}
```

其中 `seq` 会递增，用于观察消息连续性。

### 验收结果

已完成：

- ESP8266 成功连接 Wi-Fi
- ESP8266 成功连接 Mosquitto
- ESP8266 能周期性发布 MQTT 假数据
- Mosquitto 能接收 `eldercare/node01/status`
- Home Assistant 能通过 MQTT 读取状态数据
- HA 面板能显示基础实体状态

全链路验证结果：

```txt
固件 -> 服务器 -> 显示面板
```

已打通。

### 常用验证命令

启动服务：

```bash
cd server
docker compose up -d
```

查看容器：

```bash
docker ps
```

订阅状态 topic：

```bash
docker exec -it eldercare-mosquitto mosquitto_sub -t eldercare/node01/status -v
```

手动发布测试消息：

```bash
docker exec -it eldercare-mosquitto mosquitto_pub -t eldercare/node01/status -m '{"test":1}'
```

查看 Mosquitto 日志：

```bash
docker logs -f eldercare-mosquitto
```

查看 HA 日志：

```bash
docker logs -f eldercare-homeassistant
```

### 已知注意事项

- ESP8266 访问 MQTT 时必须使用 Windows 宿主机局域网 IP。
- VMware NAT 端口转发必须包含 `1883`，访问 HA 时还需要 `8123`。
- Windows 防火墙需允许相关端口入站。
- Home Assistant Container 没有 Add-on Store，Mosquitto 作为独立容器运行。
- 当前 Mosquitto MVP 阶段允许匿名访问，演示或长期运行前建议增加用户名密码。
- 当前 ESP8266 使用假数据，下一阶段再接 STM32 USART2 JSON。

### 下一步计划

1. 将 ESP8266 固件从假数据发布改为读取 STM32 USART2 数据行。历史记录中曾写作 JSON 行；当前冻结版协议已改为 `Docs/protocol.md` 中的 `S/E/C/R/D` CSV 帧。
2. 保留假数据模式作为离线测试开关。
3. 在 HA Dashboard 中整理项目展示卡片。
4. 根据演示需要给 Mosquitto 增加账号密码。
5. 将服务器部署和网络转发步骤补充到 `Docs/esp8266_mqtt_quickstart.md`。
