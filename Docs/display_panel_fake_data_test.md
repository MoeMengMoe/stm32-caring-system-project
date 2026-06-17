# 展示面板假数据联调说明

## 目标

在没有 STM32/ESP8266 硬件在线的情况下，用服务器侧假数据验证展示面板、事件入库、告警状态和页面刷新链路。

展示地址：

- 家属控制台：`http://192.168.233.128:18080/`
- 展示面板：`http://192.168.233.128:18080/display`

## 启动服务

在服务器工作目录执行：

```bash
cd /home/moemeng/eldercare/stm32-caring-system-project/server
docker compose up -d --build dashboard
curl -sS -o /dev/null -w 'display=%{http_code}\n' http://127.0.0.1:18080/display
curl -sS http://127.0.0.1:18080/api/health
```

预期结果：

- `/display` 返回 `display=200`
- `/api/health` 返回 `{"ok":true}`

## 发布假 status 数据

```bash
cd /home/moemeng/eldercare/stm32-caring-system-project/server
DELAY_SECONDS=1 bash scripts/publish_fake_status.sh
```

脚本会向 `eldercare/node01/status` 发布 4 条样本：

| seq | temperature | humidity | gas | presence | risk | event |
| --- | --- | --- | --- | --- | --- | --- |
| 9001 | 25.6 | 61.0 | 120 | 1 | 0 | normal |
| 9002 | 29.0 | 68.0 | 430 | 1 | 1 | notice |
| 9003 | 31.5 | 72.0 | 720 | 0 | 2 | warning |
| 9004 | 36.2 | 88.0 | 980 | 1 | 3 | alarm |

展示面板变化：

- 风险圆环从稳定逐步升高，最终进入告警展示。
- 温度、湿度、燃气、人体存在、事件标记和序号同步刷新。
- 当 `presence=0` 时，户型示意中的人员标记变灰；当 `risk>=3` 时变为红色告警。

## 发布假 event 数据

```bash
cd /home/moemeng/eldercare/stm32-caring-system-project/server
DELAY_SECONDS=1 bash scripts/publish_fake_event.sh
```

脚本会向 `eldercare/node01/event` 发布 3 条闭环事件：

| event_type | state_before | state_after | risk | result |
| --- | --- | --- | --- | --- |
| REMOTE_TRIGGER | NORMAL | ACK_WAIT | 2 | WAITING_ACK |
| ACK_TIMEOUT | ACK_WAIT | ALARM | 3 | ESCALATED |
| CLEAR_ALARM | ALARM | CLEARED | 0 | CLEARED |

展示面板变化：

- `Closed Loop` 从触发、通知、升级推进到闭环。
- `Recent Events` 增加对应三条事件，最新事件排在最上方。
- 当前告警状态先进入 active，再在 `CLEAR_ALARM` 后显示 clear。
- 主状态标题从需要关注或告警处理中，最终回到已闭环。

## 服务器侧验收命令

```bash
cd /home/moemeng/eldercare/stm32-caring-system-project/server
curl -sS http://127.0.0.1:18080/api/status/latest
curl -sS 'http://127.0.0.1:18080/api/events/recent?limit=3'
curl -sS http://127.0.0.1:18080/api/alarm/current
```

关键判断：

- `/api/status/latest` 能看到 `seq=9004`、`risk=3`、`event=alarm`。
- `/api/events/recent?limit=3` 能看到 `REMOTE_TRIGGER`、`ACK_TIMEOUT`、`CLEAR_ALARM`。
- `/api/alarm/current` 在最后一条 `CLEAR_ALARM` 后应返回 `active:false`。

## 备注

- 当前展示面板只读数据库和告警 API，不发布控制命令，不改变冻结协议。
- 如果没有硬件在线，控制台按钮只能验证 MQTT 命令是否发出，不能产生真实 `relay/state` 或硬件回传。
- 高风险 status 可能触发 LLM 增强分析；analysis 服务用后台工作线程处理 MQTT 消息，模型变慢时不应阻塞后续 status/event 入库。
