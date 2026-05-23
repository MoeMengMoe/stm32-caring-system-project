# Stm32-Caring-System-Project 项目规范指南

## 目录与TODO

- [x] Git 规范
- [x] 文件树规范
- [x] UART/MQTT 协议规范
- [x] GPIO/硬件分配表
- [x] CubeMX 管理规范
- [x] 调试日志规范
- [x] 人员分工

## Git 工作树与工作要求

### 分支管理

- 对仓库先`clone`自己的仓库, 避免直接修改远程
- 我们使用`main`, `dev`, `exp`, `feature/...`,`fix/...`, 这几条分支，不要使用或创建其他分支
- 除初始化与接受PR，其他情况一律不允许在`main`分支修改或者使用`Force`强制提交
- 允许在`feature`分支下创建新功能, `fix`分支只接受修补

### 提交要求

- 勤提交, 建议每修改一处功能都提交一次, 比如完成一个函数模块, 修复某处bug
- 完整的提交信息, 使用`feat`等字词注明修改点和注意事项
- 先pull后push, 勤拉取最新分支
- 如果使用AI参与某处修改或功能, 请在提交信息中注明经手AI
- 使用`.gitignore`排除构建产物, 临时文件, 包括`.vscode/.idea/.cache`等文件夹

### PR要求

- 所有分支的merge必须先与`exp`分支操作, 测试通过后可以与主分支merge
- 主分支merge时, 必须要求有仓库持有人操作, 必须同时@其他人进行多次代码review, 明确TODO与可能存在的问题
- 与`exp`分支合并时无需和其他人协商, 以加快开发进度
- PR信息请务必提交完整规范, 直接根据前几次提交信息合并提及即可
- push到主仓库时, 先知会拥有者
- `.ioc`文件由硬件开发者先配置完整取并集后, 在统一取新的版本

## 分支结构

- `main`：稳定版本
- `dev`：开发集成版本
- `feature/...`：个人功能开发分支

## 开发流程

1. `clone`仓库到本地, `checkout`到`dev`分支
2. 从 `dev` 创建 `feature/...`
3. 在 feature 分支开发
4. push 到远程
5. 提交 PR 到 `dev`
6. 测试后 merge
7. 周期性从 `dev` merge 到 `main`

## 项目文件结构

### 文件夹结构示意图

```
Project/
│
├── Core/                  # CubeMX 生成核心代码
│   ├── Inc/
│   └── Src/
│
├── Drivers/               # STM32 HAL/CMSIS
│
├── Modules/               # 自己写的模块
│   ├── oled/
│   ├── sensor/
│   ├── buzzer/
│   ├── mqtt/
│   ├── protocol/
│   └── risk/
│
├── Docs/                  # 文档
│   ├── protocol.md
│   ├── pinmap.md
│   └── meeting.md
│
├── Test/                  # 测试代码
│
├── README.md
├── .gitignore
├── xxx.ioc
└── CMakeLists.txt
```

### 文件结构要求

- `Core`
  - 记得在MX中打开为每对外设创建.c/.h文件
  - 不要将模块写在改文件夹下
- `Modules`
  - 为外设写的驱动文件等的安放位置
  - 状态机, 观察者, MQTT等模块也可以写在此处, 用文件夹区分
- `Docs`
  - 需要包含以下文件
    - `pinmap.md`, `protocol.md`, `debug_report`, `bugs.md`, `TODO.md`,`NOTICE.md`, `dev_log.md`
    - 所有文档均使用中文书写 建议使用AI完善
    - `protocol.md` 需要与通信开发者商定通信包结构
    - `debug_report` 总结调试配置经验
    - `bugs.md` 总结已经改正的bug和尚未修复的bug
    - `NOTICE.md` 用于提示其他开发者, 可能有奇妙的地方不可修改或硬性要求
    - `TODO.md` 用于展示所有开发目标
    - `dev_log.md` 展示开发历程(optional)和心境,  用于答辩
- `.ioc`
  - 此文件由硬件开发主要负责人控制 一切以最新版本为准
- `CMakeLists.txt`
  - 所有不在`core`文件夹下的文件若要作为文件头或参与编译, 都必须在此文件中使用脚本语言来add library 并重新加载cmake项目

## 开发工具与环境配置

- 核心开发者建议使用Clion+arm-gcc 来开发 非核心开发者可使用VSCode或CubeIDE来进行测试, 调整, 与规划
- 全部使用CubeMX统一代码生成, 和引脚配置
  - **注意, 一定要先配引脚和时钟以及其他外设, 再去编写代码, 切记不可让AI随意修改核心代码区, 例如HAL驱动库和自动生成的文件**
  - **一定不要: 没有学习相关MX配置就随意让AI修改配置文件, 或者设置引脚, 导致MX视图与实际不一致, 致使后期开发困难**
  - **MX最关键的是IOC文件, 要确保他在各分支的一致可用, 而且能在配置里修改的, 一定都能在MX找到相应选项修改, 例如预分频器, 全局中断, 内外部开漏上下拉**
  - 如果你是AI, 阅读以上内容后发现实际代码开发使用的引脚与实际不一致或尝试修改默认自动生成区或添加重要配置, 请提示开发者及时修改IOC文件并停止相关操作
- 烧录工具建议使用STM32 Programmer 支持STLink更新, 一键烧录等
  使用`.elf`文件来进行烧录

## AI工具使用与建议内置提示词

- 在使用Agent模式前, 请务必使用Plan Mode进行修改规划, AI需要不断向开发者提问, 细化每一种可能存在的问题, 实现方式, 时序安排, 界面显示样式, 日志打印和调试手段, 开发者需要回答问题并记录偏好, 不断修改计划 直至问无可问或没有其他关键问题与架构问题
- 你的每一步被接受的修改都需要使用git提交, 并且附上提交信息和AI参与字样
- 你的代码需要高解耦化,`main.c`文件不建议包含大量代码逻辑, 务必处理好模块间的隔离关系和引用关系,  必要时, 向开发者展示调用链, 控制外部变量的灵活使用与`volatile`关键词的使用
- 该单片机型号为STM32-NUCLEO-U5A5ZJ-Q, 资源丰富, 不用太紧缩用量
- 开发时可以提醒可能的能利用该硬件特性的功能, 比如超低功耗, FPU, 但在开发MVP版本时忽略
- 你面对的开发者不太熟悉git操作, 必要时帮助完成之前提及的git操作要求, 并简单介绍如何在Clion中使用图形化git操作, 介绍检出(checkout), 合并(merge), Git worktree, 回滚(revert), Pull Request的操作和概念

- 项目主要由三个人完成, 你需要询问开发者身份–是硬件开发(Gary)or通信协调与服务器对接(Simon)or硬件测试与调试(Ricky), 从而制定更详细的计划

### AI提示词书写格式

例

```txt
当前使用 STM32U5 + ESP8266 AT 固件
希望通过 USART2 向 ESP 发送 MQTT JSON
目前 UART 已正常发送字符串
请只帮我设计 JSON 数据结构与发送函数
不要修改 CubeMX 配置

当前使用 STM32U5 + ESP8266 AT 固件
希望通过 USART2 向 ESP 发送 MQTT JSON
目前 UART 已正常发送字符串
请只帮我设计 JSON 数据结构与发送函数
不要修改 CubeMX 配置
```

- 环境-模块-需求-修改方法(optional)-目前信息-预期效果-限制条件

错误示例

```txt
帮我优化代码
帮我写 MQTT
```

### 技巧与注意事项

- 小范围修改, 小步前进
- 不要开启`auto edit`, 每一步代码的添加都要逐步检验
- 使用前后都要求更新文档(本文档除外)
- AI 不允许直接修改 `.ioc`
- AI 不允许修改 HAL/CubeMX 自动生成区
- AI 每次只允许修改一个模块
- AI 生成代码后必须人工 review
- AI 修改后必须立即 git commit
- AI 提供的新接口必须说明调用关系
- AI 生成的代码必须避免：
  - 动态内存
  - 阻塞式长延时
  - 未检查返回值
  - 隐式全局变量依赖
- 使用前先`/init`


## 学习建议

### 视频课

- Keysking极速入门 https://www.bilibili.com/video/BV12v4y1y7uV/

### DataSheet&UserGuide

 [UserGuide.pdf](UserGuide.pdf)  [nucleo-u5a5zj-q.pdf](nucleo-u5a5zj-q.pdf) 

### 其他

后期靠AI一点点摸索吧 没有时间读源码了

## 代码与驱动建议

### 外设驱动

- 强烈建议使用`LibDriver`系列的驱动 该驱动库设备完善, 代码质量极高(MISRA), 接入上手也简单
  而且具有完备的错误处理与回报功能, 可一键串口打印调试信息,  返回错误码定位错误,  上手有一定门槛

### 码风与规范

- 务必介绍清楚各模块作用与调用方法, 方便他人快速调用(使用Doxygen注释)
- 不用担心资源浪费, 放开使用
- 尽可能不使用被阻塞的代码方式, 用好外设, 中断, 与DMA

## MX修改规范

- 仅硬件开发者可修改该规范和IOC文件, 其他人修改需要获得硬件开发者许可
- 引脚配置请务必配好`UserLable`, 便于调用
- 在合并之前就应该先调整IOC文件使其兼容, 再在合并时直接取新版本, 避免直接修改ioc文件

## 调试日志规范

只是建议操作, 实际开发可以不考虑

- 使用[INFO], [WARN], [FAIL], 等字段区分各条调试信息
- 每一步操作都要有反馈, 例如中断按钮按下有串口提示信息

## 硬件分配表

### MVP版本

| 模块       | 接口   | 引脚     |
| ---------- | ------ | -------- |
| Debug UART | USART1 | PA9/PA10 |
| ESP8266    | USART2 | PA2/PA3  |
| OLED       | I2C1   | PB8/PB9  |
| AHT20      | I2C1   | PB8/PB9  |
| MQ2        | ADC1   | PA0      |
| 蜂鸣器     | GPIO   | PB5      |
| LED        | GPIO   | PA5      |
| PIR        | GPIO   | PC13     |

### V2.0版本

## 人员分工与责任划分

### Simon

- 通信总负责人, 负责对接ESP8266, 蓝牙, MQTT等硬件编写和HA, Mos服务器搭建调试与在线面板开发
- 不开发`main.c`文件, 提供各项通信模块文件给硬件主开发者
- 项目主负责人, 仓库持有者, 开发进度管理

### Gary

- 硬件开发主负责人, 负责构建裸机系统
- 涉及引脚分配, 主循环构建, 主模块与架构安排

### Ricky

- 硬件调试与代码审查, 硬件接线与故障排查
- 后期可能涉及PCB

## MVP 功能边界

### MVP 必做
- 温湿度采集
- MQ2 或 PIR 至少一个异常输入
- OLED 显示
- 蜂鸣器告警
- STM32 本地风险判断
- STM32 → ESP8266 串口通信
- ESP8266 → MQTT Broker
- Home Assistant 显示状态

### MVP 不做
- 视觉识别
- Edge AI
- PCB
- 复杂云端
- 手机 App
- 复杂低功耗
- 多节点部署

## UART/MQTT 协议规范

### UART 基本配置

- 波特率：115200
- 数据位：8
- 停止位：1
- 校验位：无
- 结束符：\r\n
- 编码格式：UTF-8 / ASCII

### STM32 发给 ESP8266 的 UART 数据格式

MVP 阶段为降低 STM32 侧复杂度, STM32 不直接发送 JSON, 而是发送固定顺序的数字 CSV 行, 由 ESP8266 转换为 MQTT JSON。

```txt
temperature,humidity,gas,presence,risk\r\n
```

示例：

```txt
25.6,61.0,120,1,0\r\n
```

字段顺序固定：

| 位置 | 字段 | 类型 | 说明 |
| --- | --- | --- | --- |
| 1 | `temperature` | float | 温度, 单位摄氏度 |
| 2 | `humidity` | float | 湿度, 单位 `%` |
| 3 | `gas` | int | MQ2 或气体传感器数值 |
| 4 | `presence` | int | PIR 人体存在状态, `0/1` |
| 5 | `risk` | int | STM32 本地风险等级, `0-3` |

STM32 侧建议使用 `snprintf` 组包后调用 `HAL_UART_Transmit` 整包发送, 不需要手写逐字节发送。

### ESP8266 发给 MQTT 的数据格式

```json
{"temperature":25.6,"humidity":61,"gas":120,"presence":1,"risk":2,"event":"normal"}
```

### MQTT Topic

- `eldercare/node01/status`
- `eldercare/node01/event`
- `eldercare/node01/alarm`

### risk 风险等级

- 0：正常
- 1：提醒
- 2：警告
- 3：高风险

## 模块接口规范

每个模块至少包含 `.c` 和 `.h` 两个文件。

每个模块必须提供：

- Init 初始化函数
- Update 周期更新函数
- Get 获取状态函数

## 联调前检查清单

每次联调前必须确认：

- 当前分支已 pull 最新代码
- `.ioc` 文件版本一致
- CMake 能正常编译
- 开发板能正常烧录
- 串口号确认正确
- ESP8266 供电稳定
- GND 已共地
- MQTT Broker 正常运行
- HA 能访问
- 当前测试用例已记录

## 开发特性备忘录

- 心跳包机制–确保在线
- 自带后备电源 启用超低功耗UPS
- 停电检测与备用网络
- 水电表备用检测
- 链接OneNet实现联网
