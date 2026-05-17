# Simon 通信与服务器开发计划

## 角色边界

Simon 负责 STM32 与 ESP8266 D1 mini 的通信链路、ESP8266 到本地 Mosquitto 的 MQTT 发布链路，以及 Linux Docker 服务器环境搭建。

不主动修改以下内容：

- `stm32-caring-system-project.ioc`
- `Core/` 下 CubeMX 自动生成区
- HAL/CMSIS 驱动库
- Gary 负责的主控采集、风险判断和主循环架构

如果发现 USART2、PA2/PA3、波特率、供电或引脚配置与项目文档不一致，应先提示 Gary 在 CubeMX 中确认。

## 总体链路

```txt
STM32 采集与本地风险判断
  -> USART2 输出 JSON 行
  -> ESP8266 D1 mini 读取串口 JSON
  -> ESP8266 连接 Wi-Fi
  -> ESP8266 发布 MQTT
  -> Linux Docker Mosquitto
  -> Home Assistant 或 MQTT 客户端显示
```

## 硬件连接原则

当前方案使用 ESP8266 D1 mini，D1 mini 由面包板电源提供稳定 5V，Nucleo 板独立供电。

推荐接线：

```txt
面包板电源 5V  -> D1 mini 5V / VIN
面包板电源 GND -> D1 mini GND
Nucleo GND      -> D1 mini GND
Nucleo PA2 TX   -> D1 mini RX
Nucleo PA3 RX   <- D1 mini TX
```

注意事项：

- 面包板电源、D1 mini、Nucleo 必须共地。
- 不要把面包板 5V 直接接到 Nucleo 5V，除非硬件负责人确认 Nucleo 外部供电方式。
- Nucleo U5 与 D1 mini 均为 3.3V 串口逻辑，USART TX/RX 可以直接连接。
- D1 mini 不要从 Nucleo 3V3 取电，ESP8266 Wi-Fi 峰值电流容易导致复位或掉线。

## 阶段 1：STM32 到 D1 mini 串口打通

目标：只验证串口，不接入 Wi-Fi 和 MQTT。

验收链路：

```txt
STM32 USART2 输出一行 JSON
D1 mini 串口收到完整一行
```

需要确认：

- STM32 USART2 已配置为 `115200 8N1`
- TX/RX 使用 PA2/PA3
- 串口数据以 `\r\n` 或 `\n` 结束
- D1 mini 能按行读取，不丢失、不粘包

建议测试 payload：

```json
{"node_id":"node01","seq":1,"temperature":25.6,"humidity":61,"gas":120,"presence":1,"risk":0,"event":"normal"}
```

阶段输出：

- 记录实际接线
- 记录串口参数
- 记录一条成功接收的 JSON 示例

## 阶段 2：D1 mini 连接 Wi-Fi

目标：D1 mini 单独运行，确认能接入本地网络。

验收标准：

```txt
D1 mini 串口输出 Wi-Fi connected
D1 mini 串口输出局域网 IP 地址
```

实现建议：

- 使用 Arduino ESP8266 Core
- 使用 `ESP8266WiFi.h`
- Wi-Fi SSID 和密码先放在单独配置区，后续再考虑隐藏或拆分

阶段输出：

- 记录 Wi-Fi 名称
- 记录 D1 mini 获得的 IP
- 记录失败时的串口日志，例如密码错误、信号弱、供电不足

## 阶段 3：Docker Mosquitto 搭建

目标：在 Linux Docker 环境中运行本地 MQTT Broker。

建议目录：

```txt
server/
  docker-compose.yml
  mosquitto/
    config/
      mosquitto.conf
    data/
    log/
```

MVP 阶段建议先不开认证，监听局域网 `1883`，便于快速联调。演示前再根据需要加入用户名密码。

建议 topic：

```txt
eldercare/node01/status
eldercare/node01/event
eldercare/node01/alarm
```

验收命令：

```bash
mosquitto_sub -h <server_ip> -t eldercare/node01/status
mosquitto_pub -h <server_ip> -t eldercare/node01/status -m '{"test":1}'
```

验收标准：

```txt
subscribe 端能收到 publish 端发出的消息
```

阶段输出：

- `docker-compose.yml`
- `mosquitto.conf`
- Broker IP、端口、topic 记录
- 本机或局域网客户端测试记录

## 阶段 4：D1 mini 发布 MQTT 假数据

目标：D1 mini 不接 STM32，先定时发布固定 JSON 到 Mosquitto。

验收链路：

```txt
D1 mini
  -> Wi-Fi
  -> Mosquitto
  -> mosquitto_sub 收到消息
```

实现建议：

- 使用 `PubSubClient`
- 固定 client id：`eldercare-node01`
- 发布 topic：`eldercare/node01/status`
- 初期每 5 秒发布一次固定 JSON
- 断线后自动重连 Wi-Fi 和 MQTT

建议测试 payload：

```json
{"node_id":"node01","seq":1,"temperature":25.6,"humidity":61,"gas":120,"presence":1,"risk":0,"event":"normal"}
```

验收标准：

```txt
mosquitto_sub 能持续收到 D1 mini 发布的数据
断开 Wi-Fi 或重启 Broker 后，恢复网络时能重新发布
```

阶段输出：

- D1 mini 测试固件
- MQTT 连接日志
- Broker 端接收记录

## 阶段 5：整链路联调

目标：STM32 数据经 D1 mini 发布到 Mosquitto。

完整链路：

```txt
STM32 采集或模拟数据
  -> USART2 JSON 行
  -> D1 mini 串口读取
  -> D1 mini MQTT publish
  -> Mosquitto 接收
  -> Home Assistant 或客户端显示
```

验收标准：

- STM32 修改数据后，MQTT topic 中的数据同步变化。
- `seq` 字段连续递增，便于观察是否丢包。
- 串口异常、Wi-Fi 断开、MQTT 断开时有可读日志。
- 供电稳定，D1 mini 不反复重启。

## 协议约定

UART 基本参数：

```txt
baudrate: 115200
data bits: 8
stop bits: 1
parity: none
line ending: \r\n
encoding: UTF-8 / ASCII
```

### STM32 到 ESP8266 UART 数据包

MVP 阶段 STM32 不直接发送 JSON，避免在 STM32 侧增加字符串拼接和转义复杂度。STM32 只发送固定顺序的数字 CSV 行，由 ESP8266 转换为 MQTT JSON。

包格式：

```txt
temperature,humidity,gas,presence,risk\r\n
```

示例：

```txt
25.6,61.0,120,1,0\r\n
```

字段顺序固定，不允许省略字段：

| 位置 | 字段 | 类型 | 说明 |
| --- | --- | --- | --- |
| 1 | `temperature` | float | 温度，单位摄氏度 |
| 2 | `humidity` | float | 湿度，单位 `%` |
| 3 | `gas` | int | MQ2 或气体传感器数值 |
| 4 | `presence` | int | PIR 人体存在状态，`0/1` |
| 5 | `risk` | int | STM32 本地风险等级，`0-3` |

长度约束：

```txt
single line <= 64 bytes
```

STM32 侧建议整包发送，不需要手写逐字节发送：

```c
snprintf(buf, sizeof(buf), "%.1f,%.1f,%d,%d,%d\r\n",
         temperature, humidity, gas, presence, risk);
HAL_UART_Transmit(&huart2, (uint8_t *)buf, strlen(buf), 100);
```

注意：

- 不要在中断回调里使用长阻塞发送。
- 发送周期建议不低于 1 秒一次。
- `presence` 只能发送 `0` 或 `1`。
- `risk` 只能发送 `0`、`1`、`2`、`3`。
- 必须带换行结束符，推荐 `\r\n`。

### ESP8266 到 MQTT JSON

ESP8266 解析 UART CSV 后发布 JSON 到：

```txt
eldercare/node01/status
```

发布格式：

```json
{
  "node_id": "node01",
  "temperature": 25.6,
  "humidity": 61.0,
  "gas": 120,
  "presence": 1,
  "risk": 0,
  "event": "normal"
}
```

`event` 由 ESP8266 根据 `risk` 自动生成：

| risk | event |
| --- | --- |
| 0 | `normal` |
| 1 | `notice` |
| 2 | `warning` |
| 3 | `alarm` |

### MQTT JSON 字段

| 字段 | 类型 | 含义 |
| --- | --- | --- |
| `node_id` | string | 节点编号，MVP 使用 `node01` |
| `temperature` | number | 温度 |
| `humidity` | number | 湿度 |
| `gas` | number | MQ2 或其他气体传感器读数 |
| `presence` | number | PIR 人体存在状态，0/1 |
| `risk` | number | 风险等级，0-3 |
| `event` | string | 事件名，例如 `normal`、`warning`、`alarm` |

risk 等级：

| 等级 | 含义 |
| --- | --- |
| 0 | 正常 |
| 1 | 提醒 |
| 2 | 警告 |
| 3 | 高风险 |

## 推荐代码边界

STM32 侧：

- Gary 负责采集、风险判断、主循环调度。
- Simon 只定义 UART JSON 协议和必要的通信接口建议。
- 若需要 STM32 侧新增发送函数，应放在 `Modules/protocol/` 或通信相关模块中，并由 Gary 接入主循环。

D1 mini 侧：

- Wi-Fi 连接
- MQTT 连接与重连
- 串口按行读取 STM32 JSON
- 校验 JSON 是否为空、是否过长、是否包含结束符
- 发布到 MQTT topic

Linux Docker 侧：

- Mosquitto 配置
- topic 测试
- 日志记录
- 后续 Home Assistant 对接

## 联调检查清单

每次联调前确认：

- Nucleo、D1 mini、面包板电源共地。
- D1 mini 使用稳定 5V 供电。
- USART2 参数为 `115200 8N1`。
- STM32 输出 JSON 以换行结束。
- Mosquitto Docker 容器正在运行。
- Linux 服务器 IP 没有变化。
- 防火墙允许局域网访问 `1883`。
- `mosquitto_sub` 能收到手动 `mosquitto_pub` 测试消息。
- D1 mini 能连接 Wi-Fi 并获得 IP。

## MVP 完成定义

满足以下条件即可认为 Simon 负责的 MVP 通信链路完成：

- STM32 能稳定输出项目约定 JSON。
- D1 mini 能稳定读取 STM32 JSON。
- D1 mini 能连接 Wi-Fi 和 Mosquitto。
- D1 mini 能将 STM32 JSON 发布到 `eldercare/node01/status`。
- Linux Docker Mosquitto 能被局域网客户端订阅和测试。
- 文档记录接线、topic、payload、Docker 配置和常见故障。

## V2.0 后台分析服务构想

该部分作为后续开发路线，不阻塞 MVP。MVP 阶段仍以 STM32 本地风险判断、ESP8266 上传、Mosquitto 转发、Home Assistant 展示为主。

### 目标定位

后台分析服务用于增强云端能力：

```txt
Mosquitto
  -> analysis_service 订阅设备状态
  -> 数据库存档
  -> 本地规则初筛
  -> 必要时调用大模型辅助判断
  -> 分析结果发布回 MQTT
  -> HA 显示分析结果
  -> 后期回传 ESP8266/STM32
```

设计原则：

- STM32 本地风险判断始终是主判断。
- 云端大模型只做辅助分析和建议，不作为唯一告警依据。
- 云端失联时，本地蜂鸣器和本地风险判断仍应可用。
- 后台服务以 Docker 容器封装，便于在虚拟机、局域网服务器和云服务器之间迁移。

### 推荐 Topic

```txt
eldercare/node01/status     # 设备上报状态
eldercare/node01/analysis   # 后台分析结果
eldercare/node01/alarm      # 后台告警事件
eldercare/node01/command    # 云端回传本体建议或命令
```

### 服务职责

`analysis_service` 建议承担：

1. 订阅 `eldercare/node01/status`。
2. 校验 JSON payload 字段完整性。
3. 保存原始数据到数据库。
4. 基于规则进行初步分析。
5. 判断是否需要调用大模型。
6. 保存分析结果。
7. 发布分析结果到 `eldercare/node01/analysis`。
8. 高风险时发布告警到 `eldercare/node01/alarm`。
9. 后期通过 `eldercare/node01/command` 将辅助建议回传给 ESP8266/STM32。

### 数据库路线

MVP 后的第一版后台服务建议使用 SQLite：

```txt
server/analysis_service/data/eldercare.db
```

理由：

- 部署简单。
- Docker 只需挂载一个数据目录。
- 足够记录单节点比赛演示数据。
- 后续可平滑迁移到 PostgreSQL。

后期多节点或长期运行时，可升级为：

```txt
PostgreSQL / TimescaleDB
```

### 大模型调用策略

不建议每条 MQTT 数据都调用大模型。推荐先做规则初筛：

```txt
正常数据：只保存，不调用大模型
risk >= 2：进入规则分析，可选调用大模型
risk >= 3：立即调用大模型并发布 alarm
连续异常：调用大模型进行摘要判断
固定周期：对最近一段时间数据做总结
```

可触发大模型的条件示例：

- `gas` 持续高于阈值。
- `risk >= 2`。
- `presence` 状态与预期行为不一致。
- 温湿度异常持续多次。
- 多传感器同时异常。

### 分析结果 Payload

发布到 `eldercare/node01/analysis` 的示例：

```json
{
  "node_id": "node01",
  "source_risk": 2,
  "cloud_risk": 3,
  "summary": "燃气数值持续偏高，建议立即检查厨房通风和燃气阀门。",
  "action": "check_gas_and_ventilate",
  "model_used": true
}
```

发布到 `eldercare/node01/command` 的示例：

```json
{
  "node_id": "node01",
  "command": "cloud_risk_update",
  "risk": 3,
  "message": "gas_high"
}
```

ESP8266 后期可订阅 `command` topic，并通过串口将云端建议转发给 STM32。STM32 是否采纳云端建议，应由本地风险模块决定。

### 推荐技术栈

```txt
Python
paho-mqtt
sqlite3
OpenAI SDK 或其他大模型 HTTP API
Docker
```

建议目录：

```txt
server/
  analysis_service/
    Dockerfile
    requirements.txt
    src/
      main.py
    data/
      eldercare.db
```

后续可加入 `docker-compose.yml`：

```yaml
analysis_service:
  build: ./analysis_service
  container_name: eldercare-analysis
  restart: unless-stopped
  depends_on:
    - mosquitto
  volumes:
    - ./analysis_service/data:/app/data
  environment:
    - MQTT_HOST=mosquitto
    - MQTT_PORT=1883
    - OPENAI_API_KEY=${OPENAI_API_KEY}
```

### 后期开发顺序

1. 实现 MQTT 订阅与日志打印。
2. 将原始状态写入 SQLite。
3. 添加规则分析并发布 `analysis` topic。
4. HA 增加 AI 分析结果展示实体。
5. 高风险时发布 `alarm` topic。
6. 接入短信提醒。
7. 接入大模型进行异常摘要和辅助风险判断。
8. 实现 `command` topic 回传 ESP8266/STM32。
