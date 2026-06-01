# NUCLEO-U5A5ZJ-Q Arduino/Zio 丝印与 CubeMX 引脚对应表

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
| D12 | PA6 | GPIO / SPI1_MISO |
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