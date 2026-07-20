# 比赛展示面板部署及确认指南

> 项目：基于 STM32 的独居老人居家安全边缘看护系统
> 团队：AAA 单片机批发
> 环境：Linux 虚拟机 + Docker Compose
> 大屏：1920 × 1080
> 预计入口：`http://192.168.233.128:18080/display`

本文档可直接交给虚拟机内部的 AI 执行。范围仅包括展示面板的部署与验收，不包括上位机开发，也不包括后续的 HTML 动效 PPT。

## 1. 目标和边界

需要完成：

1. 在不破坏 MQTT、Home Assistant、数据库和通知配置的前提下部署展示面板。
2. 验证三个只读视图：
   - 安全态势：`/display?variant=overview`
   - AI 决策：`/display?variant=ai`
   - 事件复盘：`/display?variant=replay`
3. 验证状态、历史、事件、告警、分析、通知和继电器接口。
4. 配合现场人员验收语音求助、燃气联动和局域网断电三项场景。
5. 明确标注神经网络信息来自真实推理还是映射展示。
6. 验证右侧“云端看护管家”可按需查询真实家庭数据，且模型不可用时仍有规则回答。

执行 AI 必须遵守：

- 不得执行 `git reset --hard`、`git clean` 或覆盖未提交改动。
- 不得执行 `docker compose down -v`，不得删除 Docker volume 或 SQLite 数据库。
- 不得打印、上传或擅自修改 `.env` 中的 Token、密码和密钥。
- 不得擅自改动 HA 自动化、PushPlus 配置或米家设备状态。
- 页面不得出现学校名称、校徽或其他学校信息。
- 未获现场人员确认前，不得发布高燃气值或报警测试消息，因为它们可能真正发送通知并启动插座、排风扇。
- 若关键文件缺失、Compose 配置无效或数据库异常，应停止并报告，不得临时编造替代实现。

## 2. 环境预检和项目定位

记录环境：

```bash
date
hostname
hostname -I
docker --version
docker compose version
```

项目通常位于：

```bash
cd /home/moemeng/eldercare/stm32-caring-system-project/server
```

若路径不存在，只查找，不移动文件：

```bash
find /home /opt -path '*/server/docker-compose.yml' -print 2>/dev/null
```

进入包含 `docker-compose.yml` 的 `server` 目录，执行：

```bash
pwd
git status --short
docker compose config --quiet
```

如需启用云端看护管家的模型增强，在 `server/.env` 中复用现有兼容接口配置：

```dotenv
LLM_ENABLED=auto
OPENAI_API_KEY=填写实际密钥
LLM_BASE_URL=https://api.openai.com/v1/chat/completions
LLM_MODEL=gpt-4o-mini
LLM_TIMEOUT_SECONDS=8
```

不配置密钥也可以部署；此时页面明确显示“本地规则兜底”，所有查询功能仍可使用。

确认新版展示文件存在：

```bash
test -f dashboard/src/main.py
test -f dashboard/src/display.html
test -f dashboard/src/display_page.py
test -f dashboard/src/caretaker.py
test -f scripts/publish_fake_status.sh
test -f scripts/publish_fake_event.sh
grep -n '"/display"' dashboard/src/main.py
grep -n '/api/status/history' dashboard/src/main.py
grep -n '/api/events/recent' dashboard/src/main.py
grep -n '/api/analysis/latest' dashboard/src/main.py
grep -n '/api/notifications/recent' dashboard/src/main.py
grep -n '/api/relays/latest' dashboard/src/main.py
grep -n '/api/caretaker/chat' dashboard/src/main.py
```

如果文件或路由缺失，说明代码尚未同步到虚拟机。停止执行，报告“虚拟机缺少最新展示面板代码”，不要自行重写页面。

## 3. 部署前备份

创建带时间戳的目录：

```bash
backup_stamp=$(date +%Y%m%d-%H%M%S)
backup_dir="$HOME/eldercare-backups/display-$backup_stamp"
mkdir -p "$backup_dir"
cp -a docker-compose.yml "$backup_dir/"
test ! -f .env || cp -a .env "$backup_dir/"
cp -a dashboard "$backup_dir/"
test ! -f homeassistant/config/configuration.yaml || cp -a homeassistant/config/configuration.yaml "$backup_dir/"
```

为保证 SQLite 文件一致，短暂停止写入服务后复制数据目录：

```bash
docker compose stop dashboard analysis_service
test ! -d analysis_service/data || cp -a analysis_service/data "$backup_dir/"
```

完成后立即重新构建和启动，不要长时间保持服务停止。记录 `backup_dir` 的实际值，回滚时需要使用。

## 4. 构建和启动

只重建分析服务与展示服务，尽量不打断 Mosquitto 和 Home Assistant：

```bash
docker compose build analysis_service dashboard
docker compose up -d analysis_service dashboard
docker compose ps
```

查看日志：

```bash
docker logs --tail 100 eldercare-analysis
docker logs --tail 100 eldercare-dashboard
```

预期结果：

- `eldercare-mosquitto` 正常运行，MQTT 端口为 1883。
- `eldercare-analysis` 正常运行。
- `eldercare-dashboard` 正常运行，对外端口为 18080。
- `eldercare-homeassistant` 保持原有状态。
- Dashboard 日志显示监听 `0.0.0.0:8080`。
- 日志中没有持续 traceback、数据库锁死或反复重启。

若构建失败，保存完整错误信息，并先尝试用现有镜像恢复服务：

```bash
docker compose up -d analysis_service dashboard
```

不要通过删除镜像、数据卷或数据库来重试。若旧服务也无法恢复，立即报告。

## 5. 接口和页面冒烟测试

在虚拟机内执行：

```bash
curl -fsS http://127.0.0.1:18080/api/health
curl -fsS http://127.0.0.1:18080/api/status/latest
curl -fsS 'http://127.0.0.1:18080/api/status/history?limit=10'
curl -fsS 'http://127.0.0.1:18080/api/events/recent?limit=20'
curl -fsS http://127.0.0.1:18080/api/alarm/current
curl -fsS http://127.0.0.1:18080/api/analysis/latest
curl -fsS 'http://127.0.0.1:18080/api/notifications/recent?limit=10'
curl -fsS http://127.0.0.1:18080/api/relays/latest
curl -fsS http://127.0.0.1:18080/api/caretaker/suggestions
curl -fsS 'http://127.0.0.1:18080/api/caretaker/session?session_id=display-main&limit=10'
curl -fsS -X POST http://127.0.0.1:18080/api/caretaker/chat \
  -H 'Content-Type: application/json' \
  -d '{"session_id":"display-main","message":"家里现在安全吗？"}'
```

`/api/health` 应成功。其他接口没有历史数据时允许返回空对象或空数组，但不得返回 HTTP 500。

下载页面做静态检查：

```bash
curl -fsS http://127.0.0.1:18080/display -o /tmp/eldercare-display.html
grep -q '基于STM32的独居老人居家安全边缘看护系统' /tmp/eldercare-display.html
grep -q 'AAA单片机批发' /tmp/eldercare-display.html
grep -q '安全态势' /tmp/eldercare-display.html
grep -q 'AI决策' /tmp/eldercare-display.html
grep -q '事件复盘' /tmp/eldercare-display.html
```

确认没有在线 CDN、在线字体或学校信息：

```bash
if grep -nE 'src="https?://|href="https?://' /tmp/eldercare-display.html; then
  echo 'FAIL: 页面包含外部资源'
else
  echo 'PASS: 页面仅使用本地资源'
fi

if grep -nE '学校|大学|学院|校徽' /tmp/eldercare-display.html; then
  echo 'FAIL: 页面可能包含学校信息'
else
  echo 'PASS: 未检出学校信息'
fi
```

## 6. 1920 × 1080 大屏视觉验收

在比赛使用的电脑和显示器上，将分辨率设为 1920 × 1080，浏览器缩放设为 100%，依次打开：

```text
http://192.168.233.128:18080/display?variant=overview
http://192.168.233.128:18080/display?variant=ai
http://192.168.233.128:18080/display?variant=replay
```

快捷键：

- `1`、`2`、`3`：切换三个视图。
- 左右方向键：前后切换视图。
- `F`：进入或退出全屏。
- `A`：打开或关闭云端看护管家。
- 看护管家内按 `Enter` 发送，`Esc` 关闭。

逐项确认：

- 页面无横向或纵向滚动条。
- 标题、指标、图表和底部状态栏无截断、重叠或溢出。
- 远距离能看清燃气值、风险等级、告警状态和关键结论。
- 风险颜色变化清晰，整体为医疗科技风格。
- 页面只有展示功能，没有控制继电器、插座或告警的业务按钮。
- 看护管家回答包含风险、置信度、真实证据和已调用的数据工具；建议项不得声称已经控制设备。
- 临时移除模型密钥或令模型接口超时后，提问仍能得到“本地规则兜底”回答。
- 项目名、团队名准确，不含任何学校信息。
- 三个视图切换无白屏和明显卡顿。
- 安全态势能看到燃气、风险、告警链路、设备连接及趋势。
- AI 决策以识别结果和处理过程为重点，网络结构为辅助。
- 事件复盘能按时间顺序理解事件升级、通知和联动结果。

为三个视图各保存一张 1920 × 1080 截图，并在最终报告中给出文件路径。

## 7. 数据真实性和神经网络判定

检查最新载荷：

```bash
curl -fsS http://127.0.0.1:18080/api/status/latest | python3 -m json.tool
curl -fsS http://127.0.0.1:18080/api/analysis/latest | python3 -m json.tool
```

展示口径：

- 数据中存在且持续更新 `edge_ai_*` 一类板端识别字段时，页面可显示 `LIVE · 板端实时`。
- 当前协议没有完整板端神经网络字段时，页面应显示 `EXPLAIN · 映射展示`，用现有传感器和规则链解释识别过程。
- 不得把映射结果描述成真实神经网络推理。

只有同时满足以下条件，才认定神经网络数据是稳定的现场真实来源：

1. 连续运行至少 10 分钟，无字段消失、明显乱跳或长时间停更。
2. 输入变化时，识别类别、置信度和处理阶段能够合理响应。
3. 断网后仍能在板端或本地链路完成推理或自治。
4. 页面时间戳与实际操作基本一致。

任一条件不满足，就保持 `EXPLAIN · 映射展示`。

## 8. 受控模拟数据验证

优先观察已有真实数据。若页面已稳定接收现场节点数据，不要随意覆盖当前状态。

以下脚本可能触发真实 PushPlus、HA 自动化、米家插座和排风扇。只有现场人员明确确认以下事项后才能执行：

- 插座与排风扇周边安全；
- 允许产生一次真实通知；
- 允许测试数据写入事件历史；
- 当前没有正在处理的真实告警。

获准后执行：

```bash
DELAY_SECONDS=2 bash scripts/publish_fake_status.sh
bash scripts/publish_fake_event.sh
```

检查结果：

```bash
curl -fsS 'http://127.0.0.1:18080/api/status/history?limit=10' | python3 -m json.tool
curl -fsS 'http://127.0.0.1:18080/api/events/recent?limit=20' | python3 -m json.tool
curl -fsS http://127.0.0.1:18080/api/alarm/current | python3 -m json.tool
curl -fsS 'http://127.0.0.1:18080/api/notifications/recent?limit=10' | python3 -m json.tool
curl -fsS http://127.0.0.1:18080/api/relays/latest | python3 -m json.tool
```

预期能看到燃气值由正常逐级升高、风险由低到高，以及求助事件从等待确认、升级报警到解除。若外部设备未联动，应记录“页面链路通过，外部设备待确认”，不得伪造成功。

## 9. 比赛场景验收

### 9.1 语音主动求助、确认和升级

操作：

1. 保持局域网、MQTT、语音模块及相关服务正常。
2. 打开“事件复盘”或“安全态势”。
3. 说：`小冰小冰`。
4. 说：`我摔倒了`。
5. 观察本地语音确认、页面事件和确认倒计时。
6. 暂不回应，观察告警是否按设计逐级升级。
7. 确认通知或远端告警记录出现。
8. 再说：`小冰小冰`。
9. 说：`取消报警`。
10. 确认本地和页面均回到已解除或安全状态。

通过条件：语音识别稳定；时间线顺序正确；告警不会无故提前解除；升级和取消均有可见结果；页面、声音和通知状态无明显矛盾。

### 9.2 燃气异常、HA 警告、排风联动和恢复

操作前确认米家插座和排风扇可安全启动，HA 能连接设备。然后：

1. 用真实传感器或获准的模拟数据让燃气值逐渐升高。
2. 观察燃气曲线和风险等级变化。
3. 确认异常达到条件后 HA 发出警告。
4. 确认米家智能插座启动，排风扇实际运转。
5. 使燃气值逐渐回落。
6. 确认数值回到安全区，风险恢复平衡。

当前展示口径为：参考阈值 150 ppm；高于阈值约 5 秒进入风险处理；低于阈值约 10 秒判定恢复。若固件或 HA 的实际配置不同，应以实际配置为准，并在比赛前同步讲解口径。

通过条件：数值、风险、通知、插座和排风扇形成清楚的“检测—告警—联动—恢复”链；恢复后不再持续报警；事件历史保留本次过程。

### 9.3 整个局域网断电后的本地自治

此场景必须由现场人员手动切断和恢复局域网设备供电。执行 AI 不得自行关机、断网或修改网络配置。

断网前：

1. 预先打开展示页并进入全屏。
2. 页面保持打开，不要刷新。
3. 确认本地硬件、上位机和页面均为正常状态。

断网后：

1. 已加载的前端应继续显示，但接口请求会失败。
2. 观察页面从波动、确认中逐渐进入离线状态。
3. 不要刷新；刷新后浏览器无法重新从 VM 取得页面文件。
4. 用实际硬件和上位机演示本地检测、语音、继电器等自治能力。
5. 讲解时明确：云端、HA 和 VM 页面依赖局域网，但安全处置的核心能力保留在本地设备。

网络恢复后：

1. 等待页面自动重连，不要立即刷新。
2. 确认接口恢复、状态重新更新。
3. 按系统实际能力检查离线数据是否补传；没有补传能力时如实说明。
4. 只有自动恢复失败时才刷新页面，并记录该问题。

通过条件：页面不把旧数据伪装成实时数据；硬件能独立完成预定自治动作；网络恢复后页面重新取得数据；讲解清楚区分本地自治与依赖局域网的能力。

## 10. 30 分钟稳定性检查

正式比赛前连续运行至少 30 分钟：

```bash
docker compose ps
docker inspect -f '{{.Name}} restart={{.RestartCount}} status={{.State.Status}}' \
  eldercare-mosquitto eldercare-analysis eldercare-dashboard eldercare-homeassistant
docker logs --since 30m eldercare-analysis
docker logs --since 30m eldercare-dashboard
```

同时确认：

- 页面时间持续更新，数据时间戳合理。
- 三个视图反复切换至少 10 次无异常。
- 容器没有反复重启。
- MQTT 数据持续更新，没有大范围乱序。
- 通知、HA 和米家联动只在预定条件下触发。
- 浏览器无持续异常的 CPU 或内存增长。

备用入口：

```text
安全态势：http://192.168.233.128:18080/display?variant=overview
AI 决策：http://192.168.233.128:18080/display?variant=ai
事件复盘：http://192.168.233.128:18080/display?variant=replay
原控制页：http://192.168.233.128:18080/
```

## 11. 故障处理和回滚

页面无法访问：

```bash
docker compose ps
docker logs --tail 200 eldercare-dashboard
curl -v http://127.0.0.1:18080/api/health
ss -lntp | grep 18080
```

页面能打开但无数据：

```bash
docker logs --tail 200 eldercare-analysis
docker logs --tail 200 eldercare-mosquitto
curl -fsS http://127.0.0.1:18080/api/status/latest | python3 -m json.tool
```

先判断容器、端口、MQTT、数据库或局域网问题，不要直接清理 Docker。

需要回滚时，先向用户报告原因，再使用第 3 节记录的备份目录：

```bash
docker compose stop dashboard analysis_service
cp -a "$backup_dir/dashboard/." ./dashboard/
test ! -d "$backup_dir/analysis_service/data" || cp -a "$backup_dir/analysis_service/data/." ./analysis_service/data/
docker compose build analysis_service dashboard
docker compose up -d analysis_service dashboard
```

只有确认本次部署破坏了 `.env` 或 Compose 文件时才恢复对应备份，不要覆盖部署后现场人员做出的有效配置。回滚后重新执行接口冒烟测试，保留失败日志和新版文件。

## 12. 执行 AI 最终报告模板

执行结束后必须按下列格式报告，不得只说“部署成功”：

```text
【部署信息】
- 执行时间：
- 虚拟机 IP：
- 项目实际路径：
- Git 分支/提交：
- 备份目录：

【服务状态】
- Mosquitto：通过/失败，说明：
- Analysis Service：通过/失败，说明：
- Dashboard：通过/失败，说明：
- Home Assistant：通过/失败/未改动，说明：

【接口检查】
- /api/health：
- /api/status/latest：
- /api/status/history：
- /api/events/recent：
- /api/alarm/current：
- /api/analysis/latest：
- /api/notifications/recent：
- /api/relays/latest：

【大屏视觉检查】
- 安全态势：通过/失败
- AI 决策：通过/失败
- 事件复盘：通过/失败
- 1920×1080 无溢出：通过/失败
- 无学校信息：通过/失败
- 截图路径：

【数据口径】
- 状态数据：真实/模拟/混合
- 神经网络展示：LIVE 板端实时 / EXPLAIN 映射展示
- 判定依据：

【场景验收】
- 语音求助与升级：通过/失败/未执行，证据：
- 燃气告警与排风联动：通过/失败/未执行，证据：
- 局域网断电与本地自治：通过/失败/未执行，证据：

【遗留问题与比赛风险】
- 问题：
- 影响：
- 临时方案：
```

只有接口、大屏视觉和计划执行的现场场景均有证据时，才可以宣布“已通过验收”。因缺少硬件或人工授权而未执行的项目，应明确标记“未执行”，不能算作通过。
