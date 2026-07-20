"""Grounded, read-only cloud caretaker used by the competition display.

The public surface is deliberately small: ``chat`` answers one question and
``history`` restores a display session.  Data collection, risk preservation,
model invocation and deterministic fallback stay behind that interface.
"""

from __future__ import annotations

import json
import os
import re
import sqlite3
import time
import urllib.error
import urllib.request
from contextlib import closing
from dataclasses import dataclass
from datetime import datetime, timezone
from typing import Any, Protocol


RISK_LABELS = {0: "安全", 1: "关注", 2: "警告", 3: "告警"}
SESSION_PATTERN = re.compile(r"^[A-Za-z0-9_-]{1,64}$")


class ModelAdapter(Protocol):
    def complete(self, context: dict[str, Any], recent_messages: list[dict[str, str]]) -> dict[str, Any]: ...


@dataclass(frozen=True)
class CaretakerConfig:
    source_db_path: str
    session_db_path: str
    node_id: str = "node01"
    stale_after_seconds: int = 8


class OpenAICompatibleModel:
    """Minimal adapter for the already configured OpenAI-compatible endpoint."""

    def __init__(self, api_key: str, base_url: str, model: str, timeout_seconds: float) -> None:
        self._api_key = api_key
        self._base_url = base_url
        self._model = model
        self._timeout_seconds = timeout_seconds

    def complete(self, context: dict[str, Any], recent_messages: list[dict[str, str]]) -> dict[str, Any]:
        system = (
            "你是独居老人居家安全系统的云端看护管家。只依据给定的工具结果回答，"
            "不得虚构设备、数值或已执行的动作，不得降低本地规则给出的风险等级。"
            "回答简洁、医疗科技风格、面向现场讲解。只输出 JSON："
            '{"answer":"...","suggested_actions":["..."],"unknowns":["..."]}。'
            "suggested_actions 只能是只读核查或人工处置建议，不能声称已经控制设备。"
        )
        messages: list[dict[str, str]] = [{"role": "system", "content": system}]
        messages.extend(recent_messages[-6:])
        messages.append(
            {
                "role": "user",
                "content": "工具结果如下，请回答本轮问题：\n" + json.dumps(context, ensure_ascii=False),
            }
        )
        body = json.dumps(
            {
                "model": self._model,
                "temperature": 0.15,
                "response_format": {"type": "json_object"},
                "messages": messages,
            },
            ensure_ascii=False,
        ).encode("utf-8")
        request = urllib.request.Request(
            self._base_url,
            data=body,
            headers={"Authorization": f"Bearer {self._api_key}", "Content-Type": "application/json"},
            method="POST",
        )
        try:
            with urllib.request.urlopen(request, timeout=self._timeout_seconds) as response:
                payload = json.loads(response.read().decode("utf-8"))
        except (OSError, urllib.error.URLError, json.JSONDecodeError) as exc:
            raise RuntimeError(f"cloud model unavailable: {exc}") from exc

        try:
            content = payload["choices"][0]["message"]["content"]
            result = json.loads(content)
        except (KeyError, IndexError, TypeError, json.JSONDecodeError) as exc:
            raise RuntimeError("cloud model returned invalid JSON") from exc
        if not isinstance(result, dict) or not isinstance(result.get("answer"), str):
            raise RuntimeError("cloud model response is missing answer")
        return result


class CaretakerService:
    def __init__(self, config: CaretakerConfig, model: ModelAdapter | None = None) -> None:
        self._config = config
        self._model = model
        self._initialize_session_store()

    @property
    def suggestions(self) -> list[str]:
        return ["家里现在安全吗？", "刚才为什么报警？", "燃气风险恢复了吗？", "生成今日看护摘要"]

    def chat(self, session_id: str, message: str) -> dict[str, Any]:
        session_id = self._validate_session_id(session_id)
        question = message.strip()
        if not question or len(question) > 500:
            raise ValueError("message must contain 1 to 500 characters")

        intent = self._detect_intent(question)
        tools = self._collect_tools(intent)
        grounded = self._grounded_assessment(question, intent, tools)
        recent = self._recent_model_messages(session_id)

        model_used = False
        model_error: str | None = None
        answer = grounded["answer"]
        suggested_actions = grounded["suggested_actions"]
        unknowns = grounded["unknowns"]
        if self._model is not None:
            try:
                candidate = self._model.complete(
                    {"question": question, "intent": intent, "assessment": grounded, "tools": tools},
                    recent,
                )
                answer = self._safe_text(candidate.get("answer"), answer, 700)
                suggested_actions = self._safe_string_list(
                    candidate.get("suggested_actions"), suggested_actions, 4
                )
                unknowns = self._safe_string_list(candidate.get("unknowns"), unknowns, 4)
                model_used = True
            except Exception as exc:  # model failure must never break the local caretaker
                model_error = str(exc)[:160]

        generated_at = datetime.now(timezone.utc).isoformat()
        response = {
            "answer": answer,
            "care_state": grounded["care_state"],
            "risk": grounded["risk"],
            "confidence": grounded["confidence"],
            "evidence": grounded["evidence"],
            "tools_used": list(tools),
            "suggested_actions": suggested_actions,
            "unknowns": unknowns,
            "generated_at": generated_at,
            "model_used": model_used,
            "mode": "云端增强" if model_used else "本地规则兜底",
        }
        if model_error:
            response["model_error"] = model_error

        self._append_message(session_id, "user", question, None)
        self._append_message(session_id, "assistant", answer, response)
        return response

    def history(self, session_id: str, limit: int = 30) -> dict[str, Any]:
        session_id = self._validate_session_id(session_id)
        bounded = max(1, min(int(limit), 60))
        with closing(self._session_connect()) as conn:
            rows = conn.execute(
                "SELECT role, content, created_at, payload_json FROM caretaker_messages "
                "WHERE session_id = ? ORDER BY id DESC LIMIT ?",
                (session_id, bounded),
            ).fetchall()
        messages = []
        for row in reversed(rows):
            payload = self._decode_json(row["payload_json"]) if row["payload_json"] else None
            messages.append(
                {"role": row["role"], "content": row["content"], "created_at": row["created_at"], "payload": payload}
            )
        return {"session_id": session_id, "messages": messages}

    def _collect_tools(self, intent: str) -> dict[str, Any]:
        tools: dict[str, Any] = {"get_home_snapshot": self._home_snapshot()}
        if intent in {"gas", "summary", "safety"}:
            tools["get_sensor_trend"] = self._sensor_trend()
        if intent in {"alarm", "gas", "safety"}:
            tools["get_active_episode"] = self._active_episode()
            tools["get_event_timeline"] = self._recent_events()
        if intent in {"network", "safety"}:
            tools["get_device_health"] = self._device_health(tools["get_home_snapshot"])
        if intent in {"gas", "alarm"}:
            tools["get_automation_state"] = self._automation_state()
            tools["get_notification_history"] = self._notifications()
        if intent == "summary":
            tools["get_daily_summary"] = self._daily_summary()
        return tools

    def _home_snapshot(self) -> dict[str, Any]:
        row = self._source_one("SELECT received_at, payload_json FROM raw_status ORDER BY id DESC LIMIT 1")
        if row is None:
            return {"available": False, "reason": "暂无状态样本"}
        payload = self._decode_json(row["payload_json"])
        payload.update({"available": True, "received_at": row["received_at"]})
        payload["age_seconds"] = self._age_seconds(row["received_at"])
        return payload

    def _sensor_trend(self) -> dict[str, Any]:
        rows = self._source_all(
            "SELECT received_at, gas, temperature, humidity, risk FROM raw_status ORDER BY id DESC LIMIT 30"
        )
        if not rows:
            return {"sample_count": 0}
        chronological = list(reversed(rows))
        gas_values = [int(row["gas"]) for row in chronological]
        first = gas_values[0]
        last = gas_values[-1]
        direction = "平稳"
        if last > first + 20:
            direction = "上升"
        elif last < first - 20:
            direction = "回落"
        return {
            "sample_count": len(rows),
            "window_start": chronological[0]["received_at"],
            "window_end": chronological[-1]["received_at"],
            "gas_first": first,
            "gas_latest": last,
            "gas_peak": max(gas_values),
            "gas_direction": direction,
            "risk_peak": max(int(row["risk"]) for row in rows),
        }

    def _recent_events(self) -> dict[str, Any]:
        rows = self._source_all(
            "SELECT received_at, payload_json, is_backfilled FROM event_logs ORDER BY id DESC LIMIT 12"
        )
        events = []
        for row in rows:
            event = self._decode_json(row["payload_json"])
            event.update({"received_at": row["received_at"], "is_backfilled": bool(row["is_backfilled"])})
            events.append(event)
        return {"events": events}

    def _active_episode(self) -> dict[str, Any]:
        events = self._recent_events()["events"]
        if not events:
            return {"active": False, "reason": "暂无事件记录"}
        latest = events[0]
        cleared = latest.get("state_after") == "CLEARED" or latest.get("result") in {"ACKNOWLEDGED", "CLEARED"}
        risky = int(latest.get("risk", 0) or 0) >= 2 or latest.get("state_after") in {"ACK_WAIT", "ALARM", "NO_RESPONSE"}
        return {"active": bool(risky and not cleared), "latest": latest, "timeline_size": len(events)}

    def _device_health(self, snapshot: dict[str, Any]) -> dict[str, Any]:
        if not snapshot.get("available"):
            return {"online": False, "status": "无数据", "age_seconds": None}
        age = snapshot.get("age_seconds")
        online = isinstance(age, (int, float)) and age <= self._config.stale_after_seconds
        return {
            "online": online,
            "status": "在线" if online else "状态超时，本地自治中",
            "age_seconds": age,
            "threshold_seconds": self._config.stale_after_seconds,
        }

    def _automation_state(self) -> dict[str, Any]:
        rows = self._source_all(
            "SELECT received_at, relay_id, state, payload_json FROM relay_states AS current "
            "WHERE id = (SELECT MAX(id) FROM relay_states WHERE relay_id = current.relay_id) ORDER BY relay_id"
        )
        relays = [{"relay_id": row["relay_id"], "state": row["state"], "received_at": row["received_at"]} for row in rows]
        return {"relays": relays, "fan_on": any(row["relay_id"] == 1 and row["state"] == "ON" for row in rows)}

    def _notifications(self) -> dict[str, Any]:
        rows = self._source_all(
            "SELECT created_at, notice_type, decision FROM notification_logs ORDER BY id DESC LIMIT 8"
        )
        return {"notifications": [dict(row) for row in rows]}

    def _daily_summary(self) -> dict[str, Any]:
        day_prefix = datetime.now(timezone.utc).date().isoformat() + "%"
        status = self._source_one(
            "SELECT COUNT(*) AS samples, MAX(gas) AS gas_peak, MAX(risk) AS risk_peak, "
            "AVG(temperature) AS temperature_avg, AVG(humidity) AS humidity_avg "
            "FROM raw_status WHERE received_at LIKE ?",
            (day_prefix,),
        )
        events = self._source_one(
            "SELECT COUNT(*) AS total, SUM(CASE WHEN risk >= 2 THEN 1 ELSE 0 END) AS risky "
            "FROM event_logs WHERE received_at LIKE ?",
            (day_prefix,),
        )
        notices = self._source_one(
            "SELECT COUNT(*) AS total FROM notification_logs WHERE created_at LIKE ?",
            (day_prefix,),
        )
        return {
            "date_utc": day_prefix[:-1],
            "samples": int(status["samples"] or 0) if status else 0,
            "gas_peak": status["gas_peak"] if status else None,
            "risk_peak": int(status["risk_peak"] or 0) if status else 0,
            "temperature_avg": round(float(status["temperature_avg"]), 1) if status and status["temperature_avg"] is not None else None,
            "humidity_avg": round(float(status["humidity_avg"]), 1) if status and status["humidity_avg"] is not None else None,
            "event_count": int(events["total"] or 0) if events else 0,
            "risky_event_count": int(events["risky"] or 0) if events else 0,
            "notification_count": int(notices["total"] or 0) if notices else 0,
        }

    def _grounded_assessment(self, question: str, intent: str, tools: dict[str, Any]) -> dict[str, Any]:
        snapshot = tools["get_home_snapshot"]
        risk = int(snapshot.get("risk", 0) or 0) if snapshot.get("available") else 0
        episode = tools.get("get_active_episode", {})
        if episode.get("active"):
            risk = max(risk, int(episode.get("latest", {}).get("risk", 0) or 0))
        health = tools.get("get_device_health") or self._device_health(snapshot)
        care_state = "unknown"
        if snapshot.get("available"):
            care_state = "offline" if not health.get("online") else ("alarm" if risk >= 3 else "attention" if risk else "normal")
        confidence = "高" if snapshot.get("available") and health.get("online") else "中" if snapshot.get("available") else "低"
        evidence = self._build_evidence(intent, tools)
        unknowns: list[str] = []
        if not snapshot.get("available"):
            unknowns.append("当前没有可用的家庭状态样本")
        elif not health.get("online"):
            unknowns.append("云端无法确认断网后的最新传感器值")

        label = RISK_LABELS.get(risk, "未知")
        if intent == "gas":
            trend = tools.get("get_sensor_trend", {})
            automation = tools.get("get_automation_state", {})
            if trend.get("sample_count"):
                recovered = trend.get("gas_direction") == "回落" and risk < 2
                answer = (
                    f"燃气最新值 {trend.get('gas_latest')}，窗口峰值 {trend.get('gas_peak')}，趋势为{trend.get('gas_direction')}。"
                    f"当前风险为{label}；排风继电器{'已开启' if automation.get('fan_on') else '未确认开启'}。"
                    + ("数值已回落到可观察状态。" if recovered else "仍需继续观察后续样本。")
                )
            else:
                answer = "目前没有足够的燃气趋势样本，无法判断是否已经恢复。"
        elif intent == "alarm":
            latest = episode.get("latest", {})
            answer = (
                f"最近事件为 {latest.get('event_type', '未知')}，状态 {latest.get('state_before', '-')} → "
                f"{latest.get('state_after', '-')}，处理结果 {latest.get('result', '-')}。"
                if latest else "目前没有可用于解释报警的事件记录。"
            )
        elif intent == "network":
            age = health.get("age_seconds")
            answer = f"设备{health.get('status', '状态未知')}，最近状态距今 {age:.1f} 秒。" if isinstance(age, (int, float)) else "当前没有设备在线证据。"
        elif intent == "summary":
            summary = tools.get("get_daily_summary", {})
            answer = (
                f"今日已接收 {summary.get('samples', 0)} 个状态样本，最高风险 {summary.get('risk_peak', 0)}，"
                f"燃气峰值 {summary.get('gas_peak', '-')}，记录 {summary.get('event_count', 0)} 个事件并发出 "
                f"{summary.get('notification_count', 0)} 条通知。"
            )
        else:
            if snapshot.get("available"):
                answer = (
                    f"当前综合状态为{label}，温度 {snapshot.get('temperature', '-')}℃、湿度 "
                    f"{snapshot.get('humidity', '-')}%、燃气 {snapshot.get('gas', '-')}、"
                    f"人体存在 {snapshot.get('presence', '-')}。设备{health.get('status', '状态未知')}。"
                )
            else:
                answer = "当前没有家庭状态数据，云端无法给出可靠的安全判断；本地端仍应按既定规则自治。"

        actions = ["继续观察下一批状态与事件"]
        if risk >= 3:
            actions = ["现场确认老人状态", "核对语音确认与通知送达记录"]
        elif not health.get("online"):
            actions = ["查看上位机的本地自治状态", "恢复局域网后核对事件补传"]
        return {
            "answer": answer,
            "care_state": care_state,
            "risk": risk,
            "confidence": confidence,
            "evidence": evidence,
            "suggested_actions": actions,
            "unknowns": unknowns,
            "question": question,
        }

    def _build_evidence(self, intent: str, tools: dict[str, Any]) -> list[dict[str, Any]]:
        evidence: list[dict[str, Any]] = []
        snapshot = tools["get_home_snapshot"]
        if snapshot.get("available"):
            evidence.append(
                {
                    "source": "STM32 状态心跳",
                    "detail": f"seq={snapshot.get('seq', '-')} risk={snapshot.get('risk', '-')} gas={snapshot.get('gas', '-')}",
                    "time": snapshot.get("received_at"),
                }
            )
        if intent == "gas" and tools.get("get_sensor_trend", {}).get("sample_count"):
            trend = tools["get_sensor_trend"]
            evidence.append(
                {"source": "传感器趋势", "detail": f"峰值 {trend['gas_peak']}，最新 {trend['gas_latest']}，{trend['gas_direction']}", "time": trend.get("window_end")}
            )
        latest = tools.get("get_active_episode", {}).get("latest")
        if latest:
            evidence.append(
                {"source": "事件日志", "detail": f"{latest.get('event_type')} / {latest.get('result')}", "time": latest.get("received_at")}
            )
        notices = tools.get("get_notification_history", {}).get("notifications", [])
        if notices:
            evidence.append(
                {"source": "通知记录", "detail": f"{notices[0].get('notice_type')} / {notices[0].get('decision')}", "time": notices[0].get("created_at")}
            )
        return evidence[:4]

    @staticmethod
    def _detect_intent(message: str) -> str:
        if any(word in message for word in ("燃气", "煤气", "排风", "气体")):
            return "gas"
        if any(word in message for word in ("报警", "摔倒", "求助", "事件", "刚才")):
            return "alarm"
        if any(word in message for word in ("断网", "离线", "在线", "网络", "心跳")):
            return "network"
        if any(word in message for word in ("摘要", "今日", "一天", "汇总")):
            return "summary"
        return "safety"

    def _initialize_session_store(self) -> None:
        parent = os.path.dirname(self._config.session_db_path)
        if parent:
            os.makedirs(parent, exist_ok=True)
        with closing(self._session_connect()) as conn:
            conn.execute(
                "CREATE TABLE IF NOT EXISTS caretaker_messages ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT, session_id TEXT NOT NULL, role TEXT NOT NULL, "
                "content TEXT NOT NULL, created_at TEXT NOT NULL, payload_json TEXT)"
            )
            conn.execute(
                "CREATE INDEX IF NOT EXISTS idx_caretaker_session ON caretaker_messages(session_id, id)"
            )
            conn.commit()

    def _append_message(self, session_id: str, role: str, content: str, payload: dict[str, Any] | None) -> None:
        with closing(self._session_connect()) as conn:
            conn.execute(
                "INSERT INTO caretaker_messages(session_id, role, content, created_at, payload_json) VALUES (?, ?, ?, ?, ?)",
                (session_id, role, content, datetime.now(timezone.utc).isoformat(), json.dumps(payload, ensure_ascii=False) if payload else None),
            )
            conn.commit()

    def _recent_model_messages(self, session_id: str) -> list[dict[str, str]]:
        return [{"role": item["role"], "content": item["content"]} for item in self.history(session_id, 6)["messages"]]

    def _source_one(self, sql: str, params: tuple[Any, ...] = ()) -> sqlite3.Row | None:
        rows = self._source_all(sql, params)
        return rows[0] if rows else None

    def _source_all(self, sql: str, params: tuple[Any, ...] = ()) -> list[sqlite3.Row]:
        if not os.path.exists(self._config.source_db_path):
            return []
        try:
            uri = f"file:{os.path.abspath(self._config.source_db_path)}?mode=ro"
            with closing(sqlite3.connect(uri, uri=True, timeout=2.0)) as conn:
                conn.row_factory = sqlite3.Row
                return conn.execute(sql, params).fetchall()
        except sqlite3.Error:
            return []

    def _session_connect(self) -> sqlite3.Connection:
        conn = sqlite3.connect(self._config.session_db_path, timeout=3.0)
        conn.row_factory = sqlite3.Row
        return conn

    @staticmethod
    def _validate_session_id(value: str) -> str:
        value = str(value or "")
        if not SESSION_PATTERN.fullmatch(value):
            raise ValueError("invalid session_id")
        return value

    @staticmethod
    def _decode_json(value: Any) -> dict[str, Any]:
        try:
            result = json.loads(value or "{}")
        except (TypeError, json.JSONDecodeError):
            return {}
        return result if isinstance(result, dict) else {}

    @staticmethod
    def _age_seconds(timestamp: str) -> float | None:
        try:
            parsed = datetime.fromisoformat(timestamp.replace("Z", "+00:00"))
            if parsed.tzinfo is None:
                parsed = parsed.replace(tzinfo=timezone.utc)
            return round(max(0.0, time.time() - parsed.timestamp()), 1)
        except (AttributeError, TypeError, ValueError):
            return None

    @staticmethod
    def _safe_text(value: Any, fallback: str, max_length: int) -> str:
        if not isinstance(value, str) or not value.strip():
            return fallback
        return value.strip()[:max_length]

    @classmethod
    def _safe_string_list(cls, value: Any, fallback: list[str], limit: int) -> list[str]:
        if not isinstance(value, list):
            return fallback
        clean = [cls._safe_text(item, "", 120) for item in value if isinstance(item, str) and item.strip()]
        return clean[:limit] or fallback


def build_caretaker_service() -> CaretakerService:
    source_db = os.getenv("DB_PATH", "data/eldercare.db")
    session_db = os.getenv("CARETAKER_DB_PATH", "data/caretaker.db")
    enabled = os.getenv("LLM_ENABLED", "auto").lower() not in {"0", "false", "off", "disabled"}
    api_key = os.getenv("OPENAI_API_KEY", "").strip()
    model: ModelAdapter | None = None
    if enabled and api_key:
        model = OpenAICompatibleModel(
            api_key=api_key,
            base_url=os.getenv("LLM_BASE_URL", "https://api.openai.com/v1/chat/completions"),
            model=os.getenv("LLM_MODEL", "gpt-4o-mini"),
            timeout_seconds=float(os.getenv("LLM_TIMEOUT_SECONDS", "8")),
        )
    return CaretakerService(
        CaretakerConfig(source_db_path=source_db, session_db_path=session_db, node_id=os.getenv("NODE_ID", "node01")),
        model=model,
    )
