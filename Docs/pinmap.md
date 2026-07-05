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
| TFT SCL | `CN7 pin 10 / D13` | `PA5` | `SPI1_SCK` | STM32 输出 | 2.0 英寸 TFT 的 SPI 时钟；屏幕丝印 `SCL` 不是 I2C |
| TFT SDA | `CN7 pin 14 / D11` | `PA7` | `SPI1_MOSI` | STM32 输出 | 2.0 英寸 TFT 的 SPI 数据；屏幕丝印 `SDA` 不是 I2C |
| TFT CS | `CN7 pin 16 / D10` | `PD14` | `GPIO_Output`，标签 `TFT_CS` | STM32 输出 | 屏幕片选，低电平有效 |
| TFT BL | `CN7 pin 18 / D9` | `PD15` | `GPIO_Output`，标签 `TFT_BL` | STM32 输出 | 屏幕背光控制 |
| TFT RST | `CN7 pin 20 / D8` | `PF12` | `GPIO_Output`，标签 `TFT_RST` | STM32 输出 | 屏幕硬件复位，低电平有效 |
| TFT DC | `CN10 pin 2 / D7` | `PF13` | `GPIO_Output`，标签 `TFT_DC` | STM32 输出 | 屏幕命令/数据选择 |
| 板载 LED LD1 | 板载 LED，不需要外接 | `PC7` | `GPIO_Output`，标签 `LED_STATUS` | STM32 输出 | 已验证，心跳灯 |
| 调试串口 TX | ST-LINK VCP | `PA9` | `USART1_TX` | STM32 输出 | 已通过 COM6 验证 |
| 调试串口 RX | ST-LINK VCP | `PA10` | `USART1_RX` | STM32 输入 | 调试串口接收预留 |

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
| 蜂鸣器 | `D22` | `PB5` | `GPIO_Output` 或定时器 PWM | 尚未在 CubeMX 配置，接线前必须再次确认 |
| 有源蜂鸣器 | 待定 | 待定 | `GPIO_Output` | 计划使用 `3.3V` 供电、低电平触发；尚未分配引脚，不要接线 |
| 本地求助按钮 | 待定 | 待定 | `GPIO_Input` | 两个本地按钮之一；用于主动求助/模拟跌倒，尚未分配引脚 |
| 本地确认按钮 | 待定 | 待定 | `GPIO_Input` | 两个本地按钮之一；用于“我没事”确认，尚未分配引脚 |
| HW-280 四路继电器模块 IN1-IN4 | 待定 | 待定 | `GPIO_Output` | 4 路能力，现场先接 2 个 LED 负载；模块为 5V 继电器、支持高/低电平触发，计划低电平触发。接线前必须确认电平隔离方案，禁止直接按猜测接 STM32 GPIO |
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
- 当前代码输出 `mq_adc_mv` 和按分压比例反推的 `mq_ao_est_mv`。

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
- 当前主循环时钟为 `4 MHz`，SPI1 预分频为 `16`，实际时钟约为 `250 Kbit/s`。显示层采用静态界面一次绘制、运行时单字符协作刷新；雷达 UART 已改为 USART3 RX DMA，避免显示刷新或日志输出时阻塞接收。

### 4.7 HW-280 四路继电器模块

当前实物为红色 `HW-280` 四路继电器模块，板上丝印包含 `4 Relay Module High/Low Level Trigger`。继电器本体为 `JQC3F-05VDC-C`，因此继电器线圈侧按 `5V` 模块处理。

模块低压控制端按照片可识别为：

```text
DC+ / DC- / IN1 / IN2 / IN3 / IN4
```

初步接线原则：

- `DC+` 是继电器模块控制侧电源正端，按 `5V` 供电处理。
- `DC-` 是继电器模块控制侧地，若由 STM32 GPIO 控制，必须与 NUCLEO `GND` 共地。
- `IN1` 到 `IN4` 分别对应四路继电器控制输入。
- 板上高/低触发跳帽第一版计划拨到低电平触发位置；低电平触发时，GPIO 输出低电平表示继电器吸合，输出高电平表示继电器释放。
- 由于模块电源为 `5V`，在未确认输入端电路与 3.3V GPIO 兼容前，不允许把 STM32 GPIO 直接接到 `IN1-IN4`。
- 第一阶段负载只接低压 LED 演示，不接市电负载。

每一路继电器的负载端通常是三端触点：`NC / COM / NO`。最终接线必须以模块背面或端子旁实际丝印为准；如果只是做 LED “打开才亮”的演示，优先使用 `COM` 与 `NO` 这对常开触点。

当前状态：软件侧已经预留四路继电器状态 `relay_state_mask` 和云端命令闭环，但硬件 GPIO 尚未分配，CubeMX 尚未配置继电器输出引脚。

## 5. 常用 Arduino/Zio 逻辑编号对照

以下表格只记录本项目常用或容易混淆的逻辑编号。完整对照请查阅 ST 官方 `UM2861` 用户手册中的 Zio connector pinout。`Dxx` 逻辑编号通常不印在板子正面，不能单独用于指导实物接线。

| Arduino/Zio 逻辑编号 | MCU 引脚 | 本项目用途 | 重要说明 |
| --- | --- | --- | --- |
| `A0` | `PA3` | ESP8266 `USART2_RX` | 不是 `PA0` |
| `A1` | `PA2` | ESP8266 `USART2_TX` | 不是 `PA1` |
| `A2` | `PC3` | MQ `ADC1_IN4` | 不是 `PA2` |
| `A3` | `PB0` | PIR 输入 | 不是 `PA3` |
| `A4` | `PC1` | Rd-03 `OT2` 数字输出 | 不是 `PA4` |
| `D14 / SDA` | `PB9` | I2C1 SDA | BME280/OLED 共用 |
| `D15 / SCL` | `PB8` | I2C1 SCL | BME280/OLED 共用 |
| `D13` | `PA5` | TFT `SPI1_SCK` | 对应 `CN7 pin 10` |
| `D12` | `PA6` | 当前未使用 | TFT 没有 `SDO`，不要接屏幕 |
| `D11` | `PA7` | TFT `SPI1_MOSI` | 对应 `CN7 pin 14` |
| `D10` | `PD14` | TFT `CS` | 对应 `CN7 pin 16` |
| `D9` | `PD15` | TFT `BL` | 对应 `CN7 pin 18` |
| `D8` | `PF12` | TFT `RST` | 对应 `CN7 pin 20` |
| `D7` | `PF13` | TFT `DC` | 对应 `CN10 pin 2` |
| `D35` | `PB11` | USART3 RX，接 Rd-03 `OT1` | 对应 `CN10 pin 34`；板上不印 `D35` |
| `D36` | `PB10` | USART3 TX，接 Rd-03 `RX` | 对应 `CN10 pin 32`；板上不印 `D36` |
| `D27` | `PB10` | 当前未使用 | 对应 `CN10 pin 15`；与 `D36` 是同一个 MCU 引脚 |
| `D0` | `PG8` | 当前未使用 | 默认是 `LPUART1_RX` |
| `D1` | `PG7` | 当前未使用 | 默认是 `LPUART1_TX` |

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
