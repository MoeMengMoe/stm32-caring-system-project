import json
import os
import sqlite3
import time
from contextlib import contextmanager
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Any
from urllib.parse import parse_qs, urlparse

import paho.mqtt.client as mqtt

from .caretaker import build_caretaker_service


MQTT_HOST = os.getenv("MQTT_HOST", "localhost")
MQTT_PORT = int(os.getenv("MQTT_PORT", "1883"))
NODE_ID = os.getenv("NODE_ID", "node01")
DB_PATH = os.getenv("DB_PATH", "data/eldercare.db")
HOST = os.getenv("DASHBOARD_HOST", "0.0.0.0")
PORT = int(os.getenv("DASHBOARD_PORT", "8080"))

MQTT_DEMO_COMMAND_TOPIC = os.getenv("MQTT_DEMO_COMMAND_TOPIC", f"eldercare/{NODE_ID}/demo/command")
MQTT_RELAY_TOPIC_TEMPLATE = os.getenv(
    "MQTT_RELAY_TOPIC_TEMPLATE",
    f"eldercare/{NODE_ID}/relay/{{relay_id}}/set",
)
CARETAKER = build_caretaker_service()


def main() -> None:
    server = ThreadingHTTPServer((HOST, PORT), DashboardHandler)
    print(f"dashboard listening on {HOST}:{PORT}", flush=True)
    server.serve_forever()


class DashboardHandler(BaseHTTPRequestHandler):
    def do_GET(self) -> None:
        parsed = urlparse(self.path)
        if parsed.path == "/":
            self._send_html(HTML_PAGE)
            return
        if parsed.path == "/display":
            self._send_html(DISPLAY_PAGE)
            return
        if parsed.path == "/api/status/latest":
            self._send_json(latest_status())
            return
        if parsed.path == "/api/status/history":
            params = parse_qs(parsed.query)
            limit_text = params.get("limit", ["60"])[0]
            try:
                limit = int(limit_text)
            except ValueError:
                limit = 60
            self._send_json(status_history(limit))
            return
        if parsed.path == "/api/events/recent":
            params = parse_qs(parsed.query)
            limit_text = params.get("limit", ["20"])[0]
            try:
                limit = int(limit_text)
            except ValueError:
                limit = 20
            self._send_json(recent_events(limit))
            return
        if parsed.path == "/api/alarm/current":
            self._send_json(current_alarm())
            return
        if parsed.path == "/api/analysis/latest":
            self._send_json(latest_analysis())
            return
        if parsed.path == "/api/notifications/recent":
            params = parse_qs(parsed.query)
            limit_text = params.get("limit", ["8"])[0]
            try:
                limit = int(limit_text)
            except ValueError:
                limit = 8
            self._send_json(recent_notifications(limit))
            return
        if parsed.path == "/api/relays/latest":
            self._send_json(latest_relay_states())
            return
        if parsed.path == "/api/caretaker/session":
            params = parse_qs(parsed.query)
            session_id = params.get("session_id", ["display-main"])[0]
            limit_text = params.get("limit", ["30"])[0]
            try:
                self._send_json(CARETAKER.history(session_id, int(limit_text)))
            except (TypeError, ValueError) as exc:
                self._send_json({"error": str(exc)}, HTTPStatus.BAD_REQUEST)
            return
        if parsed.path == "/api/caretaker/suggestions":
            self._send_json({"suggestions": CARETAKER.suggestions})
            return
        if parsed.path == "/api/health":
            self._send_json({"ok": True})
            return
        self._send_json({"error": "not found"}, HTTPStatus.NOT_FOUND)

    def do_POST(self) -> None:
        parsed = urlparse(self.path)
        body = self._read_json_body()
        if body is None:
            return

        if parsed.path == "/api/demo/trigger":
            scenario = str(body.get("scenario", "NONE"))
            if scenario not in ("SOS_OR_FALL_SIM", "LONG_STILL_NO_RESPONSE", "OFFLINE_AUTONOMY"):
                self._send_json({"error": "invalid scenario"}, HTTPStatus.BAD_REQUEST)
                return
            self._send_json(publish_demo_command("TRIGGER_SCENARIO", scenario, int(body.get("value", 1))))
            return
        if parsed.path == "/api/caretaker/chat":
            try:
                response = CARETAKER.chat(
                    str(body.get("session_id", "display-main")),
                    str(body.get("message", "")),
                )
            except (TypeError, ValueError) as exc:
                self._send_json({"error": str(exc)}, HTTPStatus.BAD_REQUEST)
                return
            self._send_json(response)
            return
        if parsed.path == "/api/demo/ack":
            self._send_json(publish_demo_command("USER_ACK", "NONE", 1))
            return
        if parsed.path == "/api/demo/clear":
            self._send_json(publish_demo_command("CLEAR_ALARM", "NONE", 1))
            return
        if parsed.path == "/api/demo/network":
            online = bool(body.get("online", False))
            self._send_json(publish_demo_command("SIMULATE_NETWORK", "OFFLINE_AUTONOMY", 1 if online else 0))
            return
        if parsed.path.startswith("/api/relay/") and parsed.path.endswith("/set"):
            parts = parsed.path.split("/")
            try:
                relay_id = int(parts[3])
            except (IndexError, ValueError):
                self._send_json({"error": "invalid relay id"}, HTTPStatus.BAD_REQUEST)
                return
            action = str(body.get("action", ""))
            if relay_id < 1 or relay_id > 4 or action not in ("ON", "OFF"):
                self._send_json({"error": "invalid relay request"}, HTTPStatus.BAD_REQUEST)
                return
            self._send_json(publish_relay_command(relay_id, action))
            return

        self._send_json({"error": "not found"}, HTTPStatus.NOT_FOUND)

    def log_message(self, format: str, *args: Any) -> None:
        print(f"{self.address_string()} - {format % args}", flush=True)

    def _read_json_body(self) -> dict[str, Any] | None:
        length = int(self.headers.get("Content-Length", "0"))
        if length <= 0:
            return {}
        raw = self.rfile.read(length)
        try:
            data = json.loads(raw.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError):
            self._send_json({"error": "invalid JSON"}, HTTPStatus.BAD_REQUEST)
            return None
        if not isinstance(data, dict):
            self._send_json({"error": "JSON body must be an object"}, HTTPStatus.BAD_REQUEST)
            return None
        return data

    def _send_html(self, html: str) -> None:
        payload = html.encode("utf-8")
        self.send_response(HTTPStatus.OK)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def _send_json(self, data: dict[str, Any], status: HTTPStatus = HTTPStatus.OK) -> None:
        payload = json.dumps(data, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)


@contextmanager
def connect_db():
    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row
    try:
        yield conn
    finally:
        conn.close()


def publish(topic: str, payload: dict[str, Any] | str) -> bool:
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
    client.connect(MQTT_HOST, MQTT_PORT, keepalive=30)
    message = payload if isinstance(payload, str) else json.dumps(payload, ensure_ascii=False, separators=(",", ":"))
    result = client.publish(topic, message, qos=0, retain=False)
    client.disconnect()
    return result.rc == mqtt.MQTT_ERR_SUCCESS


def next_request_id() -> int:
    return int(time.time() * 1000) % 2147483647


def publish_demo_command(command_type: str, scenario: str, value: int) -> dict[str, Any]:
    request_id = next_request_id()
    payload = {
        "command_id": f"demo-{request_id}",
        "request_id": request_id,
        "command_type": command_type,
        "scenario": scenario,
        "value": value,
        "source": "dashboard",
    }
    if not publish(MQTT_DEMO_COMMAND_TOPIC, payload):
        return {"ok": False, "error": "failed to publish demo command", **payload}
    return {"ok": True, **payload}


def publish_relay_command(relay_id: int, action: str) -> dict[str, Any]:
    request_id = next_request_id()
    payload = {
        "request_id": request_id,
        "action": action,
        "source": "dashboard",
    }
    topic = MQTT_RELAY_TOPIC_TEMPLATE.format(relay_id=relay_id)
    if not publish(topic, payload):
        return {"ok": False, "error": "failed to publish relay command", "topic": topic, **payload}
    return {"ok": True, "topic": topic, **payload}


def latest_status() -> dict[str, Any]:
    if not os.path.exists(DB_PATH):
        return {"available": False}
    with connect_db() as conn:
        row = conn.execute(
            "SELECT received_at, payload_json FROM raw_status ORDER BY id DESC LIMIT 1"
        ).fetchone()
    if row is None:
        return {"available": False}
    payload = json.loads(row["payload_json"])
    payload["available"] = True
    payload["received_at"] = row["received_at"]
    return payload


def status_history(limit: int = 60) -> dict[str, Any]:
    if not os.path.exists(DB_PATH):
        return {"samples": []}
    bounded_limit = max(2, min(limit, 240))
    try:
        with connect_db() as conn:
            rows = conn.execute(
                """
                SELECT received_at, payload_json
                FROM raw_status
                ORDER BY id DESC
                LIMIT ?
                """,
                (bounded_limit,),
            ).fetchall()
    except sqlite3.OperationalError:
        return {"samples": []}

    samples: list[dict[str, Any]] = []
    for row in reversed(rows):
        try:
            payload = json.loads(row["payload_json"])
        except (TypeError, json.JSONDecodeError):
            continue
        payload["received_at"] = row["received_at"]
        samples.append(payload)
    return {"samples": samples}


def latest_analysis() -> dict[str, Any]:
    if not os.path.exists(DB_PATH):
        return {"available": False}
    try:
        with connect_db() as conn:
            row = conn.execute(
                """
                SELECT created_at, payload_json
                FROM analysis_results
                ORDER BY id DESC
                LIMIT 1
                """
            ).fetchone()
    except sqlite3.OperationalError:
        return {"available": False}
    if row is None:
        return {"available": False}
    try:
        payload = json.loads(row["payload_json"])
    except (TypeError, json.JSONDecodeError):
        return {"available": False}
    payload["available"] = True
    payload["created_at"] = row["created_at"]
    return payload


def recent_notifications(limit: int = 8) -> dict[str, Any]:
    if not os.path.exists(DB_PATH):
        return {"notifications": []}
    bounded_limit = max(1, min(limit, 40))
    try:
        with connect_db() as conn:
            rows = conn.execute(
                """
                SELECT created_at, notice_type, decision, payload_json
                FROM notification_logs
                ORDER BY id DESC
                LIMIT ?
                """,
                (bounded_limit,),
            ).fetchall()
    except sqlite3.OperationalError:
        return {"notifications": []}

    notifications: list[dict[str, Any]] = []
    for row in rows:
        try:
            payload = json.loads(row["payload_json"])
        except (TypeError, json.JSONDecodeError):
            payload = {}
        payload["created_at"] = row["created_at"]
        payload["notice_type"] = row["notice_type"]
        payload["decision"] = row["decision"]
        notifications.append(payload)
    return {"notifications": notifications}


def latest_relay_states() -> dict[str, Any]:
    if not os.path.exists(DB_PATH):
        return {"relays": []}
    try:
        with connect_db() as conn:
            rows = conn.execute(
                """
                SELECT received_at, relay_id, state, payload_json
                FROM relay_states AS current
                WHERE id = (
                    SELECT MAX(id)
                    FROM relay_states
                    WHERE relay_id = current.relay_id
                )
                ORDER BY relay_id
                """
            ).fetchall()
    except sqlite3.OperationalError:
        return {"relays": []}

    relays: list[dict[str, Any]] = []
    for row in rows:
        try:
            payload = json.loads(row["payload_json"])
        except (TypeError, json.JSONDecodeError):
            payload = {}
        payload["received_at"] = row["received_at"]
        payload["relay_id"] = row["relay_id"]
        payload["state"] = row["state"]
        relays.append(payload)
    return {"relays": relays}


def recent_events(limit: int = 20) -> dict[str, Any]:
    if not os.path.exists(DB_PATH):
        return {"events": []}
    bounded_limit = max(1, min(limit, 100))
    with connect_db() as conn:
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


def current_alarm() -> dict[str, Any]:
    if not os.path.exists(DB_PATH):
        return {"active": False}
    with connect_db() as conn:
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
          <button onclick="relay(3, 'ON')">R3 ON</button>
          <button onclick="relay(3, 'OFF')">R3 OFF</button>
          <button onclick="relay(4, 'ON')">R4 ON</button>
          <button onclick="relay(4, 'OFF')">R4 OFF</button>
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
      const keys = ['available', 'seq', 'temperature', 'humidity', 'gas', 'presence', 'risk', 'event', 'relay_state_mask', 'cloud_perm_mask'];
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


LEGACY_DISPLAY_PAGE = """
<!doctype html>
<html lang="zh-CN">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Eldercare Display</title>
  <style>
    :root {
      color-scheme: dark;
      --bg: #111316;
      --band: #191d22;
      --panel: #20252b;
      --panel-soft: #262c32;
      --line: #37404a;
      --text: #eef2f6;
      --muted: #9ba7b4;
      --good: #34c579;
      --warn: #f2a340;
      --danger: #ef635f;
      --info: #66a7ff;
      --shadow: 0 22px 60px rgba(0, 0, 0, 0.34);
    }
    * { box-sizing: border-box; }
    body {
      margin: 0;
      min-height: 100vh;
      background:
        linear-gradient(180deg, rgba(255, 255, 255, 0.04), rgba(255, 255, 255, 0) 260px),
        var(--bg);
      color: var(--text);
      font-family: "Segoe UI", Arial, sans-serif;
      letter-spacing: 0;
    }
    main {
      width: min(1360px, calc(100vw - 32px));
      margin: 0 auto;
      padding: 18px 0 26px;
      display: grid;
      gap: 14px;
    }
    .topbar {
      min-height: 54px;
      display: flex;
      align-items: center;
      justify-content: space-between;
      gap: 14px;
      border-bottom: 1px solid var(--line);
    }
    h1, h2, h3, p { margin: 0; }
    h1 {
      font-size: 22px;
      line-height: 1.15;
      font-weight: 700;
    }
    h2 {
      font-size: 13px;
      line-height: 1.2;
      color: var(--muted);
      font-weight: 600;
      text-transform: uppercase;
    }
    h3 {
      font-size: 15px;
      line-height: 1.2;
      font-weight: 650;
    }
    a {
      color: var(--info);
      text-decoration: none;
    }
    .top-meta {
      display: flex;
      align-items: center;
      justify-content: flex-end;
      gap: 10px;
      color: var(--muted);
      font-size: 13px;
      flex-wrap: wrap;
    }
    .chip {
      min-height: 30px;
      display: inline-flex;
      align-items: center;
      gap: 7px;
      border: 1px solid var(--line);
      border-radius: 6px;
      padding: 0 10px;
      background: rgba(255, 255, 255, 0.04);
      white-space: nowrap;
    }
    .dot {
      width: 9px;
      height: 9px;
      border-radius: 50%;
      background: var(--muted);
      box-shadow: 0 0 0 4px rgba(155, 167, 180, 0.12);
    }
    .dot.good {
      background: var(--good);
      box-shadow: 0 0 0 4px rgba(52, 197, 121, 0.14);
    }
    .dot.warn {
      background: var(--warn);
      box-shadow: 0 0 0 4px rgba(242, 163, 64, 0.14);
    }
    .dot.danger {
      background: var(--danger);
      box-shadow: 0 0 0 4px rgba(239, 99, 95, 0.16);
    }
    .overview {
      display: grid;
      grid-template-columns: minmax(0, 1.35fr) minmax(330px, 0.65fr);
      gap: 14px;
    }
    .hero {
      min-height: 318px;
      border: 1px solid var(--line);
      border-radius: 8px;
      background:
        radial-gradient(circle at 12% 14%, rgba(102, 167, 255, 0.22), transparent 28%),
        linear-gradient(135deg, rgba(32, 37, 43, 0.98), rgba(27, 31, 36, 0.98));
      box-shadow: var(--shadow);
      padding: 20px;
      display: grid;
      grid-template-columns: minmax(0, 0.9fr) minmax(320px, 1.1fr);
      gap: 18px;
      overflow: hidden;
    }
    .status-stack {
      display: grid;
      align-content: space-between;
      gap: 22px;
    }
    .state-label {
      font-size: 14px;
      color: var(--muted);
      margin-bottom: 8px;
    }
    .state-title {
      font-size: clamp(34px, 5vw, 58px);
      line-height: 0.96;
      font-weight: 800;
      letter-spacing: 0;
      overflow-wrap: anywhere;
    }
    .state-subtitle {
      margin-top: 12px;
      max-width: 540px;
      color: #c6d0dc;
      font-size: 15px;
      line-height: 1.5;
    }
    .risk-row {
      display: grid;
      grid-template-columns: 110px 1fr;
      gap: 16px;
      align-items: center;
    }
    .risk-dial {
      width: 110px;
      aspect-ratio: 1;
      border-radius: 50%;
      display: grid;
      place-items: center;
      background: conic-gradient(var(--good) 0deg, var(--good) 30deg, #323941 30deg);
      border: 1px solid var(--line);
      box-shadow: inset 0 0 0 10px rgba(17, 19, 22, 0.74);
    }
    .risk-dial strong {
      font-size: 42px;
      line-height: 1;
    }
    .risk-copy {
      display: grid;
      gap: 8px;
      color: var(--muted);
      font-size: 14px;
      min-width: 0;
    }
    .sensor-board {
      display: grid;
      grid-template-columns: repeat(2, minmax(0, 1fr));
      gap: 10px;
    }
    .relay-board {
      display: grid;
      grid-template-columns: repeat(4, minmax(0, 1fr));
      gap: 8px;
    }
    .metric {
      min-height: 92px;
      background: rgba(255, 255, 255, 0.065);
      border: 1px solid rgba(255, 255, 255, 0.11);
      border-radius: 8px;
      padding: 12px;
      display: grid;
      align-content: space-between;
      gap: 10px;
    }
    .metric span {
      color: var(--muted);
      font-size: 13px;
    }
    .metric strong {
      display: block;
      font-size: 27px;
      line-height: 1;
      overflow-wrap: anywhere;
    }
    .relay-pill {
      min-height: 64px;
      border: 1px solid var(--line);
      border-radius: 8px;
      background: var(--panel-soft);
      padding: 10px;
      display: grid;
      align-content: center;
      gap: 6px;
    }
    .relay-pill span {
      color: var(--muted);
      font-size: 12px;
    }
    .relay-pill strong {
      font-size: 18px;
      line-height: 1;
    }
    .relay-pill.on {
      border-color: rgba(52, 197, 121, 0.58);
      background: rgba(52, 197, 121, 0.12);
    }
    .relay-pill.off {
      border-color: rgba(155, 167, 180, 0.32);
    }
    .person-map {
      position: relative;
      min-height: 318px;
      border: 1px solid var(--line);
      border-radius: 8px;
      background:
        linear-gradient(90deg, transparent 31%, rgba(255,255,255,0.06) 31%, rgba(255,255,255,0.06) 32%, transparent 32%),
        linear-gradient(0deg, transparent 48%, rgba(255,255,255,0.06) 48%, rgba(255,255,255,0.06) 49%, transparent 49%),
        var(--band);
      box-shadow: var(--shadow);
      padding: 16px;
      overflow: hidden;
    }
    .room-label {
      position: absolute;
      color: rgba(238, 242, 246, 0.58);
      font-size: 12px;
      text-transform: uppercase;
    }
    .room-label.living { left: 18px; top: 18px; }
    .room-label.bed { right: 18px; top: 18px; }
    .room-label.hall { left: 18px; bottom: 18px; }
    .presence-marker {
      position: absolute;
      left: 50%;
      top: 53%;
      width: 72px;
      height: 72px;
      transform: translate(-50%, -50%);
      border-radius: 50%;
      background: rgba(52, 197, 121, 0.18);
      border: 1px solid rgba(52, 197, 121, 0.68);
      display: grid;
      place-items: center;
    }
    .presence-marker::before {
      content: "";
      width: 30px;
      height: 30px;
      border-radius: 50%;
      background: var(--good);
    }
    .presence-marker.off {
      background: rgba(155, 167, 180, 0.12);
      border-color: rgba(155, 167, 180, 0.45);
    }
    .presence-marker.off::before {
      background: var(--muted);
    }
    .presence-marker.alert {
      background: rgba(239, 99, 95, 0.2);
      border-color: rgba(239, 99, 95, 0.74);
    }
    .presence-marker.alert::before {
      background: var(--danger);
    }
    .content-grid {
      display: grid;
      grid-template-columns: minmax(0, 0.92fr) minmax(360px, 1.08fr);
      gap: 14px;
    }
    .panel {
      border: 1px solid var(--line);
      border-radius: 8px;
      background: var(--panel);
      padding: 14px;
      min-width: 0;
    }
    .panel-head {
      min-height: 28px;
      display: flex;
      align-items: center;
      justify-content: space-between;
      gap: 12px;
      margin-bottom: 12px;
    }
    .flow {
      display: grid;
      grid-template-columns: repeat(4, minmax(0, 1fr));
      gap: 8px;
    }
    .step {
      min-height: 112px;
      border: 1px solid var(--line);
      border-radius: 8px;
      padding: 12px;
      background: var(--panel-soft);
      display: grid;
      align-content: space-between;
      gap: 12px;
    }
    .step .num {
      width: 30px;
      height: 30px;
      border-radius: 50%;
      display: grid;
      place-items: center;
      background: #333b44;
      color: var(--muted);
      font-weight: 700;
    }
    .step.active {
      border-color: rgba(102, 167, 255, 0.62);
      background: rgba(102, 167, 255, 0.12);
    }
    .step.done .num {
      color: #101418;
      background: var(--good);
    }
    .step.alert .num {
      color: #101418;
      background: var(--danger);
    }
    .timeline {
      display: grid;
      gap: 8px;
      max-height: 374px;
      overflow: auto;
      padding-right: 2px;
    }
    .event {
      display: grid;
      grid-template-columns: 88px 1fr;
      gap: 10px;
      min-height: 74px;
      border: 1px solid var(--line);
      border-radius: 8px;
      background: var(--panel-soft);
      padding: 10px;
    }
    .event-time {
      color: var(--muted);
      font-size: 12px;
      line-height: 1.35;
    }
    .event-main {
      min-width: 0;
      display: grid;
      gap: 6px;
    }
    .event-title {
      font-size: 14px;
      font-weight: 700;
      overflow-wrap: anywhere;
    }
    .event-detail {
      color: var(--muted);
      font-size: 12px;
      line-height: 1.35;
      overflow-wrap: anywhere;
    }
    .empty {
      min-height: 120px;
      border: 1px dashed var(--line);
      border-radius: 8px;
      display: grid;
      place-items: center;
      color: var(--muted);
      text-align: center;
      padding: 14px;
    }
    @media (max-width: 1020px) {
      main { width: min(100vw - 20px, 840px); }
      .overview, .hero, .content-grid { grid-template-columns: 1fr; }
      .hero, .person-map { min-height: 280px; }
      .flow { grid-template-columns: repeat(2, minmax(0, 1fr)); }
      .relay-board { grid-template-columns: repeat(2, minmax(0, 1fr)); }
    }
    @media (max-width: 620px) {
      main { width: min(100vw - 16px, 540px); padding-top: 10px; }
      .topbar { align-items: flex-start; flex-direction: column; padding-bottom: 12px; }
      .top-meta { justify-content: flex-start; }
      .hero { padding: 14px; }
      .risk-row, .sensor-board, .flow { grid-template-columns: 1fr; }
      .event { grid-template-columns: 1fr; }
    }
  </style>
</head>
<body>
  <main>
    <header class="topbar">
      <div>
        <h1>独居老人看护展示面板</h1>
      </div>
      <div class="top-meta">
        <span class="chip"><span id="healthDot" class="dot"></span><span id="healthText">连接中</span></span>
        <span class="chip" id="nodeText">node01</span>
        <span class="chip" id="updatedText">--</span>
        <a class="chip" href="/">控制台</a>
      </div>
    </header>

    <section class="overview">
      <div class="hero">
        <div class="status-stack">
          <div>
            <p class="state-label">CURRENT STATE</p>
            <p id="stateTitle" class="state-title">等待数据</p>
            <p id="stateSubtitle" class="state-subtitle">启动假数据脚本后，这里会跟随 status/event/alarm 三类数据实时变化。</p>
          </div>
          <div class="risk-row">
            <div id="riskDial" class="risk-dial"><strong id="riskValue">0</strong></div>
            <div class="risk-copy">
              <h3 id="riskLabel">风险等级</h3>
              <p id="riskCopy">暂无风险输入。</p>
            </div>
          </div>
        </div>
        <div class="sensor-board">
          <div class="metric"><span>体征温度</span><strong id="tempValue">--</strong></div>
          <div class="metric"><span>环境湿度</span><strong id="humidityValue">--</strong></div>
          <div class="metric"><span>燃气读数</span><strong id="gasValue">--</strong></div>
          <div class="metric"><span>人体存在</span><strong id="presenceValue">--</strong></div>
          <div class="metric"><span>事件标记</span><strong id="eventValue">--</strong></div>
          <div class="metric"><span>状态序号</span><strong id="seqValue">--</strong></div>
        </div>
      </div>

      <div class="person-map">
        <span class="room-label living">Living</span>
        <span class="room-label bed">Bedroom</span>
        <span class="room-label hall">Hallway</span>
        <div id="presenceMarker" class="presence-marker off"></div>
      </div>
    </section>

    <section class="content-grid">
      <div class="panel">
        <div class="panel-head">
          <h2>Closed Loop</h2>
          <span class="chip" id="alarmText">alarm: --</span>
        </div>
        <div class="flow">
          <div id="stepTrigger" class="step">
            <span class="num">1</span>
            <div><h3>触发</h3><p class="event-detail">远程演示或本地传感事件进入系统</p></div>
          </div>
          <div id="stepNotice" class="step">
            <span class="num">2</span>
            <div><h3>通知</h3><p class="event-detail">进入等待确认或看护判断状态</p></div>
          </div>
          <div id="stepEscalate" class="step">
            <span class="num">3</span>
            <div><h3>升级</h3><p class="event-detail">无响应、跌倒、SOS 或高风险触发告警</p></div>
          </div>
          <div id="stepClear" class="step">
            <span class="num">4</span>
            <div><h3>闭环</h3><p class="event-detail">家属确认或清除后回到稳定状态</p></div>
          </div>
        </div>
      </div>

      <div class="panel">
        <div class="panel-head">
          <h2>Relay State</h2>
          <span class="chip" id="relayMaskText">mask: --</span>
        </div>
        <div class="relay-board">
          <div id="relayPill1" class="relay-pill"><span>Relay 1</span><strong>--</strong></div>
          <div id="relayPill2" class="relay-pill"><span>Relay 2</span><strong>--</strong></div>
          <div id="relayPill3" class="relay-pill"><span>Relay 3</span><strong>--</strong></div>
          <div id="relayPill4" class="relay-pill"><span>Relay 4</span><strong>--</strong></div>
        </div>
      </div>

      <div class="panel">
        <div class="panel-head">
          <h2>Recent Events</h2>
          <span class="chip" id="eventCount">0 条</span>
        </div>
        <div id="timeline" class="timeline">
          <div class="empty">暂无事件</div>
        </div>
      </div>
    </section>
  </main>

  <script>
    const $ = (id) => document.getElementById(id);
    const levelText = ["稳定", "关注", "警惕", "告警", "紧急"];

    function text(value, fallback = "--") {
      if (value === null || value === undefined || value === "") return fallback;
      return String(value);
    }

    function fmtTime(value) {
      if (!value) return "--";
      const d = new Date(value);
      if (Number.isNaN(d.getTime())) return String(value).replace("T", " ");
      return d.toLocaleString("zh-CN", {hour12: false});
    }

    function shortTime(value) {
      if (!value) return "--";
      const d = new Date(value);
      if (Number.isNaN(d.getTime())) return String(value).replace("T", " ").slice(5, 19);
      return d.toLocaleTimeString("zh-CN", {hour12: false});
    }

    function riskColor(risk) {
      if (risk >= 4) return "var(--danger)";
      if (risk >= 2) return "var(--warn)";
      return "var(--good)";
    }

    function classifyState(status, alarm) {
      const risk = Number(status.risk ?? 0);
      const event = text(status.event, "NORMAL");
      const active = Boolean(alarm.active);
      const alarmEvent = alarm.event || {};
      const stateAfter = text(alarmEvent.state_after, "");
      const result = text(alarmEvent.result, "");
      if (!active && (stateAfter === "CLEARED" || result === "ACKNOWLEDGED" || result === "CLEARED")) {
        return {
          title: "已闭环",
          subtitle: `最近事件已处理：${result || stateAfter}。面板保留事件轨迹用于复盘。`,
          dot: "good",
          marker: status.presence ? "" : "off"
        };
      }
      if (active || risk >= 3 || stateAfter === "ALARM" || stateAfter === "NO_RESPONSE") {
        return {
          title: risk >= 4 || stateAfter === "NO_RESPONSE" ? "紧急处置" : "告警处理中",
          subtitle: `当前事件 ${event}，风险 ${risk}，系统已进入家属关注视图。`,
          dot: "danger",
          marker: "alert"
        };
      }
      if (risk >= 2) {
        return {
          title: "需要关注",
          subtitle: `传感器提示风险上升，当前事件 ${event}。`,
          dot: "warn",
          marker: status.presence ? "" : "off"
        };
      }
      return {
        title: status.available ? "状态稳定" : "等待数据",
        subtitle: status.available ? "最近状态正常，系统正在持续刷新传感器与事件流。" : "运行假数据脚本后会出现完整状态变化。",
        dot: status.available ? "good" : "",
        marker: status.presence ? "" : "off"
      };
    }

    function setStepClasses(alarm, events) {
      const latest = events[0] || {};
      const state = text(latest.state_after, "");
      const result = text(latest.result, "");
      const type = text(latest.event_type, "");
      const active = Boolean(alarm.active);
      const ids = ["stepTrigger", "stepNotice", "stepEscalate", "stepClear"];
      for (const id of ids) $(id).className = "step";
      if (events.length > 0 || type === "REMOTE_TRIGGER") $("stepTrigger").classList.add("done");
      if (["ACK_WAIT", "LOCAL_NOTICE"].includes(state) || events.length > 0) $("stepNotice").classList.add("active");
      if (active || ["ALARM", "NO_RESPONSE"].includes(state) || result === "ESCALATED") {
        $("stepNotice").classList.remove("active");
        $("stepNotice").classList.add("done");
        $("stepEscalate").classList.add("alert");
      }
      if (["ACKNOWLEDGED", "CLEARED"].includes(result) || state === "CLEARED") {
        $("stepNotice").classList.remove("active");
        $("stepNotice").classList.add("done");
        $("stepEscalate").classList.remove("alert");
        $("stepEscalate").classList.add("done");
        $("stepClear").classList.add("done");
      }
    }

    function renderStatus(status, alarm) {
      const risk = Number(status.risk ?? 0);
      const degrees = Math.max(0, Math.min(4, risk)) * 90;
      const state = classifyState(status, alarm);
      $("stateTitle").textContent = state.title;
      $("stateSubtitle").textContent = state.subtitle;
      $("healthDot").className = `dot ${state.dot}`;
      $("healthText").textContent = status.available ? "数据在线" : "暂无数据";
      $("updatedText").textContent = fmtTime(status.received_at);
      $("nodeText").textContent = text(status.node_id, "node01");
      $("riskValue").textContent = text(risk, "0");
      $("riskLabel").textContent = levelText[Math.max(0, Math.min(4, risk))] || "风险等级";
      $("riskCopy").textContent = `风险值来自冻结协议 status.risk，0 表示正常，3 以上进入告警展示。`;
      $("riskDial").style.background = `conic-gradient(${riskColor(risk)} 0deg, ${riskColor(risk)} ${degrees}deg, #323941 ${degrees}deg)`;
      $("tempValue").textContent = status.temperature === undefined ? "--" : `${status.temperature} ℃`;
      $("humidityValue").textContent = status.humidity === undefined ? "--" : `${status.humidity} %`;
      $("gasValue").textContent = text(status.gas);
      $("presenceValue").textContent = status.presence === undefined ? "--" : (status.presence ? "有人" : "无人");
      $("eventValue").textContent = text(status.event, "NORMAL");
      $("seqValue").textContent = text(status.seq);
      $("alarmText").textContent = alarm.active ? "alarm: active" : "alarm: clear";
      $("presenceMarker").className = `presence-marker ${state.marker}`;
      renderRelayState(status);
    }

    function renderRelayState(status) {
      const mask = Number(status.relay_state_mask ?? 0);
      const perm = Number(status.cloud_perm_mask ?? 15);
      $("relayMaskText").textContent = `mask: ${mask} / perm: ${perm}`;
      for (let id = 1; id <= 4; id++) {
        const bit = 1 << (id - 1);
        const isOn = (mask & bit) !== 0;
        const isAllowed = (perm & bit) !== 0;
        const pill = $(`relayPill${id}`);
        pill.className = `relay-pill ${isOn ? "on" : "off"}`;
        pill.querySelector("strong").textContent = isOn ? "ON" : "OFF";
        pill.querySelector("span").textContent = `Relay ${id}${isAllowed ? "" : " / locked"}`;
      }
    }

    function renderEvents(events) {
      $("eventCount").textContent = `${events.length} 条`;
      if (!events.length) {
        $("timeline").innerHTML = '<div class="empty">暂无事件</div>';
        return;
      }
      $("timeline").innerHTML = "";
      for (const event of events) {
        const row = document.createElement("div");
        const eventTime = document.createElement("div");
        const eventMain = document.createElement("div");
        const eventTitle = document.createElement("div");
        const eventDetail = document.createElement("div");
        row.className = "event";
        eventTime.className = "event-time";
        eventMain.className = "event-main";
        eventTitle.className = "event-title";
        eventDetail.className = "event-detail";
        eventTime.textContent = `${shortTime(event.received_at)} / ${text(event.source, "device")}`;
        eventTitle.textContent = `${text(event.scenario, "NONE")} / ${text(event.event_type, "EVENT")}`;
        eventDetail.textContent = `${text(event.state_before)} -> ${text(event.state_after)} / ${text(event.result)} / risk ${text(event.risk, 0)}`;
        eventMain.append(eventTitle, eventDetail);
        row.append(eventTime, eventMain);
        $("timeline").appendChild(row);
      }
    }

    async function refresh() {
      try {
        const [status, eventsResp, alarm] = await Promise.all([
          fetch("/api/status/latest").then((r) => r.json()),
          fetch("/api/events/recent?limit=10").then((r) => r.json()),
          fetch("/api/alarm/current").then((r) => r.json())
        ]);
        const events = eventsResp.events || [];
        renderStatus(status, alarm);
        renderEvents(events);
        setStepClasses(alarm, events);
      } catch (err) {
        $("healthDot").className = "dot danger";
        $("healthText").textContent = "连接失败";
      }
    }

    refresh();
    setInterval(refresh, 3000);
  </script>
</body>
</html>
"""


try:
    from .display_page import DISPLAY_PAGE
except ImportError:
    from display_page import DISPLAY_PAGE


if __name__ == "__main__":
    main()
