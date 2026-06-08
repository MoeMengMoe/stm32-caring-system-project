# Backend Deployment Guide

This guide deploys the backend stack from the git source tree to a Linux server.

The deployed stack contains:

- Mosquitto MQTT broker
- `analysis_service` backend analysis worker
- Home Assistant
- SQLite runtime database mounted on the host

## 1. Server Requirements

Recommended OS:

- Ubuntu 22.04 / 24.04 LTS, Debian 12, or another Linux host with Docker support

Required tools:

```bash
git --version
docker --version
docker compose version
```

If Docker is not installed on Ubuntu:

```bash
sudo apt update
sudo apt install -y ca-certificates curl git
curl -fsSL https://get.docker.com | sudo sh
sudo usermod -aG docker "$USER"
```

Log out and log in again after adding the user to the `docker` group.

## 2. Clone Source Code

Choose a stable deployment directory. Example:

```bash
sudo mkdir -p /opt/eldercare
sudo chown "$USER":"$USER" /opt/eldercare
cd /opt/eldercare
git clone <your-git-repo-url> stm32-caring-system-project
cd stm32-caring-system-project
```

If the repository already exists:

```bash
cd /opt/eldercare/stm32-caring-system-project
git pull
```

Backend working directory:

```bash
cd /opt/eldercare/stm32-caring-system-project/server
```

All Docker Compose commands in this guide are run from this `server/` directory.

## 3. Runtime Directory Layout

Important paths under `server/`:

```text
server/
  docker-compose.yml
  .env                       # local secrets, not committed
  .env.example               # template, safe to commit
  mosquitto/
    config/mosquitto.conf
    data/                    # MQTT runtime data, ignored by git
    log/                     # MQTT logs, ignored by git
  homeassistant/
    config/configuration.yaml
  analysis_service/
    Dockerfile
    requirements.txt
    src/
    data/eldercare.db         # SQLite database, ignored by git
  scripts/
    publish_fake_status.sh
    publish_llm_trigger_status.sh
```

SQLite database location:

```text
server/analysis_service/data/eldercare.db
```

Inside the container, this same database is mounted at:

```text
/app/data/eldercare.db
```

Do not commit runtime data:

- `server/.env`
- `server/analysis_service/data/`
- `server/mosquitto/data/`
- `server/mosquitto/log/`

These are already ignored by `.gitignore`.

## 4. Configure Environment Variables

Create the local `.env` file:

```bash
cd /opt/eldercare/stm32-caring-system-project/server
cp .env.example .env
nano .env
```

Example `server/.env`:

```env
OPENAI_API_KEY=
LLM_BASE_URL=https://api.openai.com/v1/chat/completions
LLM_MODEL=gpt-4o-mini
```

Rules:

- Keep `OPENAI_API_KEY` empty if you only want local rule analysis.
- Set `OPENAI_API_KEY` to enable LLM analysis for abnormal data.
- Put custom API gateway addresses in `LLM_BASE_URL`.
- Never commit `server/.env`.

The Compose file injects these into `analysis_service`:

```yaml
OPENAI_API_KEY=${OPENAI_API_KEY:-}
LLM_BASE_URL=${LLM_BASE_URL:-https://api.openai.com/v1/chat/completions}
LLM_MODEL=${LLM_MODEL:-gpt-4o-mini}
```

## 5. Docker Compose Services

The Compose file starts three services:

```text
eldercare-mosquitto       MQTT broker, port 1883
eldercare-analysis        Python analysis backend
eldercare-homeassistant   Home Assistant, port 8123
```

External ports:

```text
1883  MQTT broker
8123  Home Assistant web UI
```

MQTT topics used by the backend:

```text
eldercare/node01/status      input from ESP8266
eldercare/node01/analysis    backend analysis output
eldercare/node01/alarm       high-risk backend alarm output
```

Relay topics used by ESP8266 and Home Assistant:

```text
eldercare/node01/relay/1/set
eldercare/node01/relay/1/state
eldercare/node01/relay/1/result
...
eldercare/node01/relay/4/set
eldercare/node01/relay/4/state
eldercare/node01/relay/4/result
```

## 6. Start Deployment

From the server work directory:

```bash
cd /opt/eldercare/stm32-caring-system-project/server
docker compose up -d --build
```

Check containers:

```bash
docker ps --format "table {{.Names}}\t{{.Status}}\t{{.Ports}}"
```

Expected:

```text
eldercare-mosquitto
eldercare-analysis
eldercare-homeassistant
```

Check logs:

```bash
docker logs --tail=100 eldercare-mosquitto
docker logs --tail=100 eldercare-analysis
docker logs --tail=100 eldercare-homeassistant
```

Follow backend logs:

```bash
docker logs -f eldercare-analysis
```

## 7. Fake Data Verification

Use Linux scripts on the server:

```bash
cd /opt/eldercare/stm32-caring-system-project/server
bash scripts/publish_fake_status.sh
```

The script works like this:

```text
host bash script
  -> docker exec eldercare-mosquitto
  -> mosquitto_pub inside the Mosquitto container
  -> MQTT topic eldercare/node01/status
```

The Linux host does not need `mosquitto_pub` installed.

Subscribe to analysis output:

```bash
docker exec eldercare-mosquitto mosquitto_sub -t eldercare/node01/analysis -C 1 -v
```

Expected analysis JSON:

```json
{
  "node_id": "node01",
  "source_seq": 9004,
  "source_risk": 3,
  "cloud_risk": 3,
  "risk_score": 100,
  "summary": "...",
  "model_used": false,
  "need_family_notice": true,
  "need_community_notice": true,
  "need_hospital_notice": true
}
```

Check backend logs:

```bash
docker logs --tail=50 eldercare-analysis
```

Expected log contains:

```text
stored status row=... analysis row=... node=node01 seq=... cloud_risk=...
```

Check high-risk alarm topic:

```bash
docker exec eldercare-mosquitto mosquitto_sub -t eldercare/node01/alarm -C 1 -v
```

Run fake data again in another terminal if the subscribe command is waiting:

```bash
bash scripts/publish_fake_status.sh
```

## 8. SQLite Verification

The database is created after the first valid MQTT status message:

```bash
ls -lh analysis_service/data/
```

Expected:

```text
eldercare.db
```

Query tables from a temporary SQLite container:

```bash
docker run --rm -it \
  -v "$PWD/analysis_service/data:/data" \
  nouchka/sqlite3 \
  sqlite3 /data/eldercare.db ".tables"
```

Expected tables:

```text
analysis_results
notification_logs
raw_status
relay_states
```

Query row counts:

```bash
docker run --rm -it \
  -v "$PWD/analysis_service/data:/data" \
  nouchka/sqlite3 \
  sqlite3 /data/eldercare.db \
  "select 'raw_status', count(*) from raw_status union all select 'analysis_results', count(*) from analysis_results union all select 'notification_logs', count(*) from notification_logs;"
```

If the server already has `sqlite3` installed, this is simpler:

```bash
sqlite3 analysis_service/data/eldercare.db ".tables"
sqlite3 analysis_service/data/eldercare.db "select count(*) from raw_status;"
```

## 9. LLM Verification

Set `OPENAI_API_KEY` in `server/.env` first:

```bash
nano .env
```

Restart only the analysis service:

```bash
docker compose up -d --build analysis_service
```

Trigger abnormal data:

```bash
bash scripts/publish_llm_trigger_status.sh
```

Subscribe to analysis:

```bash
docker exec eldercare-mosquitto mosquitto_sub -t eldercare/node01/analysis -C 1 -v
```

Expected:

```text
model_used=true
```

If API call fails:

- `eldercare-analysis` logs an LLM warning.
- Local rule analysis still publishes `eldercare/node01/analysis`.
- `model_used` remains `false`.

The model output is not allowed to lower the local rule risk. The backend keeps the maximum of local rule risk and model risk.

## 10. Home Assistant Verification

Open:

```text
http://<server-ip>:8123
```

MQTT broker address for HA:

```text
mosquitto
```

Port:

```text
1883
```

Expected configured entities include:

```text
Node01 Temperature
Node01 Humidity
Node01 Gas
Node01 Presence
Node01 Risk
Node01 Event
Node01 Cloud Risk
Node01 Risk Score
Node01 Analysis Summary
Node01 Need Family Notice
Node01 Need Community Notice
Node01 Need Hospital Notice
Node01 Relay 1..4
```

After fake data is published, `Node01 Cloud Risk`, `Node01 Risk Score`, and `Node01 Analysis Summary` should update.

## 11. Real Device Verification

After fake data passes, connect the actual device chain:

```text
STM32
  -> USART2 status frame
  -> ESP8266 gateway
  -> MQTT broker eldercare/node01/status
  -> analysis_service
  -> MQTT broker eldercare/node01/analysis
  -> Home Assistant
```

Subscribe to real status:

```bash
docker exec eldercare-mosquitto mosquitto_sub -t eldercare/node01/status -v
```

Expected status JSON:

```json
{
  "node_id": "node01",
  "seq": 18,
  "temperature": 25.6,
  "humidity": 61.0,
  "gas": 120,
  "presence": 1,
  "risk": 0,
  "event": "normal",
  "relay_state_mask": 5,
  "cloud_perm_mask": 15
}
```

Subscribe to analysis:

```bash
docker exec eldercare-mosquitto mosquitto_sub -t eldercare/node01/analysis -v
```

Watch backend logs:

```bash
docker logs -f eldercare-analysis
```

Real-device pass criteria:

- `eldercare/node01/status` receives ESP8266 data continuously.
- `seq` increments.
- `eldercare-analysis` logs each valid status.
- SQLite `raw_status` row count increases.
- `eldercare/node01/analysis` publishes after each valid status.
- Home Assistant analysis entities update.

## 12. Relay Verification

Subscribe to relay topics:

```bash
docker exec eldercare-mosquitto mosquitto_sub -t 'eldercare/node01/relay/+/+' -v
```

From Home Assistant, toggle `Node01 Relay 1`.

Expected flow:

```text
HA switch
  -> eldercare/node01/relay/1/set
  -> ESP8266 forwards C request to STM32
  -> STM32 returns R result
  -> ESP8266 publishes relay/1/result and relay/1/state
  -> HA switch state refreshes from relay/1/state
```

Pass criteria:

- `set` topic is published.
- `state` topic returns confirmed state.
- HA state follows `state`, not button click assumption.

## 13. Updating Deployment

Pull latest source:

```bash
cd /opt/eldercare/stm32-caring-system-project
git pull
cd server
docker compose up -d --build
```

Check logs:

```bash
docker logs --tail=100 eldercare-analysis
```

Runtime database is preserved because it is mounted at:

```text
server/analysis_service/data/eldercare.db
```

## 14. Backup and Restore

Stop services before backup:

```bash
cd /opt/eldercare/stm32-caring-system-project/server
docker compose stop
```

Backup runtime data:

```bash
tar czf eldercare-runtime-backup-$(date +%Y%m%d-%H%M%S).tar.gz \
  .env \
  analysis_service/data \
  mosquitto/data \
  mosquitto/log \
  homeassistant/config
```

Restart:

```bash
docker compose up -d
```

Restore by extracting the backup into the same `server/` directory, then:

```bash
docker compose up -d --build
```

## 15. Common Troubleshooting

Check Compose config:

```bash
docker compose config
```

Mosquitto not healthy:

```bash
docker logs eldercare-mosquitto
cat mosquitto/config/mosquitto.conf
```

Backend cannot connect MQTT:

```bash
docker logs eldercare-analysis
docker exec eldercare-mosquitto mosquitto_sub -t '$SYS/broker/version' -C 1
```

No analysis output:

```bash
docker exec eldercare-mosquitto mosquitto_sub -t eldercare/node01/status -C 1 -v
docker logs --tail=100 eldercare-analysis
```

If invalid payloads are received, backend logs show:

```text
drop invalid status payload: ...
```

LLM not used:

```bash
cat .env
docker compose exec analysis_service env | grep -E 'OPENAI|LLM'
docker logs --tail=100 eldercare-analysis
```

Remember:

- Empty `OPENAI_API_KEY` means local rules only.
- LLM is only called for abnormal data.
- Network/firewall must allow outbound access to `LLM_BASE_URL`.

ESP8266 cannot reach MQTT:

- Confirm server IP.
- Confirm port `1883` is reachable from the ESP8266 Wi-Fi network.
- Confirm ESP8266 firmware `MQTT_HOST` matches the Linux server IP.

```bash
hostname -I
docker ps
```

If firewall is enabled:

```bash
sudo ufw allow 1883/tcp
sudo ufw allow 8123/tcp
sudo ufw status
```
