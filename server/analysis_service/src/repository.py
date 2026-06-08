import os
import sqlite3
from datetime import datetime, timezone

from .notifier import NotificationDecision
from .rules_engine import AnalysisResult
from .schemas import StatusPayload


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

                CREATE TABLE IF NOT EXISTS notification_logs (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    created_at TEXT NOT NULL,
                    node_id TEXT NOT NULL,
                    notice_type TEXT NOT NULL,
                    decision TEXT NOT NULL,
                    payload_json TEXT NOT NULL
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

    def _connect(self) -> sqlite3.Connection:
        return sqlite3.connect(self._db_path)
