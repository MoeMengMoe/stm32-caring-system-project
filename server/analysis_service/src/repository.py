import os
import sqlite3
from datetime import datetime, timezone

from .notifier import NotificationDecision
from .rules_engine import AnalysisResult
from .schemas import EventPayload, StatusPayload


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
                """
            )

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

    def _connect(self) -> sqlite3.Connection:
        return sqlite3.connect(self._db_path)
