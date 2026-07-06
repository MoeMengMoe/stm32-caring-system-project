# NUCLEO-U5A5ZJ-Q 引脚分配与接线指南

本文档是本项目引脚分配、CubeMX 配置和实物接线的唯一权威来源。

任何涉及引脚、外设实例、Arduino/Zio 逻辑编号、实物丝印、供电方式或接线方式的变化，都必须在同一次提交中更新本文档。最终生成配置仍以 CubeMX `.ioc` 文件为准，修改前需要由硬件负责人 Gary 确认。

## 1. 强制核对规则

任何引脚配置、接线或变动都必须遵守以下规则：

1. 先核对 ST 官方 `UM2861` 板卡用户手册。
2. 再核对与实物一致的板卡修订版原理图。本项目实物为 `MB1549-U5A5ZJQ-C04`。
3. 再核对 STM32U5A5 的引脚复用功能，确认 CubeMX 外设功能成立。
4. 最后对照实物照片或实物正面丝印，确认用户能够实际找到接线孔。
5. 任何焊桥、复用冲突或板卡修订版差异必须明确记录。
6. 未完成以上核对时，结论必须标记为“待核对”，不得指导接线。

实物接线说明必须优先写：

```text
连接器编号 + 连接器针脚号 + 板子朝向 + 列/行位置 + 可见丝印
```

随后才写：

```text
MCU 引脚 + CubeMX 外设功能 + 手册 Zio 逻辑编号
```

> 重要：手册中的 `D35`、`D36` 等 Zio 逻辑编号不会印在这块板子的正面，禁止单独用它们指导接线。

## 2. 先认清各种名字

同一个物理接口可能同时有多种名字，接线文档必须区分它们：

| 名称类型 | 示例 | 用途 |
| --- | --- | --- |
| 连接器针脚 | `CN10 pin 32` | 在 ST 手册和原理图中唯一定位物理孔 |
| Arduino/Zio 逻辑编号 | `A2`、`D15`、`D36` | 在 ST 手册和原理图中辅助定位；`Dxx` 通常不印在板上 |
| 板子正面可见丝印 | `A2`、`SCL`、`TIMER` | 在实物正面辅助寻找接线区域 |
| STM32 MCU 引脚 | `PC3`、`PB8`、`PB10` | 在 CubeMX 中选择和配置引脚 |
| 外设功能 | `ADC1_IN4`、`I2C1_SCL`、`USART3_TX` | 确认引脚当前承担的功能 |

禁止只写其中一种名字。例如不要只写“接 D36”，也不要只写“接 PB10”。推荐格式：

```text
CN10 pin 32 / 黑色排母外侧列倒数第二孔 / PB10 / USART3_TX
```

## 3. 当前项目引脚总表

### 3.1 已配置并正在使用

| 模块 | 实物接线位置 | MCU 引脚 | CubeMX 功能 | 方向 | 当前用途与状态 |
| --- | --- | --- | --- | --- | --- |
| BME280 SCL | `D15 / SCL` | `PB8` | `I2C1_SCL` | STM32 输出时钟 | 已验证，BME280 地址 `0x76` |
| BME280 SDA | `D14 / SDA` | `PB9` | `I2C1_SDA` | 双向 | 已验证，BME280 芯片 ID `0x60` |
| MQ AO 分压采样 | `A2` | `PC3` | `ADC1_IN4` | STM32 输入 | 已验证，读取 MQ 模拟输出 |
| PIR OUT | `A3` | `PB0` | `GPIO_Input`，标签 `PIR_IN`，下拉 | STM32 输入 | 已验证，人体活动检测 |
| Rd-03 OT2 | `A4` | `PC1` | `GPIO_Input`，标签 `RD03_OUT`，下拉 | STM32 输入 | 有人/无人高低电平输出；已验证 |
| Rd-03 RX | `CN10 pin 32`，黑色排母外侧列倒数第二孔，靠板边和金色 Morpho 排针，附近丝印 `TIMER` | `PB10` | `USART3_TX` | STM32 输出 | 向雷达发送配置命令；手册逻辑编号 `D36` |
| Rd-03 OT1 | `CN10 pin 34`，黑色排母外侧列最下面孔，靠板边和金色 Morpho 排针，附近丝印 `TIMER` | `PB11` | `USART3_RX` | STM32 输入 | 雷达串口数据输出；手册逻辑编号 `D35` |
| ESP8266 RX | `A1` | `PA2` | `USART2_TX` | STM32 输出 | STM32 状态 CSV 发往 ESP8266 |
| ESP8266 TX | `A0` | `PA3` | `USART2_RX` | STM32 输入 | 已预留，当前通信模块主要使用 TX |
| ESP32 麦克风 RX | `CN10 pin 29 / D32`，右侧黑色排母靠 MCU 内侧列倒数第 3 个孔 | `PA0` | `UART4_TX` | STM32 输出 | Simon 本地 ESP32 麦克风模块，接 ESP32 `RX`；板上通常不印 `D32` |
| ESP32 麦克风 TX | `A8 / CN10 pin 11` | `PA1` | `UART4_RX` | STM32 输入 | Simon 本地 ESP32 麦克风模块，接 ESP32 `TX`；发送 `C/D` 协议帧 |
| TFT SCL | `CN7 pin 10 / D13` | `PA5` | `SPI1_SCK` | STM32 输出 | 2.0 英寸 TFT 的 SPI 时钟；屏幕丝印 `SCL` 不是 I2C |
| TFT SDA | `CN7 pin 14 / D11` | `PA7` | `SPI1_MOSI` | STM32 输出 | 2.0 英寸 TFT 的 SPI 数据；屏幕丝印 `SDA` 不是 I2C |
| TFT CS | `CN7 pin 16 / D10` | `PD14` | `GPIO_Output`，标签 `TFT_CS` | STM32 输出 | 屏幕片选，低电平有效 |
| TFT BL | `CN7 pin 18 / D9` | `PD15` | `GPIO_Output`，标签 `TFT_BL` | STM32 输出 | 屏幕背光控制 |
| TFT RST | `CN7 pin 20 / D8` | `PF12` | `GPIO_Output`，标签 `TFT_RST` | STM32 输出 | 屏幕硬件复位，低电平有效 |
| TFT DC | `CN10 pin 2 / D7` | `PF13` | `GPIO_Output`，标签 `TFT_DC` | STM32 输出 | 屏幕命令/数据选择 |
| 板载 LED LD1 | 板载 LED，不需要外接 | `PC7` | `GPIO_Output`，标签 `LED_STATUS` | STM32 输出 | 已验证，心跳灯 |
| 调试串口 TX | ST-LINK VCP | `PA9` | `USART1_TX` | STM32 输出 | 已通过 COM6 验证 |
| 调试串口 RX | ST-LINK VCP | `PA10` | `USART1_RX` | STM32 输入 | COM6 调试控制入口，可用键盘触发演示命令 |

### 3.2 已配置但尚未完成上板协议验证

| 模块 | 推荐实物位置 | MCU 引脚 | CubeMX 功能 | 方向 | 当前状态 |
| --- | --- | --- | --- | --- | --- |
| Rd-03 V2 二进制上报 | `CN10 pin 32 / pin 34`，黑色排母外侧列最下面两个孔 | `PB10 / PB11` | `USART3_TX / USART3_RX` | 双向 | `COM6-115200.log` 已验证状态、距离和 32 个距离门能量 |

> 板级核对：本项目实物是 `MB1549-U5A5ZJQ-C04`。C04 官方原理图中 `SB61` 与 `SB62` 均已装配，因此 `PB10` 同时连接到手册逻辑编号 `D27` 和 `D36`。本项目使用 `CN10 pin 32`，因为它与 `PB11 / CN10 pin 34` 位于同一列，便于连续接线。
>
> 物理列核对：ST `UM2861` Table 18 和 C04 原理图中的 `CN10 pin 32/pin 34` 均为偶数脚，位于黑色 `CN10` 排母靠板边、靠金色 ST Morpho 排针的外侧列。靠 MCU 的内侧列最下面两个孔是 `pin 31/pin 33`，不是 `PB10/PB11`。
>
> Rd-03 V2 模块端必须按 PCB 丝印识别：`RX` 是雷达串口输入，`OT1` 是雷达串口数据输出，`OT2` 是有人/无人高低电平输出。官方用户手册部分表格将 `OT1` 写作 `TX`，两者表示同一功能。

### 3.3 PB10/PB11 的真实板级连接

以下是 ST `UM2861` 手册中的板级连接关系。它解释了为什么只看 CubeMX 引脚名仍然可能接错：

| MCU 引脚 | 黑色 Zio 排母连接 | 板上可见信息 | 外侧 ST Morpho 连接 | 本项目结论 |
| --- | --- | --- | --- | --- |
| `PB10` | `CN10 pin 15 / D27` 经 `SB61`；`CN10 pin 32 / D36` 经 `SB62` | CN10 底部附近只看到分组丝印 `TIMER` | `CN12 pin 25` 直连 | 使用 `CN10 pin 32` |
| `PB11` | `CN10 pin 34 / D35` 直连 | CN10 底部附近只看到分组丝印 `TIMER` | `CN12 pin 18` 直连 | 使用 `CN10 pin 34` |

### 3.4 计划使用但尚未配置

| 模块 | 候选板上丝印 | MCU 引脚 | 计划功能 | 状态 |
| --- | --- | --- | --- | --- |
| 无源蜂鸣器模块 IO | `D6` | `PE9` | `TIM1_CH1 PWM` | 已在 CubeMX 配置；当前无源三针模块 `VCC/IO/GND` 使用 TIM1 2 kHz PWM 驱动 |
| 有源蜂鸣器 | `D6` | `PE9` | 备用逻辑输出 | 备用方案；如果换成有源低电平触发蜂鸣器，复用该逻辑脚但需要调整驱动策略 |
| 本地求助自锁按钮 | `D0` | `PG8` | `GPIO_Input`，标签 `SOS_BUTTON`，上拉 | 已在 CubeMX 配置；闭合接地，低电平有效，只在 OFF->ON 边沿触发主动求助/模拟跌倒 |
| 本地确认自锁按钮 | `D1` | `PG7` | `GPIO_Input`，标签 `ACK_BUTTON`，上拉 | 已在 CubeMX 配置；闭合接地，低电平有效，只在 OFF->ON 边沿触发“我没事”确认 |
| HW-280 四路继电器模块 IN1-IN4 | `D3/D4/D5/A5` | `PE13/PF14/PE11/PC0` | `GPIO_Output`，标签 `RELAY1_IN` 到 `RELAY4_IN` | 代码已预留四路继电器输出；CubeMX 待配置并 Generate Code。第一版按高电平触发测试，GPIO 低电平为释放，GPIO 高电平为吸合 |
| OLED / SSD1306 | 与 BME280 共用 `D15/D14` | `PB8/PB9` | `I2C1_SCL/SDA` | 备选，当前不计划使用 |

## 4. 当前接线表

### 4.1 BME280

```text
BME280 VCC -> NUCLEO 3V3
BME280 GND -> NUCLEO GND
BME280 SCL -> NUCLEO D15 / PB8 / I2C1_SCL
BME280 SDA -> NUCLEO D14 / PB9 / I2C1_SDA
```

CubeMX 配置说明：

- 启用 `I2C1`。
- `PB8` 配置为 `I2C1_SCL`。
- `PB9` 配置为 `I2C1_SDA`。
- 当前 `.ioc` 为 PB8/PB9 启用了上拉；模块板通常也自带上拉。

### 4.2 MQ 气体模块 AO

MQ 模块使用外部面包板电源模块供 `5V`。AO 可能高于 STM32 ADC 允许的 `3.3V`，必须经过 `2k/3.3k` 电阻分压。

```text
面包板电源 5V  -> MQ VCC
面包板电源 GND -> MQ GND
面包板电源 GND -> NUCLEO GND

MQ AO -> 2k 电阻 -> ADC 节点 -> 3.3k 电阻 -> GND
ADC 节点 -> NUCLEO A2 / PC3 / ADC1_IN4
```

CubeMX 配置说明：

- 启用 `ADC1`。
- `PC3` 配置为 `ADC1_IN4`，Single-ended。
- 当前代码输出 `mq_adc_mv`、按分压比例反推的 `mq_ao_est_mv`、滤波后的 `mq_filtered_mv`，并维护 `mq_base_mv` / `mq_delta_mv` 用于判断相对变化。
- `SensorMvp_Status_t.gas` 仍然是滤波后的 AO 反推电压，单位近似 `mV`，只作为本地调试量保留；TFT、Wi-Fi 状态帧和 HA/MQTT 对外展示使用 `gas_ppm_est`，它基于 `Rs/R0 = 11.5428 * ppm^(-0.6549)` 和当前环境基线推算，只用于直观显示和阈值参考，不等于经过标准气体标定的计量值。

### 4.3 PIR 人体活动检测

```text
HC-SR501 PIR VCC -> 面包板电源 5V
PIR GND -> NUCLEO GND
PIR OUT -> NUCLEO A3 / PB0 / PIR_IN
```

CubeMX 配置说明：

- `PB0` 配置为 `GPIO_Input`。
- User Label 设置为 `PIR_IN`。
- Pull-up/Pull-down 设置为 `Pull-down`。

### 4.4 Rd-03 人体存在检测

Rd-03 V2 使用 `3.3V` 供电。模块端不要只按“第几个针脚”判断，必须按 PCB 上的 `3V3 / GND / OT1 / RX / OT2` 丝印接线。

```text
Rd-03 3V3 -> NUCLEO 3V3
Rd-03 GND -> NUCLEO GND

Rd-03 OT2 -> NUCLEO A4 / PC1 / RD03_OUT
Rd-03 RX  <- NUCLEO CN10 pin 32 / 黑色排母外侧列倒数第二孔 / PB10 / USART3_TX
Rd-03 OT1 -> NUCLEO CN10 pin 34 / 黑色排母外侧列最下面孔 / PB11 / USART3_RX
```

实物定位：

- 板子正面朝上，ST-LINK USB 口在上方，USB-C 和 RESET 按钮在下方。
- `CN10` 是板子右侧下半段的黑色双排排母，连接器编号 `CN10` 印在它的下方。
- 选择黑色 `CN10` 排母靠板边、靠金色 ST Morpho 排针的外侧列最下面两个孔，不要插到金色 Morpho 排针上。
- 最下面的孔是 `CN10 pin 34 / PB11`，它上面一个孔是 `CN10 pin 32 / PB10`。
- 靠 MCU 的内侧列最下面两个孔是 `pin 33 / pin 31`，不是本项目需要的 USART3 引脚。
- 这一区域附近只能看到分组丝印 `TIMER`，板上不会印 `D35` 或 `D36`。

```text
板子正面朝上，CN10 底部局部示意

                 MCU 方向                         板边 / 金色 Morpho 排针方向
                    |                                         |
                    v                                         v

             黑色 CN10 内侧列                         黑色 CN10 外侧列
             pin 31 / PA8                            pin 32 / PB10 / USART3_TX -> Rd-03 RX
             pin 33 / PE0                            pin 34 / PB11 / USART3_RX <- Rd-03 OT1
                                                         ↑ 最下面一孔

不要使用内侧列 pin 31/pin 33，也不要插到旁边的金色 Morpho 排针。
```

CubeMX 配置说明：

- `PC1` 配置为 `GPIO_Input`。
- User Label 设置为 `RD03_OUT`。
- Pull-up/Pull-down 设置为 `Pull-down`。
- 启用 `USART3`，模式为 `Asynchronous`。
- `PB10` 配置为 `USART3_TX`，连接雷达 `RX`。
- `PB11` 配置为 `USART3_RX`，连接雷达 `OT1`。
- 参数为 `115200 8N1`，无硬件流控。
- `USART3_RX` 使用 `GPDMA1 Channel 1`，DMA Request 为 `GPDMA1_REQUEST_USART3_RX`。
- DMA 方向为 `Peripheral to Memory`，源地址固定，目标地址递增，数据宽度 `Byte / Byte`。
- 启用 `GPDMA1_Channel1_IRQn` 和 `USART3_IRQn`。
- 驱动使用 `HAL_UARTEx_ReceiveToIdle_DMA()`，DMA 回调只搬运字节，协议解析仍在主循环 `Rd03V2_Update()` 中完成。
- 当前驱动启动时切换雷达到二进制上报模式，UART 是主数据源；`OT2` 作为快速兜底和交叉检查。

### 4.5 ESP8266 D1 mini

ESP8266 使用稳定的外部 `5V` 供电，不从 NUCLEO `3V3` 取电。NUCLEO、ESP8266 和外部电源必须共地。当前实物 D1 mini 兼容板的可见丝印为 `5V`、`G`、`RX`、`TX`。

```text
面包板电源 5V  -> D1 mini 5V
面包板电源 GND -> D1 mini G
NUCLEO GND      -> D1 mini G

NUCLEO A1 / PA2 / USART2_TX -> D1 mini RX
NUCLEO A0 / PA3 / USART2_RX <- D1 mini TX
```

CubeMX 配置说明：

- 启用 `USART2`，模式为 `Asynchronous`。
- `PA2` 配置为 `USART2_TX`。
- `PA3` 配置为 `USART2_RX`。
- 参数为 `115200 8N1`，无硬件流控。
- `USART2_TX` 使用 `GPDMA1 Channel 0`，方向为 `Memory to Peripheral`。
- 启用 `GPDMA1 Channel 0` 和 `USART2` 中断。

### 4.5b ESP32 麦克风本地指令模块

Simon 的 ESP32 麦克风模块不走网络，作为本地语音识别输入源接入 STM32。ESP32 不向 STM32 发送原始音频，只发送识别后的语义命令。

```text
ESP32 TX -> NUCLEO A8 / CN10 pin 11 / PA1 / UART4_RX
ESP32 RX <- NUCLEO CN10 pin 29 / D32 / PA0 / UART4_TX
ESP32 GND -> NUCLEO GND
```

ESP32 供电优先使用自己的 USB 或稳定外部电源。若使用外部电源，必须与 NUCLEO 共地。ESP32 和 STM32 都是 `3.3V` 逻辑，UART 信号可直接连接。

`CN10 pin 29 / D32 / PA0` 物理定位：板子正面朝上、USB 口在下方，看右侧黑色长排母 `CN10`，靠 MCU 的内侧列从最下面往上数第 3 个孔。板上通常不会印 `D32`。

底部附近定位参考：

```text
CN10 右侧黑色排母，底部区域：

靠 MCU 内侧列        靠板边外侧列
pin 29 / PA0 / D32   pin 30 / PE15 / D37
pin 31 / PA8 / D33   pin 32 / PB10 / D36
pin 33 / PE0 / D34   pin 34 / PB11 / D35
```

CubeMX 配置：

- 启用 `UART4`，模式为 `Asynchronous`。
- `PA0` 配置为 `UART4_TX`。
- `PA1` 配置为 `UART4_RX`。
- Baud Rate `115200`，`8N1`，无硬件流控。
- 启用 `UART4_IRQn`。
- 不启用 DMA，当前 `Modules/comm/comm_local.c` 使用字节接收中断。

ESP32 发送给 STM32 的协议复用 ESP8266 云端同一套 `C/D` 行协议：

```text
C,request_id,relay_id,ON|OFF + CRLF
D,request_id,command_type,scenario,value + CRLF
```

示例：`D,4001,1,1,1` 触发场景一，`D,4002,2,0,1` 用户确认，`C,4003,1,ON` 打开继电器 1。

### 4.6 2.0 英寸 240 x 320 TFT

实物模块只有 8 个引脚，按 PCB 丝印识别：

```text
BL / CS / DC / RST / SDA / SCL / VCC / GND
```

模块没有 `SDO`，因此不能读取控制器 ID，也不使用 `D12 / PA6 / SPI1_MISO`。当前驱动根据“2.0 英寸、240 x 320、8 针只写 SPI”这一特征默认按 `ST7789` 初始化，但控制器型号仍需以上板画面确认。

```text
TFT VCC -> NUCLEO 3V3
TFT GND -> NUCLEO GND
TFT SCL -> NUCLEO CN7 pin 10 / D13 / PA5 / SPI1_SCK
TFT SDA -> NUCLEO CN7 pin 14 / D11 / PA7 / SPI1_MOSI
TFT CS  -> NUCLEO CN7 pin 16 / D10 / PD14 / TFT_CS
TFT BL  -> NUCLEO CN7 pin 18 / D9  / PD15 / TFT_BL
TFT RST -> NUCLEO CN7 pin 20 / D8  / PF12 / TFT_RST
TFT DC  -> NUCLEO CN10 pin 2 / D7  / PF13 / TFT_DC
```

接线说明：

- 板子正面可直接看到 `D13 / D11 / D10 / D9 / D8 / D7` 丝印，优先按这些 Arduino/Zio 丝印接线。
- 屏幕 `SCL` 是 SPI 时钟，屏幕 `SDA` 是 SPI 单向数据输入，不能接到 BME280 使用的 I2C `SCL/SDA`。
- 屏幕供电先严格使用 `3.3V`，不要接 `5V`。
- 屏幕、NUCLEO 和其他模块必须共地。
- `D12 / PA6` 保持空闲，不接屏幕。

CubeMX 配置说明：

- 启用 `SPI1`，模式为 `Transmit Only Master` 或 `Simplex Transmit Only Master`。
- `PA5` 配置为 `SPI1_SCK`。
- `PA7` 配置为 `SPI1_MOSI`。
- 数据宽度 `8 Bits`，`MSB First`，时钟极性 `High`，时钟相位 `2 Edge`，软件 NSS。
- `PD14 / PF13 / PF12 / PD15` 配置为 `GPIO_Output`，标签分别为 `TFT_CS / TFT_DC / TFT_RST / TFT_BL`。
- 初始电平：`TFT_CS=High`、`TFT_RST=High`、`TFT_DC=Low`、`TFT_BL=Low`。
- 当前主循环时钟为 `80 MHz`，SPI1 预分频为 `16`，实际时钟约为 `5 Mbit/s`。显示层采用静态界面一次绘制、运行时按数值字段协作刷新；雷达 UART 已改为 USART3 RX DMA，避免显示刷新或日志输出时阻塞接收。

### 4.7 HW-280 四路继电器模块

当前实物为红色 `HW-280` 四路继电器模块，板上丝印包含 `4 Relay Module High/Low Level Trigger`。继电器本体为 `JQC3F-05VDC-C`，因此继电器线圈侧按 `5V` 模块处理。第一阶段只接低压 LED 演示，不接市电负载。

模块低压控制端按照片可识别为：

```text
DC+ / DC- / IN1 / IN2 / IN3 / IN4
```

低压控制侧接线：

```text
继电器 DC+ -> 外部 5V
继电器 DC- -> 外部 GND
NUCLEO GND -> 外部 GND / 继电器 DC-

继电器 IN1 -> NUCLEO D3 / PE13 / RELAY1_IN
继电器 IN2 -> NUCLEO D4 / PF14 / RELAY2_IN
继电器 IN3 -> NUCLEO D5 / PE11 / RELAY3_IN
继电器 IN4 -> NUCLEO A5 / PC0  / RELAY4_IN
```

触发方式：

- 第一版按高电平触发测试。把模块对应跳帽拨到 `H` 或 High Level Trigger 位置。
- 当前代码中 `GPIO Low = 继电器释放`，`GPIO High = 继电器吸合`。
- 选择高电平触发的原因是 STM32 GPIO 是 `3.3V`，高触发时默认输出低电平关断，更不容易在复位或上电阶段误吸合。
- 如果现场发现高触发下 `3.3V` 不能可靠吸合，先停止，不要直接改成低触发硬接；后续改成三极管/驱动板隔离，或确认 `INx` 不会被 5V 上拉后再改 `BOARD_IO_RELAY_ACTIVE_LOW`。

CubeMX 配置说明：

- `PE13` 配置为 `GPIO_Output`，User Label `RELAY1_IN`，初始 `GPIO_PIN_RESET`，No pull。
- `PF14` 配置为 `GPIO_Output`，User Label `RELAY2_IN`，初始 `GPIO_PIN_RESET`，No pull。
- `PE11` 配置为 `GPIO_Output`，User Label `RELAY3_IN`，初始 `GPIO_PIN_RESET`，No pull。
- `PC0` 配置为 `GPIO_Output`，User Label `RELAY4_IN`，初始 `GPIO_PIN_RESET`，No pull。
- Generate Code 后，`main.h` 应生成 `RELAY1_IN_Pin` 到 `RELAY4_IN_Pin` 以及对应 `GPIO_Port` 宏。

每一路继电器的负载端通常是三端触点：`NC / COM / NO`。最终接线必须以模块背面或端子旁实际丝印为准；如果只是做 LED “打开才亮”的演示，优先使用 `COM` 与 `NO` 这对常开触点。

低压 LED 负载推荐接法：

```text
LED 电源正极 -> 继电器 COM
继电器 NO   -> LED 红线 / LED 正极
LED 黑线 / LED 负极 -> LED 电源负极
```

继电器只是一个开关，不直接给 LED 供电。LED 必须有自己的合适电源；如果 LED 模块额定电压未知，先用限流电源或串联合适电阻测试，禁止直接接高电压。

当前软件联动规则：

- `relay_state_mask` 是最终硬件输出状态，bit0-bit3 分别对应继电器 1-4。
- `manual mask` 来自 COM6 `r/t/y/u` 或云端 MQTT 继电器命令。
- `auto mask` 来自 STM32 本地状态机。
- 正常输出为 `manual mask | auto mask`；但用户 ACK 或 clear alarm 属于本地确认优先动作，会先清空 `manual mask`，再按状态机重算 `auto mask` 并立刻刷新硬件输出。
- 继电器 1：护理告警灯。`ACK_WAIT`、`ALARM`、`NO_RESPONSE` 时自动吸合；用户 ACK 或清除告警后自动释放。若该灯之前由 COM6 或云端手动打开，也会被 ACK/clear 一并关闭。
- 继电器 2：离线提示灯。`APP_NETWORK_OFFLINE` 时自动吸合；网络恢复后自动释放。若只是被 COM6 或云端手动打开，则 ACK/clear 会关闭它；若网络仍处于离线自动条件，则 `auto mask` 会继续保持它打开。
- 继电器 3/4：当前只作为手动/云端控制预留。
- 如果云端请求关闭某一路，但本地 `auto mask` 仍要求它打开，STM32 会保持最终输出为 ON，并在继电器结果帧中回传最终状态。

### 4.8 本地按钮与无源蜂鸣器

本地按钮和蜂鸣器用于不依赖云端的最小护理闭环：本地 SOS 自锁按钮触发求助/模拟跌倒场景，本地 ACK 自锁按钮确认“我没事”，无源蜂鸣器根据业务状态发声。

候选引脚如下，启用前必须在 CubeMX 中配置并 Generate Code：

```text
SOS 自锁按钮一端 -> NUCLEO D0 / PG8 / SOS_BUTTON
SOS 自锁按钮另一端 -> NUCLEO GND

ACK 自锁按钮一端 -> NUCLEO D1 / PG7 / ACK_BUTTON
ACK 自锁按钮另一端 -> NUCLEO GND

无源蜂鸣器 VCC -> NUCLEO 3V3
无源蜂鸣器 GND -> NUCLEO GND
无源蜂鸣器 IO  -> NUCLEO D6 / PE9 / TIM1_CH1
```

CubeMX 配置说明：

- `PG8` 配置为 `GPIO_Input`，User Label `SOS_BUTTON`，Pull-up。
- `PG7` 配置为 `GPIO_Input`，User Label `ACK_BUTTON`，Pull-up。
- `PE9` 配置为 `TIM1_CH1`，模式 `PWM Generation CH1`。TIM1 使用内部时钟，`Prescaler=79`，`Period=499`，得到约 2 kHz PWM；不要开启 `TriggerSource_ITR1` 或 Slave Mode。
- 当前按钮为自锁按钮，代码只在“释放 -> 按下锁住”的边沿触发一次；保持锁住不会重复触发。每次测试后需要再按一次让按钮释放，为下一次触发复位。
- 如果上电时按钮已经处于锁住状态，代码会把它当作初始状态，不会立刻触发事件；需要先释放再按下。
- 当前实物是 6 脚自锁按钮，按两组独立触点处理。项目只使用其中一组的两个脚，另一组保持悬空；不要把 6 个脚全部接入电路。
- 临时验收时可以不接实体按钮，直接用一根杜邦线短接 `D0 -> GND` 模拟 SOS，短接 `D1 -> GND` 模拟 ACK。每次触发后必须先断开，再短接下一次。
- 6 脚按钮常见排布为每排 3 个脚：中间脚通常是 `COM`，两侧分别是 `NO/NC`，但最终必须用万用表蜂鸣档确认。选择“释放时断开、锁住时导通”的那一对作为 `GPIO <-> GND`。
- 当前无源蜂鸣器由 `Modules/board/board_io.c` 控制 TIM1 PWM。空闲时 PWM 关闭；`ACK_WAIT`、`ALARM`、`NO_RESPONSE` 或气体风险较高时开启 2 kHz、约 50% 占空比的提示音。
- 若后续换成有源低电平触发蜂鸣器，仍可复用 `D6 / PE9`，但驱动逻辑需要从 PWM 改成电平控制。

### 4.9 COM6 调试控制入口

调试串口 `USART1 / ST-LINK VCP / COM6` 除了输出日志，也支持单字符控制命令，用于现场不依赖实体按钮、ESP8266 或云端时快速演示状态机：

```text
s 或 S -> 触发本地 SOS / 模拟跌倒
a 或 A -> 触发本地 ACK / 我没事
c 或 C -> 清除当前告警
1      -> 远程触发场景一：主动求助 / 模拟跌倒
2      -> 远程触发场景二：长时间静止无响应
o 或 O -> 模拟网络离线
n 或 N -> 模拟网络恢复
p 或 P -> 打印当前传感器与 app 状态摘要
b 或 B -> 蜂鸣器测试，响约 1 秒，不改变业务状态
r 或 R -> 切换手动继电器 1
t 或 T -> 切换手动继电器 2
y 或 Y -> 切换手动继电器 3
u 或 U -> 切换手动继电器 4
h 或 ? -> 打印帮助
```

这些命令只经过调试串口，不改变任何硬件接线。继电器命令会改变 `manual mask`，最终输出仍会与 `auto mask` 合并；正式演示时仍优先使用实体触发、dashboard 或云端命令，COM6 命令作为排障和兜底入口。

## 5. 常用 Arduino/Zio 逻辑编号对照

以下表格只记录本项目常用或容易混淆的逻辑编号。完整对照请查阅 ST 官方 `UM2861` 用户手册中的 Zio connector pinout。`Dxx` 逻辑编号通常不印在板子正面，不能单独用于指导实物接线。

| Arduino/Zio 逻辑编号 | MCU 引脚 | 本项目用途 | 重要说明 |
| --- | --- | --- | --- |
| `D32` | `PA0` | ESP32 麦克风 `UART4_TX` | 对应 `CN10 pin 29`；板上通常不印 `D32`，按物理孔定位 |
| `A8` | `PA1` | ESP32 麦克风 `UART4_RX` | 对应 `CN10 pin 11` |
| `A0` | `PA3` | ESP8266 `USART2_RX` | 不是 `PA0` |
| `A1` | `PA2` | ESP8266 `USART2_TX` | 不是 `PA1` |
| `A2` | `PC3` | MQ `ADC1_IN4` | 不是 `PA2` |
| `A3` | `PB0` | PIR 输入 | 不是 `PA3` |
| `A4` | `PC1` | Rd-03 `OT2` 数字输出 | 不是 `PA4` |
| `A5` | `PC0` | 继电器 4 `RELAY4_IN` | CubeMX 待配置；第一版高电平触发 |
| `D14 / SDA` | `PB9` | I2C1 SDA | BME280/OLED 共用 |
| `D15 / SCL` | `PB8` | I2C1 SCL | BME280/OLED 共用 |
| `D13` | `PA5` | TFT `SPI1_SCK` | 对应 `CN7 pin 10` |
| `D12` | `PA6` | 当前未使用 | TFT 没有 `SDO`，不要接屏幕 |
| `D11` | `PA7` | TFT `SPI1_MOSI` | 对应 `CN7 pin 14` |
| `D10` | `PD14` | TFT `CS` | 对应 `CN7 pin 16` |
| `D9` | `PD15` | TFT `BL` | 对应 `CN7 pin 18` |
| `D8` | `PF12` | TFT `RST` | 对应 `CN7 pin 20` |
| `D7` | `PF13` | TFT `DC` | 对应 `CN10 pin 2` |
| `D6` | `PE9` | 无源蜂鸣器 `TIM1_CH1 PWM` | 已在 CubeMX 配置 |
| `D5` | `PE11` | 继电器 3 `RELAY3_IN` | CubeMX 待配置；第一版高电平触发 |
| `D4` | `PF14` | 继电器 2 `RELAY2_IN` | CubeMX 待配置；第一版高电平触发 |
| `D3` | `PE13` | 继电器 1 `RELAY1_IN` | CubeMX 待配置；第一版高电平触发 |
| `D35` | `PB11` | USART3 RX，接 Rd-03 `OT1` | 对应 `CN10 pin 34`；板上不印 `D35` |
| `D36` | `PB10` | USART3 TX，接 Rd-03 `RX` | 对应 `CN10 pin 32`；板上不印 `D36` |
| `D27` | `PB10` | 当前未使用 | 对应 `CN10 pin 15`；与 `D36` 是同一个 MCU 引脚 |
| `D0` | `PG8` | 本地 SOS 自锁按钮 | 已在 CubeMX 配置；闭合接地，低电平有效 |
| `D1` | `PG7` | 本地 ACK 自锁按钮 | 已在 CubeMX 配置；闭合接地，低电平有效 |

## 6. 修改引脚时的强制检查清单

每次新增或修改引脚，提交前必须逐项确认：

- [ ] 查 NUCLEO-U5A5ZJ-Q 用户手册或原理图，确认板上丝印与 MCU 引脚映射。
- [ ] 写出连接器编号和连接器针脚号，并用板子朝向、列/行位置说明用户如何在实物上找到它。
- [ ] 检查所谓“丝印”是否真的印在板子上，禁止把手册信号名或 Zio `Dxx` 逻辑编号误称为板上丝印。
- [ ] 在 CubeMX 中修改，不直接编辑 `.ioc`。
- [ ] 记录连接器针脚、Arduino/Zio 逻辑编号、实物可见丝印、MCU 引脚和外设功能。
- [ ] 检查电压等级、供电电流和是否需要共地。
- [ ] 检查是否与现有 ADC、I2C、UART、调试口或板载器件冲突。
- [ ] 重新 Generate Code，并在 CLion 中重新加载 CMake。
- [ ] 编译通过后再接线和烧录。
- [ ] 更新本文档的总表、接线表和 CubeMX 配置说明。
- [ ] 在 `Docs/debug_report.md` 记录上板验证结果。

## 7. 参考依据

- ST 官方用户手册：`UM2861 STM32U5 Nucleo-144 board (MB1549)`，重点查看 Table 18 `CN10 pinout`
  `https://www.st.com/resource/en/user_manual/um2861-stm32u5-nucleo144-board-mb1549-stmicroelectronics.pdf`
- ST 官方原理图：`MB1549-U5A5ZJQ-C04`
  `https://www.st.com/resource/en/schematic_pack/mb1549-u5a5ziq-c04-schematic.pdf`
- 当前 CubeMX 配置：仓库根目录 `stm32-caring-system-project.ioc`
