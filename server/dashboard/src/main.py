import json
import os
import sqlite3
import time
from typing import Any, Literal

import paho.mqtt.client as mqtt
from fastapi import FastAPI, HTTPException
from fastapi.responses import HTMLResponse
from pydantic import BaseModel, Field


MQTT_HOST = os.getenv("MQTT_HOST", "localhost")
MQTT_PORT = int(os.getenv("MQTT_PORT", "1883"))
NODE_ID = os.getenv("NODE_ID", "node01")
DB_PATH = os.getenv("DB_PATH", "data/eldercare.db")

MQTT_DEMO_COMMAND_TOPIC = os.getenv("MQTT_DEMO_COMMAND_TOPIC", f"eldercare/{NODE_ID}/demo/command")
MQTT_RELAY_TOPIC_TEMPLATE = os.getenv(
    "MQTT_RELAY_TOPIC_TEMPLATE",
    f"eldercare/{NODE_ID}/relay/{{relay_id}}/set",
)


app = FastAPI(title="Eldercare Dashboard API")


class TriggerRequest(BaseModel):
    scenario: Literal["SOS_OR_FALL_SIM", "LONG_STILL_NO_RESPONSE", "OFFLINE_AUTONOMY"]
    value: int = 1


class NetworkRequest(BaseModel):
    online: bool


class RelayRequest(BaseModel):
    action: Literal["ON", "OFF"]


class DemoCommand(BaseModel):
    command_type: Literal["TRIGGER_SCENARIO", "USER_ACK", "CLEAR_ALARM", "SIMULATE_NETWORK"]
    scenario: str = "NONE"
    value: int = 1
    source: str = "dashboard"
    request_id: int = Field(default_factory=lambda: int(time.time() * 1000) % 2147483647)


def _connect_db() -> sqlite3.Connection:
    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row
    return conn


def _publish(topic: str, payload: dict[str, Any] | str) -> bool:
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
    client.connect(MQTT_HOST, MQTT_PORT, keepalive=30)
    if isinstance(payload, str):
        message = payload
    else:
        message = json.dumps(payload, ensure_ascii=False, separators=(",", ":"))
    result = client.publish(topic, message, qos=0, retain=False)
    client.disconnect()
    return result.rc == mqtt.MQTT_ERR_SUCCESS


def _publish_demo_command(command: DemoCommand) -> dict[str, Any]:
    payload = {
        "command_id": f"demo-{command.request_id}",
        "request_id": command.request_id,
        "command_type": command.command_type,
        "scenario": command.scenario,
        "value": command.value,
        "source": command.source,
    }
    if not _publish(MQTT_DEMO_COMMAND_TOPIC, payload):
        raise HTTPException(status_code=502, detail="failed to publish demo command")
    return payload


@app.get("/", response_class=HTMLResponse)
def control_page() -> str:
    return HTML_PAGE


@app.get("/api/status/latest")
def latest_status() -> dict[str, Any]:
    with _connect_db() as conn:
        row = conn.execute(
            "SELECT received_at, payload_json FROM raw_status ORDER BY id DESC LIMIT 1"
        ).fetchone()
    if row is None:
        return {"available": False}
    payload = json.loads(row["payload_json"])
    payload["available"] = True
    payload["received_at"] = row["received_at"]
    return payload


@app.get("/api/events/recent")
def recent_events(limit: int = 20) -> dict[str, Any]:
    bounded_limit = max(1, min(limit, 100))
    with _connect_db() as conn:
        rows = conn.execute(
            """
            SELECT received_at, payload_json, is_backfilled
            FROM event_logs
            ORDER BY id DESC
            LIMIT ?
            """,
            (bounded_limit,),
        ).fetchall()
    events: list[dict[str, Any]] = []
    for row in rows:
        payload = json.loads(row["payload_json"])
        payload["received_at"] = row["received_at"]
        payload["is_backfilled"] = bool(row["is_backfilled"])
        events.append(payload)
    return {"events": events}


@app.get("/api/alarm/current")
def current_alarm() -> dict[str, Any]:
    with _connect_db() as conn:
        row = conn.execute(
            """
            SELECT received_at, payload_json
            FROM event_logs
            WHERE risk >= 3
               OR state_after IN ('ALARM', 'NO_RESPONSE', 'CLEARED')
               OR result IN ('ESCALATED', 'ACKNOWLEDGED', 'CLEARED')
            ORDER BY id DESC
            LIMIT 1
            """
        ).fetchone()
    if row is None:
        return {"active": False}

    event = json.loads(row["payload_json"])
    cleared = event["state_after"] == "CLEARED" or event["result"] in ("ACKNOWLEDGED", "CLEARED")
    return {
        "active": not cleared,
        "event": event,
        "received_at": row["received_at"],
    }


@app.post("/api/demo/trigger")
def trigger_demo(request: TriggerRequest) -> dict[str, Any]:
    return _publish_demo_command(
        DemoCommand(command_type="TRIGGER_SCENARIO", scenario=request.scenario, value=request.value)
    )


@app.post("/api/demo/ack")
def ack_demo() -> dict[str, Any]:
    return _publish_demo_command(DemoCommand(command_type="USER_ACK", scenario="NONE", value=1))


@app.post("/api/demo/clear")
def clear_demo() -> dict[str, Any]:
    return _publish_demo_command(DemoCommand(command_type="CLEAR_ALARM", scenario="NONE", value=1))


@app.post("/api/demo/network")
def network_demo(request: NetworkRequest) -> dict[str, Any]:
    return _publish_demo_command(
        DemoCommand(command_type="SIMULATE_NETWORK", scenario="OFFLINE_AUTONOMY", value=1 if request.online else 0)
    )


@app.post("/api/relay/{relay_id}/set")
def set_relay(relay_id: int, request: RelayRequest) -> dict[str, Any]:
    if relay_id < 1 or relay_id > 4:
        raise HTTPException(status_code=400, detail="relay_id must be 1-4")
    request_id = int(time.time() * 1000) % 2147483647
    payload = {
        "request_id": request_id,
        "action": request.action,
        "source": "dashboard",
    }
    topic = MQTT_RELAY_TOPIC_TEMPLATE.format(relay_id=relay_id)
    if not _publish(topic, payload):
        raise HTTPException(status_code=502, detail="failed to publish relay command")
    return {"topic": topic, **payload}


HTML_PAGE = """
<!doctype html>
<html lang="zh-CN">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Eldercare Control</title>
  <style>
    :root {
      color-scheme: light;
      --bg: #f6f7f9;
      --panel: #ffffff;
      --line: #d9dee7;
      --text: #18202f;
      --muted: #667085;
      --ok: #147a3d;
      --warn: #b65f00;
      --danger: #b42318;
      --cmd: #2459a6;
    }
    * { box-sizing: border-box; }
    body {
      margin: 0;
      background: var(--bg);
      color: var(--text);
      font-family: "Segoe UI", Arial, sans-serif;
      letter-spacing: 0;
    }
    main {
      width: min(1120px, calc(100vw - 32px));
      margin: 24px auto;
      display: grid;
      gap: 16px;
    }
    header {
      display: flex;
      align-items: center;
      justify-content: space-between;
      gap: 16px;
    }
    h1 {
      margin: 0;
      font-size: 24px;
      line-height: 1.2;
    }
    .grid {
      display: grid;
      grid-template-columns: repeat(3, minmax(0, 1fr));
      gap: 16px;
    }
    section {
      background: var(--panel);
      border: 1px solid var(--line);
      border-radius: 8px;
      padding: 14px;
    }
    h2 {
      margin: 0 0 12px;
      font-size: 15px;
      line-height: 1.2;
    }
    .commands {
      display: grid;
      grid-template-columns: repeat(2, minmax(0, 1fr));
      gap: 8px;
    }
    button {
      min-height: 40px;
      border: 1px solid var(--line);
      border-radius: 6px;
      background: #fff;
      color: var(--text);
      font-size: 14px;
      cursor: pointer;
    }
    button.primary { border-color: var(--cmd); color: var(--cmd); }
    button.warn { border-color: var(--warn); color: var(--warn); }
    button.danger { border-color: var(--danger); color: var(--danger); }
    button.ok { border-color: var(--ok); color: var(--ok); }
    dl {
      display: grid;
      grid-template-columns: 96px 1fr;
      gap: 8px;
      margin: 0;
      font-size: 14px;
    }
    dt { color: var(--muted); }
    dd { margin: 0; overflow-wrap: anywhere; }
    .timeline {
      display: grid;
      gap: 8px;
      max-height: 360px;
      overflow: auto;
    }
    .event {
      border-top: 1px solid var(--line);
      padding-top: 8px;
      font-size: 13px;
    }
    .event:first-child {
      border-top: 0;
      padding-top: 0;
    }
    .status {
      font-size: 13px;
      color: var(--muted);
      min-height: 20px;
    }
    @media (max-width: 820px) {
      .grid { grid-template-columns: 1fr; }
      header { align-items: flex-start; flex-direction: column; }
    }
  </style>
</head>
<body>
  <main>
    <header>
      <h1>Eldercare Control</h1>
      <div id="opStatus" class="status"></div>
    </header>
    <div class="grid">
      <section>
        <h2>Demo</h2>
        <div class="commands">
          <button class="primary" onclick="triggerScenario('SOS_OR_FALL_SIM')">场景一</button>
          <button class="primary" onclick="triggerScenario('LONG_STILL_NO_RESPONSE')">场景二</button>
          <button class="warn" onclick="setNetwork(false)">离线</button>
          <button class="ok" onclick="setNetwork(true)">恢复</button>
          <button class="ok" onclick="post('/api/demo/ack', {})">确认</button>
          <button class="danger" onclick="post('/api/demo/clear', {})">清除</button>
        </div>
      </section>
      <section>
        <h2>Relay</h2>
        <div class="commands">
          <button onclick="relay(1, 'ON')">R1 ON</button>
          <button onclick="relay(1, 'OFF')">R1 OFF</button>
          <button onclick="relay(2, 'ON')">R2 ON</button>
          <button onclick="relay(2, 'OFF')">R2 OFF</button>
        </div>
      </section>
      <section>
        <h2>Status</h2>
        <dl id="statusList"></dl>
      </section>
    </div>
    <section>
      <h2>Recent Events</h2>
      <div id="events" class="timeline"></div>
    </section>
  </main>
  <script>
    const opStatus = document.getElementById('opStatus');
    const statusList = document.getElementById('statusList');
    const eventsEl = document.getElementById('events');

    async function post(url, body) {
      const res = await fetch(url, {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify(body)
      });
      const data = await res.json();
      opStatus.textContent = res.ok ? `OK ${new Date().toLocaleTimeString()}` : JSON.stringify(data);
      refresh();
    }
    function triggerScenario(scenario) {
      return post('/api/demo/trigger', {scenario, value: 1});
    }
    function setNetwork(online) {
      return post('/api/demo/network', {online});
    }
    function relay(id, action) {
      return post(`/api/relay/${id}/set`, {action});
    }
    async function refresh() {
      const status = await fetch('/api/status/latest').then(r => r.json()).catch(() => ({available: false}));
      statusList.innerHTML = '';
      const keys = ['available', 'seq', 'temperature', 'humidity', 'gas', 'presence', 'risk', 'event'];
      for (const key of keys) {
        const dt = document.createElement('dt');
        const dd = document.createElement('dd');
        dt.textContent = key;
        dd.textContent = status[key] ?? '-';
        statusList.append(dt, dd);
      }
      const events = await fetch('/api/events/recent?limit=12').then(r => r.json()).catch(() => ({events: []}));
      eventsEl.innerHTML = '';
      for (const event of events.events) {
        const div = document.createElement('div');
        div.className = 'event';
        div.textContent = `${event.scenario} | ${event.event_type} | ${event.state_before} -> ${event.state_after} | ${event.result}`;
        eventsEl.appendChild(div);
      }
    }
    refresh();
    setInterval(refresh, 3000);
  </script>
</body>
</html>
"""
