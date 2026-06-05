# NUCLEO-U5A5ZJ-Q Arduino/Zio 逻辑编号与 CubeMX 引脚对应表

本文档仅作为较完整的板卡丝印参考。项目实际分配、CubeMX 配置和接线方式统一以 `Docs/pinmap.md` 为准。

> 易错提醒：标准 Arduino `D1/D0` 默认对应 `PG7/PG8`。本项目的 Rd-03 USART3 使用 `PB10/PB11`，实物接线位置是 `CN10 pin 32/pin 34`，也就是黑色 CN10 排母靠板边和金色 Morpho 排针的外侧列最下面两个孔。Zio `D36/D35` 是手册逻辑编号，不会印在板子正面。

## 1. Analog 模拟口

| 板子 Arduino 丝印 | CubeMX 引脚 | 常见用途 |
|---|---|---|
| A0 | PA3 | GPIO / ADC / USART2_RX |
| A1 | PA2 | GPIO / ADC / USART2_TX |
| A2 | PC3 | GPIO / ADC |
| A3 | PB0 | GPIO / ADC |
| A4 | PC1 | GPIO / ADC |
| A5 | PC0 | GPIO / ADC |
| A6 | PB1 | GPIO / ADC |
| A7 | PC2 | GPIO / ADC |
| A8 | PA1 | GPIO / ADC |

## 2. Digital 数字口

| 板子 Arduino 丝印 | CubeMX 引脚 | 常见用途 |
|---|---|---|
| D0 | PG8 | GPIO / LPUART1_RX |
| D1 | PG7 | GPIO / LPUART1_TX |
| D2 | PF15 | GPIO |
| D3 | PE13 | GPIO |
| D4 | PF14 | GPIO |
| D5 | PE11 | GPIO |
| D6 | PE9 | GPIO |
| D7 | PF13 | GPIO |
| D8 | PF12 | GPIO |
| D9 | PD15 | GPIO |
| D10 | PD14 | GPIO / SPI_CS 可选 |
| D11 | PA7 | GPIO / SPI1_MOSI |
| D12 | PA6 | GPIO / SPI1_MISO，当前不接 TFT |
| D13 | PA5 | GPIO / SPI1_SCK |

## 3. I2C 接口

| 板子 Arduino 丝印 | CubeMX 引脚 | 常见用途 |
|---|---|---|
| D14 / SDA | PB9 | I2C_SDA |
| D15 / SCL | PB8 | I2C_SCL |

## 4. 电源与控制口

| 板子 Arduino 丝印 | CubeMX 引脚 | 说明 |
|---|---|---|
| IOREF | — | Arduino shield 参考电压 |
| RESET | NRST | 复位脚 |
| 3V3 | — | 3.3V 电源 |
| 5V | — | 5V 电源 |
| GND | — | 地 |
| VIN | — | 外部输入电源 |

## 5. 串口连接 Wemos D1 mini 示例

如果使用 USART2：

| STM32U5 CubeMX 引脚 | U5 板子丝印 | 功能 | Wemos D1 mini |
|---|---|---|---|
| PA2 | A1 | USART2_TX | RX |
| PA3 | A0 | USART2_RX | TX |
| GND | GND | 共地 | G / GND |

接线规则：

```text
U5 PA2 / A1 / USART2_TX  ->  Wemos RX
U5 PA3 / A0 / USART2_RX  ->  Wemos TX
U5 GND                   ->  Wemos GND
```

## 6. 当前 TFT 接线速查

| 屏幕丝印 | 板子 Arduino 丝印 | CubeMX 引脚 | 当前功能 |
|---|---|---|---|
| SCL | D13 | PA5 | SPI1_SCK |
| SDA | D11 | PA7 | SPI1_MOSI |
| CS | D10 | PD14 | TFT_CS |
| BL | D9 | PD15 | TFT_BL |
| RST | D8 | PF12 | TFT_RST |
| DC | D7 | PF13 | TFT_DC |
| VCC | 3V3 | — | 3.3V 供电 |
| GND | GND | — | 共地 |

屏幕没有 `SDO`，所以 `D12 / PA6` 不接。
