# 2.0 英寸 TFT 本地状态页

本文档记录当前 TFT 屏幕的接线、CubeMX 配置、代码逻辑和验收方法。项目引脚的唯一权威来源仍是 `Docs/pinmap.md`。

## 1. 当前结论

- 屏幕实物只有 8 个引脚：`BL / CS / DC / RST / SDA / SCL / VCC / GND`。
- 没有 `SDO`，因此不能读取屏幕控制器 ID。
- 当前代码默认按 `ST7789` 初始化，这是基于“2.0 英寸、240 x 320、8 针只写 SPI”模块形态做出的工程假设，不是已经确认的型号。
- 供电先只使用 `3.3V`。
- 显示内容为温度、湿度、MQ AO 电压、presence、雷达距离和临时 risk。

## 2. 接线

```text
TFT VCC -> NUCLEO 3V3
TFT GND -> NUCLEO GND
TFT SCL -> NUCLEO D13 / PA5 / SPI1_SCK
TFT SDA -> NUCLEO D11 / PA7 / SPI1_MOSI
TFT CS  -> NUCLEO D10 / PD14 / TFT_CS
TFT BL  -> NUCLEO D9  / PD15 / TFT_BL
TFT RST -> NUCLEO D8  / PF12 / TFT_RST
TFT DC  -> NUCLEO D7  / PF13 / TFT_DC
```

不要连接 `D12 / PA6`，因为屏幕没有 `SDO`。屏幕 `SDA/SCL` 是 SPI 数据输入和 SPI 时钟，不是 I2C。

## 3. CubeMX 配置

- `SPI1`：`Transmit Only Master`
- `PA5`：`SPI1_SCK`
- `PA7`：`SPI1_MOSI`
- `PD14`：`GPIO_Output`，User Label `TFT_CS`，初始高电平
- `PD15`：`GPIO_Output`，User Label `TFT_BL`，初始低电平
- `PF12`：`GPIO_Output`，User Label `TFT_RST`，初始高电平
- `PF13`：`GPIO_Output`，User Label `TFT_DC`，初始低电平

SPI 参数：

```text
8-bit
MSB first
CPOL high
CPHA 2 edge
Software NSS
SPI clock about 250 Kbit/s under the current 4 MHz system clock; this is a low-speed timing test for the current breadboard/Dupont-wire setup
```

## 4. 代码结构

```text
Modules/display/tft_lcd.*       -> SPI TFT 底层驱动、初始化、画点/矩形/字符
Modules/display/status_display.* -> 项目状态页，将 SensorMvp 状态映射为屏幕文字
Core/Src/main.c                  -> 初始化屏幕，并在主循环中协作刷新
```

显示层没有使用整屏 framebuffer。上电后只整屏绘制一次静态界面，运行时每次主循环最多刷新一个字符格；Rd-03 UART 已升级为 USART3 RX DMA，显示刷新不再依赖主循环及时轮询串口。

## 5. 上板验收

烧录后串口应出现：

```text
[INFO] tft controller configured st7789
[INFO] status display ready
```

屏幕应显示：

```text
CARING NODE
TEMPERATURE
HUMIDITY
GAS AO
PRESENCE
RADAR DISTANCE
RISK LEVEL
```

传感器数据更新后，数值应跟随 COM6 日志变化。

如果背光亮但无画面：

1. 先确认 `VCC=3.3V`、`GND` 共地。
2. 检查 `SCL -> D13`、`SDA -> D11` 是否误接到 I2C 的 `D15/D14`。
3. 检查 `CS/DC/RST/BL` 是否按 `D10/D7/D8/D9` 接线。
4. 如果接线正确但出现彩色条纹，先不要改引脚。当前 ILI9341 已上板试验且画面更差，因此优先按 ST7789 继续排查：像素发送缓冲、地址窗口、MADCTL 方向、SPI 时序和电源稳定性。
5. 当前代码已把 TFT 像素发送缓冲从函数栈搬到静态区，并把工程栈从 `0x400` 调整到 `0x1000`。
6. 纯色诊断图确认：大块连续像素流会出现彩色条纹，小块分块写入后色带明显干净。因此当前正式驱动保留 `TftLcd_FillRect()` 的分块写入策略，每块重新设置地址窗口，避免长时间连续 `RAMWR` 失步。
