import json
import os
import sqlite3
from contextlib import contextmanager
from datetime import datetime, timezone
from typing import Iterator

from .cloud_projection import DerivedEvent
from .notifier import NotificationDecision, NotificationDelivery
from .rules_engine import AnalysisResult
from .schemas import AvailabilityPayload, EventPayload, RelayResultPayload, RelayStatePayload, StatusPayload, parse_status_payload


class Repository:
    def __init__(self, db_path: str) -> None:
        self._db_path = db_path

    def initialize(self) -> None:
        parent = os.path.dirname(self._db_path)
        if parent:
            os.makedirs(parent, exist_ok=True)

        with self._connect() as conn:
            conn.executescript(
                """
                CREATE TABLE IF NOT EXISTS raw_status (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    received_at TEXT NOT NULL,
                    node_id TEXT NOT NULL,
                    seq INTEGER NOT NULL,
                    temperature REAL NOT NULL,
                    humidity REAL NOT NULL,
                    gas INTEGER NOT NULL,
                    presence INTEGER NOT NULL,
                    risk INTEGER NOT NULL,
                    event TEXT NOT NULL,
                    relay_state_mask INTEGER NOT NULL,
                    cloud_perm_mask INTEGER NOT NULL,
                    payload_json TEXT NOT NULL
                );

                CREATE INDEX IF NOT EXISTS idx_raw_status_node_seq
                    ON raw_status(node_id, seq);

                CREATE TABLE IF NOT EXISTS analysis_results (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    created_at TEXT NOT NULL,
                    node_id TEXT NOT NULL,
                    source_seq INTEGER,
                    source_risk INTEGER NOT NULL,
                    cloud_risk INTEGER NOT NULL,
                    risk_score INTEGER NOT NULL,
                    summary TEXT NOT NULL,
                    model_used INTEGER NOT NULL,
                    need_family_notice INTEGER NOT NULL,
                    need_community_notice INTEGER NOT NULL,
                    need_hospital_notice INTEGER NOT NULL,
                    payload_json TEXT NOT NULL
                );

                CREATE TABLE IF NOT EXISTS relay_states (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    received_at TEXT NOT NULL,
                    node_id TEXT NOT NULL,
                    relay_id INTEGER NOT NULL,
                    state TEXT NOT NULL,
                    payload_json TEXT NOT NULL
                );

                CREATE TABLE IF NOT EXISTS event_logs (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    received_at TEXT NOT NULL,
                    node_id TEXT NOT NULL,
                    event_id INTEGER NOT NULL,
                    scenario TEXT NOT NULL,
                    event_type TEXT NOT NULL,
                    trigger_source TEXT NOT NULL,
                    state_before TEXT NOT NULL,
                    state_after TEXT NOT NULL,
                    risk INTEGER NOT NULL,
                    result TEXT NOT NULL,
                    network_state TEXT NOT NULL,
                    power_state TEXT NOT NULL,
                    flags INTEGER NOT NULL,
                    timestamp_ms INTEGER NOT NULL,
                    is_backfilled INTEGER NOT NULL,
                    payload_json TEXT NOT NULL
                );

                CREATE INDEX IF NOT EXISTS idx_event_logs_node_event
                    ON event_logs(node_id, event_id);

                CREATE INDEX IF NOT EXISTS idx_event_logs_received_at
                    ON event_logs(received_at);

                CREATE TABLE IF NOT EXISTS notification_logs (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    created_at TEXT NOT NULL,
                    node_id TEXT NOT NULL,
                    notice_type TEXT NOT NULL,
                    decision TEXT NOT NULL,
                    payload_json TEXT NOT NULL
                );

                CREATE TABLE IF NOT EXISTS notification_state (
                    channel TEXT NOT NULL,
                    node_id TEXT NOT NULL,
                    notice_type TEXT NOT NULL,
                    state_key TEXT NOT NULL,
                    active INTEGER NOT NULL,
                    updated_at TEXT NOT NULL,
                    PRIMARY KEY (channel, node_id, notice_type)
                );

                CREATE TABLE IF NOT EXISTS notification_deliveries (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    decision_id INTEGER NOT NULL,
                    created_at TEXT NOT NULL,
                    provider TEXT NOT NULL,
                    notice_type TEXT NOT NULL,
                    sent INTEGER NOT NULL,
                    status_code INTEGER,
                    message TEXT NOT NULL,
                    FOREIGN KEY(decision_id) REFERENCES notification_logs(id)
                );

                CREATE TABLE IF NOT EXISTS relay_results (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    received_at TEXT NOT NULL,
                    node_id TEXT NOT NULL,
                    relay_id INTEGER NOT NULL,
                    request_id INTEGER,
                    result TEXT NOT NULL,
                    state TEXT NOT NULL,
                    reason TEXT NOT NULL,
                    payload_json TEXT NOT NULL
                );

                CREATE TABLE IF NOT EXISTS device_availability (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    received_at TEXT NOT NULL,
                    node_id TEXT NOT NULL,
                    state TEXT NOT NULL,
                    payload_json TEXT NOT NULL
                );

                CREATE INDEX IF NOT EXISTS idx_device_availability_node
                    ON device_availability(node_id, id);

                CREATE TABLE IF NOT EXISTS derived_events (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    received_at TEXT NOT NULL,
                    node_id TEXT NOT NULL,
                    event_type TEXT NOT NULL,
                    scenario TEXT NOT NULL,
                    state_before TEXT NOT NULL,
                    state_after TEXT NOT NULL,
                    risk INTEGER NOT NULL,
                    result TEXT NOT NULL,
                    detail TEXT NOT NULL,
                    payload_json TEXT NOT NULL
                );

                CREATE TABLE IF NOT EXISTS alarm_state (
                    node_id TEXT PRIMARY KEY,
                    updated_at TEXT NOT NULL,
                    active INTEGER NOT NULL,
                    source TEXT NOT NULL,
                    risk INTEGER NOT NULL,
                    state TEXT NOT NULL,
                    message TEXT NOT NULL,
                    payload_json TEXT NOT NULL,
                    mqtt_publish_ok INTEGER
                );
                """
            )

    def latest_status(self, node_id: str) -> StatusPayload | None:
        with self._connect() as conn:
            row = conn.execute(
                "SELECT payload_json FROM raw_status WHERE node_id = ? ORDER BY id DESC LIMIT 1",
                (node_id,),
            ).fetchone()
        if row is None:
            return None
        try:
            return parse_status_payload(str(row[0]).encode("utf-8"))
        except (ValueError, TypeError):
            return None

    def insert_raw_status(self, status: StatusPayload) -> int:
        received_at = datetime.now(timezone.utc).isoformat()
        with self._connect() as conn:
            cursor = conn.execute(
                """
                INSERT INTO raw_status (
                    received_at,
                    node_id,
                    seq,
                    temperature,
                    humidity,
                    gas,
                    presence,
                    risk,
                    event,
                    relay_state_mask,
                    cloud_perm_mask,
                    payload_json
                )
                VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
                """,
                (
                    received_at,
                    status.node_id,
                    status.seq,
                    status.temperature,
                    status.humidity,
                    status.gas,
                    status.presence,
                    status.risk,
                    status.event,
                    status.relay_state_mask,
                    status.cloud_perm_mask,
                    status.raw_json,
                ),
            )
            return int(cursor.lastrowid)

    def insert_event_log(self, event: EventPayload) -> int:
        received_at = datetime.now(timezone.utc).isoformat()
        is_backfilled = 1 if (event.flags & 0x02) != 0 or event.result == "BACKFILLED" else 0
        with self._connect() as conn:
            cursor = conn.execute(
                """
                INSERT INTO event_logs (
                    received_at,
                    node_id,
                    event_id,
                    scenario,
                    event_type,
                    trigger_source,
                    state_before,
                    state_after,
                    risk,
                    result,
                    network_state,
                    power_state,
                    flags,
                    timestamp_ms,
                    is_backfilled,
                    payload_json
                )
                VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
                """,
                (
                    received_at,
                    event.node_id,
                    event.event_id,
                    event.scenario,
                    event.event_type,
                    event.trigger_source,
                    event.state_before,
                    event.state_after,
                    event.risk,
                    event.result,
                    event.network_state,
                    event.power_state,
                    event.flags,
                    event.timestamp_ms,
                    is_backfilled,
                    event.raw_json,
                ),
            )
            return int(cursor.lastrowid)

    def insert_analysis_result(self, result: AnalysisResult) -> int:
        created_at = datetime.now(timezone.utc).isoformat()
        payload_json = result.to_json()
        with self._connect() as conn:
            cursor = conn.execute(
                """
                INSERT INTO analysis_results (
                    created_at,
                    node_id,
                    source_seq,
                    source_risk,
                    cloud_risk,
                    risk_score,
                    summary,
                    model_used,
                    need_family_notice,
                    need_community_notice,
                    need_hospital_notice,
                    payload_json
                )
                VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
                """,
                (
                    created_at,
                    result.node_id,
                    result.source_seq,
                    result.source_risk,
                    result.cloud_risk,
                    result.risk_score,
                    result.summary,
                    int(result.model_used),
                    int(result.need_family_notice),
                    int(result.need_community_notice),
                    int(result.need_hospital_notice),
                    payload_json,
                ),
            )
            return int(cursor.lastrowid)

    def has_clear_event_after_raw_status(self, raw_status_id: int) -> bool:
        with self._connect() as conn:
            status_row = conn.execute(
                "SELECT received_at FROM raw_status WHERE id = ?",
                (raw_status_id,),
            ).fetchone()
            if status_row is None:
                return False

            clear_row = conn.execute(
                """
                SELECT 1
                FROM event_logs
                WHERE received_at >= ?
                  AND (
                    event_type = 'CLEAR_ALARM'
                    OR state_after = 'CLEARED'
                    OR result IN ('ACKNOWLEDGED', 'CLEARED')
                  )
                ORDER BY received_at DESC
                LIMIT 1
                """,
                (status_row[0],),
            ).fetchone()
            return clear_row is not None

    def insert_notification_decision(self, decision: NotificationDecision) -> int:
        created_at = datetime.now(timezone.utc).isoformat()
        with self._connect() as conn:
            cursor = conn.execute(
                """
                INSERT INTO notification_logs (
                    created_at,
                    node_id,
                    notice_type,
                    decision,
                    payload_json
                )
                VALUES (?, ?, ?, ?, ?)
                """,
                (
                    created_at,
                    decision.node_id,
                    decision.notice_type,
                    decision.decision,
                    decision.payload_json,
                ),
            )
            return int(cursor.lastrowid)

    def insert_notification_delivery(self, decision_id: int, delivery: NotificationDelivery) -> int:
        created_at = datetime.now(timezone.utc).isoformat()
        with self._connect() as conn:
            cursor = conn.execute(
                """
                INSERT INTO notification_deliveries (
                    decision_id, created_at, provider, notice_type, sent, status_code, message
                ) VALUES (?, ?, ?, ?, ?, ?, ?)
                """,
                (
                    decision_id,
                    created_at,
                    delivery.provider,
                    delivery.notice_type,
                    int(delivery.sent),
                    delivery.status_code,
                    delivery.message,
                ),
            )
            return int(cursor.lastrowid)

    def insert_relay_state(self, payload: RelayStatePayload) -> int:
        received_at = datetime.now(timezone.utc).isoformat()
        with self._connect() as conn:
            cursor = conn.execute(
                "INSERT INTO relay_states(received_at,node_id,relay_id,state,payload_json) VALUES(?,?,?,?,?)",
                (received_at, payload.node_id, payload.relay_id, payload.state, payload.raw_json),
            )
            return int(cursor.lastrowid)

    def insert_relay_result(self, payload: RelayResultPayload) -> int:
        received_at = datetime.now(timezone.utc).isoformat()
        with self._connect() as conn:
            cursor = conn.execute(
                """INSERT INTO relay_results(
                    received_at,node_id,relay_id,request_id,result,state,reason,payload_json
                ) VALUES(?,?,?,?,?,?,?,?)""",
                (
                    received_at, payload.node_id, payload.relay_id, payload.request_id,
                    payload.result, payload.state, payload.reason, payload.raw_json,
                ),
            )
            return int(cursor.lastrowid)

    def insert_availability(self, payload: AvailabilityPayload) -> int:
        received_at = datetime.now(timezone.utc).isoformat()
        raw_json = json.dumps(
            {"node_id": payload.node_id, "state": payload.state, "raw": payload.raw_payload},
            ensure_ascii=False,
            separators=(",", ":"),
        )
        with self._connect() as conn:
            cursor = conn.execute(
                "INSERT INTO device_availability(received_at,node_id,state,payload_json) VALUES(?,?,?,?)",
                (received_at, payload.node_id, payload.state, raw_json),
            )
            return int(cursor.lastrowid)

    def insert_derived_event(self, event: DerivedEvent) -> int:
        received_at = datetime.now(timezone.utc).isoformat()
        payload = {
            "node_id": event.node_id,
            "event_type": event.event_type,
            "scenario": event.scenario,
            "trigger_source": "CLOUD",
            "state_before": event.state_before,
            "state_after": event.state_after,
            "risk": event.risk,
            "result": event.result,
            "detail": event.detail,
            "origin": "CLOUD_DERIVED",
            "is_derived": True,
        }
        raw_json = json.dumps(payload, ensure_ascii=False, separators=(",", ":"))
        with self._connect() as conn:
            cursor = conn.execute(
                """INSERT INTO derived_events(
                    received_at,node_id,event_type,scenario,state_before,state_after,risk,result,detail,payload_json
                ) VALUES(?,?,?,?,?,?,?,?,?,?)""",
                (
                    received_at, event.node_id, event.event_type, event.scenario,
                    event.state_before, event.state_after, event.risk, event.result,
                    event.detail, raw_json,
                ),
            )
            return int(cursor.lastrowid)

    def upsert_alarm_state(self, payload_json: str, source: str, publish_ok: bool | None) -> None:
        payload = json.loads(payload_json)
        updated_at = datetime.now(timezone.utc).isoformat()
        with self._connect() as conn:
            conn.execute(
                """INSERT INTO alarm_state(
                    node_id,updated_at,active,source,risk,state,message,payload_json,mqtt_publish_ok
                ) VALUES(?,?,?,?,?,?,?,?,?)
                ON CONFLICT(node_id) DO UPDATE SET
                    updated_at=excluded.updated_at, active=excluded.active, source=excluded.source,
                    risk=excluded.risk, state=excluded.state, message=excluded.message,
                    payload_json=excluded.payload_json, mqtt_publish_ok=excluded.mqtt_publish_ok""",
                (
                    payload["node_id"], updated_at, int(bool(payload.get("active"))), source,
                    int(payload.get("risk", 0)), str(payload.get("state", "")),
                    str(payload.get("message", "")), payload_json,
                    None if publish_ok is None else int(publish_ok),
                ),
            )

    def get_alarm_state(self, node_id: str) -> dict[str, object] | None:
        with self._connect() as conn:
            row = conn.execute(
                "SELECT active, source, risk, state, message, payload_json FROM alarm_state WHERE node_id=?",
                (node_id,),
            ).fetchone()
        if row is None:
            return None
        return {
            "active": bool(row[0]),
            "source": row[1],
            "risk": row[2],
            "state": row[3],
            "message": row[4],
            "payload_json": row[5],
        }

    def claim_notification_state(
        self,
        channel: str,
        node_id: str,
        notice_type: str,
        state_key: str,
    ) -> bool:
        updated_at = datetime.now(timezone.utc).isoformat()
        with self._connect() as conn:
            cursor = conn.execute(
                """
                INSERT INTO notification_state (
                    channel,
                    node_id,
                    notice_type,
                    state_key,
                    active,
                    updated_at
                )
                VALUES (?, ?, ?, ?, 1, ?)
                ON CONFLICT(channel, node_id, notice_type) DO UPDATE SET
                    state_key = excluded.state_key,
                    active = 1,
                    updated_at = excluded.updated_at
                WHERE notification_state.active = 0
                   OR notification_state.state_key <> excluded.state_key
                """,
                (channel, node_id, notice_type, state_key, updated_at),
            )
            return cursor.rowcount > 0

    def clear_notification_state(self, channel: str, node_id: str, notice_type: str) -> bool:
        updated_at = datetime.now(timezone.utc).isoformat()
        with self._connect() as conn:
            cursor = conn.execute(
                """
                UPDATE notification_state
                SET active = 0,
                    updated_at = ?
                WHERE channel = ?
                  AND node_id = ?
                  AND notice_type = ?
                  AND active <> 0
                """,
                (updated_at, channel, node_id, notice_type),
            )
            return cursor.rowcount > 0

    @contextmanager
    def _connect(self) -> Iterator[sqlite3.Connection]:
        conn = sqlite3.connect(self._db_path)
        try:
            yield conn
            conn.commit()
        finally:
            conn.close()
