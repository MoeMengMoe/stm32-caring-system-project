# Rd-03 V2 雷达智能层

本文记录项目当前对 Rd-03 V2 的定位、已实现能力和下一步算法路线。

## 1. 产品定位

Rd-03 V2 不再只作为“有人/无人”开关使用，而是作为老人居家看护节点的空间感知核心。

当前可用数据：

- `presence`：雷达是否检测到人体存在
- `distance_cm`：目标距离
- `gate_energy[32]`：32 个距离门能量，每个距离门约 10 cm
- `OT2`：雷达数字有人/无人输出，用作交叉检查

工程边界：

- 单个 Rd-03 V2 不是摄像头，也不是完整 2D/3D 姿态雷达。
- 当前不直接宣称“雷达单独识别摔倒”。
- 项目要做的是多传感器融合：雷达空间状态 + PIR 活动 + MQ 气体 + 环境 + 联网告警。

## 2. 已实现：Radar Features V1

新增模块：

```text
Modules/sensor/radar_features.h
Modules/sensor/radar_features.c
```

它把 Rd-03 原始帧转换为可供状态机使用的特征：

| 字段 | 含义 |
| --- | --- |
| `radar_zone` | 一维房间区域编号 |
| `radar_peak_gate` | 能量最高的距离门 |
| `radar_peak_gate_cm` | 峰值距离门对应距离 |
| `radar_peak_energy` | 峰值能量 |
| `radar_energy_sum` | 32 个距离门总能量 |
| `radar_motion_score` | 相邻采样之间的能量变化评分 |
| `radar_active_gate_count` | 峰值附近有效距离门数量 |
| `radar_occupied_seconds` | 雷达持续有人时间 |
| `radar_still_seconds` | 有人但低运动评分持续时间 |
| `radar_last_seen_age_ms` | 上次有人后经过时间 |

当前屏幕显示：

```text
RADAR ZONE: 85cm Z2
```

含义：目标距离约 85 cm，位于区域 2。

## 3. 当前区域划分

这是临时的一维房间建模，后续应按真实安装位置重新标定。

| Zone | 距离范围 | 暂定语义 |
| --- | --- | --- |
| `Z1` | `< 80 cm` | 近处/设备前 |
| `Z2` | `80-180 cm` | 活动区 |
| `Z3` | `180-300 cm` | 沙发/床/休息区候选 |
| `Z4` | `>= 300 cm` | 远端/门口候选 |
| `Z5` | 无人 | CLEAR |

## 4. 下一步

优先级从高到低：

1. 将 Rd-03 USART3 接收从轮询升级为 DMA 或 Receive-to-Idle，避免屏幕、日志、MQTT 同时工作时丢帧。
2. 做一轮现场标定：记录人站在不同距离和区域时的 `distance_cm / zone / peak_gate / motion_score`。
3. 把 `radar_cm / zone / motion_score / still_seconds` 加入 MQTT 上报。
4. 实现正式老人看护事件状态机：
   - `NORMAL`
   - `OCCUPIED_STILL`
   - `LEFT_ROOM`
   - `POSSIBLE_FALL`
   - `GAS_WITH_OCCUPANCY`
   - `NO_RESPONSE`
5. 再考虑 AI 层：AI 不直接吃散乱串口日志，而是吃稳定的雷达特征和事件窗口。

## 5. 可视化学习页

用于理解雷达正面、扇形探测区、32 个距离门、zone 和测试动作的交互式页面：

```text
Docs/radar_visual_guide.html
```

直接用浏览器打开即可，不需要启动开发服务器。
