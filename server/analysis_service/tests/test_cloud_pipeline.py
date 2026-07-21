import json
import sqlite3
import sys
import tempfile
import types
import unittest
from contextlib import closing
from pathlib import Path
from types import SimpleNamespace

from server.analysis_service.src.cloud_projection import project_status_transition
from server.analysis_service.src.notifier import NotificationDecision, NotificationDelivery

if "paho.mqtt.client" not in sys.modules:
    paho_module = types.ModuleType("paho")
    mqtt_package = types.ModuleType("paho.mqtt")
    mqtt_client_module = types.ModuleType("paho.mqtt.client")
    mqtt_client_module.Client = object
    mqtt_client_module.MQTTMessage = object
    mqtt_client_module.MQTT_ERR_SUCCESS = 0
    mqtt_client_module.CallbackAPIVersion = SimpleNamespace(VERSION2=2)
    mqtt_client_module.topic_matches_sub = lambda _filter, _topic: False
    sys.modules["paho"] = paho_module
    sys.modules["paho.mqtt"] = mqtt_package
    sys.modules["paho.mqtt.client"] = mqtt_client_module

mqtt_client_module = sys.modules["paho.mqtt.client"]
if not hasattr(mqtt_client_module, "MQTTMessage"):
    mqtt_client_module.MQTTMessage = object
if not hasattr(mqtt_client_module, "topic_matches_sub"):
    mqtt_client_module.topic_matches_sub = lambda _filter, _topic: False

from server.analysis_service.src.mqtt_client import MqttStatusIngestor
from server.analysis_service.src.repository import Repository
from server.analysis_service.src.rules_engine import analyze_status
from server.analysis_service.src.schemas import (
    AvailabilityPayload,
    RelayResultPayload,
    RelayStatePayload,
    parse_relay_result_payload,
    parse_relay_state_payload,
    parse_event_payload,
    parse_status_payload,
)


def status(seq: int, *, gas: int = 80, risk: int = 0):
    return parse_status_payload(
        json.dumps(
            {
                "node_id": "node01",
                "seq": seq,
                "temperature": 25.0,
                "humidity": 60.0,
                "gas": gas,
                "presence": 1,
                "risk": risk,
                "event": "NORMAL",
                "relay_state_mask": 0,
                "cloud_perm_mask": 15,
            }
        ).encode()
    )


class CloudPipelineTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.db_path = Path(self.temp.name) / "eldercare.db"
        self.repository = Repository(str(self.db_path))
        self.repository.initialize()

    def tearDown(self):
        self.temp.cleanup()

    def test_cloud_events_are_transition_only_and_labeled(self):
        normal = status(1, gas=80)
        high = status(2, gas=180)
        recovered = status(3, gas=90)

        entered = project_status_transition(normal, high, analyze_status(high))
        cleared = project_status_transition(high, recovered, analyze_status(recovered))

        self.assertEqual(["CLOUD_GAS_RISK_ENTER"], [item.event_type for item in entered])
        self.assertEqual(["CLOUD_GAS_RECOVERED"], [item.event_type for item in cleared])
        event_id = self.repository.insert_derived_event(entered[0])
        with closing(sqlite3.connect(self.db_path)) as conn:
            payload = json.loads(conn.execute("SELECT payload_json FROM derived_events WHERE id=?", (event_id,)).fetchone()[0])
        self.assertTrue(payload["is_derived"])
        self.assertEqual("CLOUD_DERIVED", payload["origin"])

    def test_relay_availability_delivery_and_alarm_are_persisted(self):
        relay_state = parse_relay_state_payload(
            b'{"node_id":"node01","relay_id":1,"state":"ON","request_id":7}'
        )
        relay_result = parse_relay_result_payload(
            b'{"node_id":"node01","relay_id":1,"request_id":7,"result":"APPLIED","state":"ON","reason":""}'
        )
        self.repository.insert_relay_state(relay_state)
        self.repository.insert_relay_result(relay_result)
        self.repository.insert_availability(AvailabilityPayload("node01", "online", "online"))

        decision_id = self.repository.insert_notification_decision(
            NotificationDecision("node01", "family", "suggest_notice", "{}")
        )
        self.repository.insert_notification_delivery(
            decision_id, NotificationDelivery("pushplus", "family", True, 200, "success")
        )
        alarm = '{"node_id":"node01","active":false,"risk":0,"state":"CLEARED","message":"已恢复"}'
        self.repository.upsert_alarm_state(alarm, "STATUS_RECOVERY", True)

        with closing(sqlite3.connect(self.db_path)) as conn:
            self.assertEqual("ON", conn.execute("SELECT state FROM relay_states").fetchone()[0])
            self.assertEqual("APPLIED", conn.execute("SELECT result FROM relay_results").fetchone()[0])
            self.assertEqual("online", conn.execute("SELECT state FROM device_availability").fetchone()[0])
            self.assertEqual((1, 200), conn.execute("SELECT sent,status_code FROM notification_deliveries").fetchone())
            self.assertEqual((0, "STATUS_RECOVERY"), conn.execute("SELECT active,source FROM alarm_state").fetchone())

    def test_latest_status_returns_previous_sample(self):
        first = status(1, gas=80)
        self.assertIsNone(self.repository.latest_status("node01"))
        self.repository.insert_raw_status(first)
        self.assertEqual(1, self.repository.latest_status("node01").seq)

    def test_extended_firmware_event_enums_are_accepted(self):
        event = parse_event_payload(
            json.dumps({
                "node_id": "node01", "event_id": 17, "scenario": "GAS_RISK",
                "event_type": "EDGE_AI_RISK", "trigger_source": "AI",
                "state_before": "NORMAL", "state_after": "ACK_WAIT", "risk": 2,
                "result": "WAITING_ACK", "network_state": "ONLINE",
                "power_state": "NORMAL", "flags": 0, "timestamp_ms": 1234,
            }).encode()
        )
        self.assertEqual("EDGE_AI_RISK", event.event_type)
        self.assertEqual("AI", event.trigger_source)

    def test_status_recovery_publishes_retained_alarm_clear(self):
        class FakeClient:
            def __init__(self):
                self.messages = []

            def publish(self, topic, payload, qos, retain):
                self.messages.append((topic, json.loads(payload), qos, retain))
                return SimpleNamespace(rc=0)

        class RulesOnlyModel:
            @staticmethod
            def maybe_refine(_status, rules):
                return rules

        class DisabledNotifier:
            @staticmethod
            def send(decision):
                return NotificationDelivery("pushplus", decision.notice_type, False, None, "disabled")

        ingestor = MqttStatusIngestor.__new__(MqttStatusIngestor)
        ingestor._repository = self.repository
        ingestor._client = FakeClient()
        ingestor._llm_service = RulesOnlyModel()
        ingestor._pushplus_notifier = DisabledNotifier()
        ingestor._config = SimpleNamespace(
            mqtt_analysis_topic="eldercare/node01/analysis",
            mqtt_alarm_topic="eldercare/node01/alarm",
            mqtt_ingest_ack_topic="eldercare/node01/ingest_ack",
        )
        import logging
        ingestor._logger = logging.getLogger("test-cloud-pipeline")

        ingestor._handle_status_message(status(1, gas=180, risk=3).raw_json.encode())
        ingestor._handle_status_message(status(2, gas=80, risk=0).raw_json.encode())

        alarm_messages = [message for message in ingestor._client.messages if message[0].endswith("/alarm")]
        ingest_acks = [message for message in ingestor._client.messages if message[0].endswith("/ingest_ack")]
        self.assertEqual([1, 2], [message[1]["seq"] for message in ingest_acks])
        self.assertTrue(all(message[1]["stored"] for message in ingest_acks))
        self.assertTrue(all(message[3] is False for message in ingest_acks))
        self.assertTrue(alarm_messages[0][1]["active"])
        self.assertFalse(alarm_messages[-1][1]["active"])
        self.assertEqual("CLEARED", alarm_messages[-1][1]["state"])
        self.assertTrue(alarm_messages[-1][3])
        with closing(sqlite3.connect(self.db_path)) as conn:
            self.assertEqual(0, conn.execute("SELECT active FROM alarm_state WHERE node_id='node01'").fetchone()[0])
            self.assertGreater(conn.execute("SELECT COUNT(*) FROM notification_deliveries").fetchone()[0], 0)

    def test_status_commit_publishes_ingest_ack_before_analysis(self):
        class FakeClient:
            def __init__(self):
                self.messages = []

            def publish(self, topic, payload, qos, retain):
                self.messages.append((topic, json.loads(payload), qos, retain))
                return SimpleNamespace(rc=0)

        class FailingModel:
            @staticmethod
            def maybe_refine(_status, _rules):
                raise RuntimeError("downstream analysis failed")

        ingestor = MqttStatusIngestor.__new__(MqttStatusIngestor)
        ingestor._repository = self.repository
        ingestor._client = FakeClient()
        ingestor._llm_service = FailingModel()
        ingestor._config = SimpleNamespace(
            mqtt_ingest_ack_topic="eldercare/node01/ingest_ack",
        )
        import logging
        ingestor._logger = logging.getLogger("test-ingest-ack")

        with self.assertRaisesRegex(RuntimeError, "downstream analysis failed"):
            ingestor._handle_status_message(status(41).raw_json.encode())

        self.assertEqual(41, self.repository.latest_status("node01").seq)
        self.assertEqual(
            [
                (
                    "eldercare/node01/ingest_ack",
                    {"node_id": "node01", "seq": 41, "stored": True},
                    1,
                    False,
                )
            ],
            ingestor._client.messages,
        )


if __name__ == "__main__":
    unittest.main()
