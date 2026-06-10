# ESP32-S3 + RuView Wi-Fi 人体感应与位置分析部署流程

更新时间：2026-06-05

## 1. 目标与边界

本文档用于给当前项目增加一条新的“无摄像头人体感应”能力链路：

```text
ESP32-S3 CSI 采集节点
  -> Wi-Fi CSI UDP
  -> RuView Sensing Server
  -> Presence / Motion / 粗位置分析
  -> MQTT / Home Assistant / STM32 联动
```

这条链路与仓库当前已有的 `STM32 -> ESP8266 -> MQTT -> Home Assistant` 链路不是替代关系，而是推荐作为并行的感知子系统：

- `STM32U5` 继续负责本地传感器采集、规则判断、蜂鸣器/TFT 等本地执行。
- `ESP32-S3` 负责采集 Wi-Fi CSI，不建议再让 STM32 直接做 CSI 算法。
- `RuView` 运行在 Windows/Linux 主机，负责 CSI 数据处理、存在检测和位置分析。
- 分析结果再通过 MQTT 回灌到现有服务器和可视化链路。

## 2. 当前推荐架构

### 2.1 最小可运行架构

```text
1 个 ESP32-S3
  -> RuView
  -> presence / motion
```

适合先验证：

- 房间内是否有人
- 是否有明显运动
- 实时链路是否稳定

### 2.2 比赛展示推荐架构

```text
2 到 3 个 ESP32-S3
  -> 同一台 RuView Server
  -> 多节点联合分析
  -> zone / 相对位置 / 轨迹趋势
```

说明：

- 单节点更适合“存在/运动”。
- 多节点更适合“位置/分区”。
- 这里的“位置分析”建议先定义为房间区域级别，而不是厘米级坐标。

## 3. 硬件与软件清单

### 3.1 硬件

- `ESP32-S3` 开发板 1 到 3 块
- 数据线
- 2.4 GHz Wi-Fi 网络
- 一台运行 RuView 的主机

### 3.2 软件

- `RuView` 仓库
- `esptool`
- `Docker Desktop`
- `Rust toolchain`
- `Python`

## 4. 与当前项目的接口关系

当前仓库已有主链路：

```text
STM32 传感器
  -> USART2
  -> ESP8266
  -> Mosquitto
  -> Home Assistant
```

新增后推荐变为：

```text
STM32 环境/本地执行链路
  -> ESP8266
  -> Mosquitto

ESP32-S3 Wi-Fi 感知链路
  -> RuView Server
  -> MQTT Bridge
  -> Mosquitto

Mosquitto
  -> Home Assistant
```

建议不要让 `ESP32-S3` 直接替代现有 `ESP8266` 网关，原因如下：

- 当前仓库的 MQTT 和 HA 基础设施已经围绕 `ESP8266` 链路打通。
- `RuView` 的重点是 CSI 感知，不是替代 STM32 主业务控制器。
- 分层更清晰，比赛演示时更容易解释系统职责。

## 5. 部署步骤

### 5.1 拉取 RuView

在单独目录执行：

```bash
git clone https://github.com/ruvnet/RuView.git
cd RuView
```

建议优先使用官方发布的 `ESP32` 固件版本，避免在比赛前自己改底层 CSI 固件。

### 5.2 准备主机环境

安装：

- `Docker Desktop`
- `Rust`
- `Python`
- `esptool`

安装 `esptool`：

```bash
pip install esptool
```

如果是 Windows，确认 `ESP32-S3` 的 USB 串口驱动正常。

### 5.3 烧录 ESP32-S3 固件

优先使用 RuView 发布页提供的预编译固件。

典型烧录命令如下：

```bash
python -m esptool --chip esp32s3 --port COM7 --baud 460800 \
  write_flash --flash_mode dio --flash_size 8MB \
  0x0     firmware/esp32-csi-node/release_bins/bootloader.bin \
  0x8000  firmware/esp32-csi-node/release_bins/partition-table.bin \
  0xf000  firmware/esp32-csi-node/release_bins/ota_data_initial.bin \
  0x20000 firmware/esp32-csi-node/release_bins/esp32-csi-node.bin
```

注意：

- 上面示例适用于 `8 MB flash` 板卡。
- 如果你的板卡是 `4 MB flash`，必须改用对应的 `4 MB` 分区表和固件。
- `COM7` 需要替换为实际串口号。

### 5.4 给节点写入 Wi-Fi 与目标主机信息

固件烧录完成后，再写入部署参数：

```bash
python firmware/esp32-csi-node/provision.py --port COM7 \
  --ssid "你的WiFi名称" \
  --password "你的WiFi密码" \
  --target-ip 192.168.1.20 \
  --node-id 1 \
  --edge-tier 2 \
  --force-partial
```

参数说明：

- `target-ip`：运行 `RuView sensing-server` 的主机 IP。
- `node-id`：每块板唯一编号，多节点部署时依次递增。
- `edge-tier 2`：建议先用完整处理链。
- `force-partial`：避免覆盖未显式传入的已有 NVS 配置。

多节点示例：

```text
node-id 1 -> 客厅门口
node-id 2 -> 床边
node-id 3 -> 卫生间门口
```

### 5.5 放通 UDP 端口

RuView 默认常用到 CSI 数据输入端口 `5005/UDP`。

Windows 管理员 PowerShell：

```powershell
netsh advfirewall firewall add rule name="ESP32 CSI" dir=in action=allow protocol=UDP localport=5005
```

Linux：

```bash
sudo ufw allow 5005/udp
```

### 5.6 启动 RuView 服务

在 `RuView/v2` 目录执行：

```bash
cargo run -p wifi-densepose-sensing-server --release -- \
  --source esp32 \
  --udp-port 5005 \
  --http-port 3000 \
  --ws-port 3001 \
  --ui-path ../ui
```

启动后打开：

```text
http://localhost:3000/ui/index.html
```

如果界面显示硬件在线并持续收到帧，说明 `ESP32-S3 -> RuView` 链路已打通。

## 6. 结果并回当前项目的推荐方式

### 6.1 推荐，不改 STM32 主链路

最稳妥的做法是在 `RuView Server` 旁边增加一个桥接程序：

```text
RuView HTTP / WebSocket 输出
  -> bridge.py / bridge.js
  -> Mosquitto
  -> Home Assistant
```

这样可以不改 STM32 当前采集逻辑，先把 Wi-Fi 感知作为新增能力并进比赛系统。

建议新增 MQTT topic：

```text
eldercare/node01/wifi_presence
eldercare/node01/wifi_zone
eldercare/node01/wifi_motion
```

建议 MQTT JSON：

```json
{
  "node_id": "node01",
  "rf_node": "esp32s3-1",
  "presence": 1,
  "zone": "bedside",
  "motion_level": 0.72,
  "source": "ruview",
  "ts": "2026-06-05T12:00:00+08:00"
}
```

### 6.2 如果要联动 STM32

推荐只把 RuView 的高层结论回传给 STM32，例如：

- `presence`
- `zone`
- `fall_suspect`
- `breathing_abnormal`

不推荐把大规模 CSI 原始数据送回 STM32。

## 7. 部署验收顺序

建议按下面顺序，不要一开始就追求“位置分析 + HA + STM32 全部同时跑通”：

1. 单块 `ESP32-S3` 烧录成功，串口启动正常。
2. 单块 `ESP32-S3` 成功连上 Wi-Fi。
3. `RuView dashboard` 能看到实时数据流。
4. 验证“有人/无人”切换。
5. 再加第二块 `ESP32-S3`，验证多节点同时在线。
6. 再做区域划分与位置分析展示。
7. 最后把结果桥接到 `Mosquitto` 和 `Home Assistant`。

## 8. 比赛答辩时的表述建议

建议把这部分称为：

```text
基于 Wi-Fi CSI 的隐私友好型人体存在与区域感知子系统
```

不建议直接说：

```text
精确人体定位
```

更稳妥的说法：

- 无接触人体存在检测
- 区域级位置分析
- 面向卧室/卫生间等隐私场景的无摄像头感知

## 9. 当前项目落地建议

对于本仓库，推荐分三步推进：

### Step A

先不改 `STM32` 代码，只补：

- `RuView` 独立部署
- `MQTT bridge`
- `Home Assistant` 新实体

### Step B

稳定后再在当前项目文档里补充新的 MQTT topic 和展示面板。

### Step C

最后再决定是否需要把 `RuView` 结论回传给 `STM32` 参与本地风险状态机。

## 10. 已知限制

- 该路线对现场 Wi-Fi 环境、摆位和干扰较敏感。
- 单节点更容易做存在检测，位置分析通常需要多节点。
- 该方案适合作为“比赛展示增强项”，不应在当前阶段替代已有 `PIR + Rd-03 + STM32` 基线方案。
- 如果时间有限，优先完成 `presence + zone`，不要先追求复杂姿态或生命体征。

## 11. 外部参考入口

以下入口用于后续下载固件、核对命令和查看 RuView 当前说明：

- RuView 仓库：`https://github.com/ruvnet/RuView`
- ESP32 CSI 固件说明：`https://github.com/ruvnet/RuView/blob/main/firmware/esp32-csi-node/README.md`
- 端到端部署教程：`https://github.com/ruvnet/RuView/issues/34`

本文档写作时参考的是 `2026-05-21` 更新后的 `Issue #34` 教程口径，文中部署步骤按 `v0.6.6-esp32` 这一版命令组织。若后续 RuView 仓库再次调整目录或端口，应优先以官方仓库最新说明为准。
