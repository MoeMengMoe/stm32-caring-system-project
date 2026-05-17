# ESP8266 到 Mosquitto 快速联调

本文档用于今天先打通最小链路：

```txt
NodeMCU ESP8266
  -> Wi-Fi
  -> Docker Mosquitto
  -> mosquitto_sub 收到假 JSON
```

## 1. 启动 Mosquitto 和 Home Assistant

在 Ubuntu 虚拟机中进入仓库的 `server/` 目录：

```bash
cd server
docker compose up -d
```

查看容器状态：

```bash
docker ps
```

确认存在：

```txt
eldercare-mosquitto
eldercare-homeassistant
```

## 2. 测试 Broker

开一个终端订阅：

```bash
docker exec -it eldercare-mosquitto mosquitto_sub -t eldercare/node01/status -v
```

另开一个终端发布：

```bash
docker exec -it eldercare-mosquitto mosquitto_pub -t eldercare/node01/status -m '{"test":1}'
```

订阅端能看到消息，说明 Mosquitto 可用。

## 2.1 打开 Home Assistant

浏览器打开：

```txt
http://<Windows宿主机IP>:8123
```

如果是在 Ubuntu 虚拟机内部访问：

```txt
http://localhost:8123
```

首次进入按页面提示创建管理员账号。

进入 HA 后添加 MQTT 集成：

```txt
Settings
  -> Devices & services
  -> Add integration
  -> MQTT
```

Broker 填：

```txt
mosquitto
```

端口：

```txt
1883
```

当前 MVP 未启用用户名密码，账号密码留空。

项目已经在 `server/homeassistant/config/configuration.yaml` 中预置了 MQTT 传感器。添加 MQTT 集成并重启 HA 后，应能看到 `Node01 Temperature`、`Node01 Humidity`、`Node01 Risk` 等实体。

## 3. 获取 Ubuntu 虚拟机 IP

ESP8266 不能连接 `localhost`，必须连接 Ubuntu 虚拟机的局域网 IP。

在 Ubuntu 中执行：

```bash
hostname -I
```

例如得到：

```txt
192.168.1.100
```

VMware 网络建议使用桥接模式，否则 ESP8266 可能无法访问虚拟机。

## 4. 修改 ESP8266 配置

打开：

```txt
esp8266_firmware/src/main.cpp
```

修改这三项：

```cpp
static const char *WIFI_SSID = "YOUR_WIFI_SSID";
static const char *WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
static const char *MQTT_HOST = "192.168.1.100";
```

`MQTT_HOST` 填 Ubuntu 虚拟机 IP。

## 5. 烧录 NodeMCU

在 VS Code PlatformIO 中选择：

```txt
Project Tasks
  -> nodemcuv2
  -> General
  -> Upload
```

打开串口监视：

```txt
Project Tasks
  -> nodemcuv2
  -> Platform
  -> Monitor
```

也可以使用命令：

```bash
cd esp8266_firmware
pio run -t upload
pio device monitor
```

## 6. 验收现象

ESP8266 串口应输出类似：

```txt
[INFO] ESP8266 MQTT fake-data gateway boot
[INFO] Connecting WiFi: ...
[INFO] WiFi connected, IP: 192.168.1.xxx
[INFO] Connecting MQTT: 192.168.1.100:1883
[INFO] MQTT connected
[INFO] Publish OK: {"node_id":"node01","seq":0,...}
```

Mosquitto 订阅端应看到：

```txt
eldercare/node01/status {"node_id":"node01","seq":0,"temperature":25.6,"humidity":61,"gas":120,"presence":1,"risk":0,"event":"normal"}
```

Home Assistant 中应能看到 Node01 相关实体状态更新，并可添加到 Dashboard。

## 7. 常见问题

- ESP8266 一直连不上 MQTT：检查 `MQTT_HOST` 是否为 Ubuntu 虚拟机 IP，不是 `localhost`。
- ESP8266 能连 Wi-Fi 但连不上 Broker：检查 VMware 是否为桥接模式，Ubuntu 防火墙是否放行 `1883`。
- `docker exec` 测试能通但 ESP8266 不通：检查 ESP8266 和 Ubuntu 是否在同一局域网。
- 串口没有输出：确认 PlatformIO Monitor 波特率为 `115200`。
- 发布失败或重启：优先检查 ESP8266 供电是否稳定。

## 8. 下一阶段

假数据链路打通后，ESP8266 固件预留 STM32 串口 CSV 输入接口：

```txt
temperature,humidity,gas,presence,risk\r\n
```

示例：

```txt
25.6,61.0,120,1,0\r\n
```

ESP8266 会将该 CSV 行转换为 MQTT JSON 并发布到：

```txt
eldercare/node01/status
```

STM32 侧不需要逐字节发送，建议使用 `snprintf` 组包后调用 `HAL_UART_Transmit` 整包发送。
