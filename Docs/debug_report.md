# 调试报告

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

1. 将 ESP8266 固件从假数据发布改为读取 STM32 USART2 JSON 行。
2. 保留假数据模式作为离线测试开关。
3. 在 HA Dashboard 中整理项目展示卡片。
4. 根据演示需要给 Mosquitto 增加账号密码。
5. 将服务器部署和网络转发步骤补充到 `Docs/esp8266_mqtt_quickstart.md`。
