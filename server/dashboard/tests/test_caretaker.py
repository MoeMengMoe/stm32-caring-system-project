import json
import sqlite3
import tempfile
import unittest
from contextlib import closing
from datetime import datetime, timezone
from pathlib import Path

from server.dashboard.src.caretaker import CaretakerConfig, CaretakerService


class FakeModel:
    def complete(self, context, recent_messages):
        return {"answer": "云端已结合家庭证据生成说明。", "suggested_actions": ["人工核对现场"], "unknowns": []}


class FailingModel:
    def complete(self, context, recent_messages):
        raise TimeoutError("simulated timeout")


class CaretakerServiceTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        root = Path(self.temp.name)
        self.source_db = root / "eldercare.db"
        self.session_db = root / "caretaker.db"
        self._create_source_db()

    def tearDown(self):
        self.temp.cleanup()

    def _create_source_db(self):
        now = datetime.now(timezone.utc).isoformat()
        with closing(sqlite3.connect(self.source_db)) as conn:
            conn.executescript(
                """
                CREATE TABLE raw_status (id INTEGER PRIMARY KEY, received_at TEXT, gas INTEGER,
                    temperature REAL, humidity REAL, risk INTEGER, payload_json TEXT);
                CREATE TABLE event_logs (id INTEGER PRIMARY KEY, received_at TEXT, risk INTEGER,
                    is_backfilled INTEGER, payload_json TEXT);
                CREATE TABLE relay_states (id INTEGER PRIMARY KEY, received_at TEXT, relay_id INTEGER,
                    state TEXT, payload_json TEXT);
                CREATE TABLE notification_logs (id INTEGER PRIMARY KEY, created_at TEXT,
                    notice_type TEXT, decision TEXT);
                """
            )
            for seq, gas in enumerate((260, 190, 110), start=1):
                payload = {
                    "node_id": "node01", "seq": seq, "temperature": 25.2,
                    "humidity": 61.0, "gas": gas, "presence": 1,
                    "risk": 0 if seq == 3 else 2, "event": "normal",
                }
                conn.execute(
                    "INSERT INTO raw_status(received_at,gas,temperature,humidity,risk,payload_json) VALUES(?,?,?,?,?,?)",
                    (now, gas, 25.2, 61.0, payload["risk"], json.dumps(payload)),
                )
            event = {
                "event_type": "GAS_RISK", "scenario": "GAS_RISK", "state_before": "ALARM",
                "state_after": "CLEARED", "risk": 0, "result": "CLEARED",
                "trigger_source": "LOCAL",
            }
            conn.execute(
                "INSERT INTO event_logs(received_at,risk,is_backfilled,payload_json) VALUES(?,?,?,?)",
                (now, 0, 0, json.dumps(event)),
            )
            conn.execute(
                "INSERT INTO relay_states(received_at,relay_id,state,payload_json) VALUES(?,?,?,?)",
                (now, 1, "OFF", "{}"),
            )
            conn.execute(
                "INSERT INTO notification_logs(created_at,notice_type,decision) VALUES(?,?,?)",
                (now, "family", "sent"),
            )
            conn.commit()

    def service(self, model=None):
        return CaretakerService(
            CaretakerConfig(str(self.source_db), str(self.session_db), stale_after_seconds=8), model=model
        )

    def test_gas_question_uses_real_trend_and_persists_evidence(self):
        service = self.service()
        result = service.chat("display-main", "燃气风险恢复了吗？")

        self.assertEqual("本地规则兜底", result["mode"])
        self.assertIn("燃气最新值 110", result["answer"])
        self.assertIn("回落", result["answer"])
        self.assertIn("get_sensor_trend", result["tools_used"])
        self.assertTrue(any(item["source"] == "传感器趋势" for item in result["evidence"]))
        self.assertEqual(2, len(service.history("display-main")["messages"]))

    def test_cloud_model_only_changes_wording_not_grounded_risk_or_evidence(self):
        now = datetime.now(timezone.utc).isoformat()
        with closing(sqlite3.connect(self.source_db)) as conn:
            payload = {"seq": 9, "gas": 400, "temperature": 25, "humidity": 60, "presence": 1, "risk": 3}
            conn.execute(
                "INSERT INTO raw_status(received_at,gas,temperature,humidity,risk,payload_json) VALUES(?,?,?,?,?,?)",
                (now, 400, 25, 60, 3, json.dumps(payload)),
            )
            conn.commit()

        result = self.service(FakeModel()).chat("display-main", "家里现在安全吗？")

        self.assertTrue(result["model_used"])
        self.assertEqual(3, result["risk"])
        self.assertEqual("alarm", result["care_state"])
        self.assertEqual("云端已结合家庭证据生成说明。", result["answer"])
        self.assertTrue(result["evidence"])

    def test_model_failure_returns_deterministic_answer(self):
        result = self.service(FailingModel()).chat("display-main", "家里现在安全吗？")

        self.assertFalse(result["model_used"])
        self.assertEqual("本地规则兜底", result["mode"])
        self.assertIn("simulated timeout", result["model_error"])
        self.assertIn("当前综合状态", result["answer"])

    def test_rejects_invalid_session_and_oversized_message(self):
        service = self.service()
        with self.assertRaises(ValueError):
            service.chat("../../bad", "状态")
        with self.assertRaises(ValueError):
            service.chat("display-main", "x" * 501)


if __name__ == "__main__":
    unittest.main()
