import json
import os
import sys
import tempfile
import types
import unittest
from pathlib import Path

from server.analysis_service.src.cloud_projection import project_status_transition
from server.analysis_service.src.notifier import NotificationDecision, NotificationDelivery
from server.analysis_service.src.repository import Repository
from server.analysis_service.src.rules_engine import analyze_status
from server.analysis_service.src.schemas import AvailabilityPayload, RelayStatePayload, parse_status_payload


def install_paho_stub_if_needed():
    try:
        import paho.mqtt.client  # noqa: F401
        return
    except ModuleNotFoundError:
        pass
    client_module = types.ModuleType("paho.mqtt.client")
    client_module.CallbackAPIVersion = types.SimpleNamespace(VERSION2=2)
    client_module.MQTT_ERR_SUCCESS = 0
    client_module.Client = object
    mqtt_package = types.ModuleType("paho.mqtt")
    mqtt_package.client = client_module
    paho_package = types.ModuleType("paho")
    paho_package.mqtt = mqtt_package
    sys.modules.update({"paho": paho_package, "paho.mqtt": mqtt_package, "paho.mqtt.client": client_module})


class CloudStateApiTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.root = Path(cls.temp.name)
        os.environ["CARETAKER_DB_PATH"] = str(cls.root / "caretaker.db")
        os.environ["LLM_ENABLED"] = "false"
        install_paho_stub_if_needed()
        from server.dashboard.src import main
        cls.main = main

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def setUp(self):
        self.db_path = self.root / f"api-{self._testMethodName}.db"
        self.main.DB_PATH = str(self.db_path)
        repository = Repository(str(self.db_path))
        repository.initialize()
        low = parse_status_payload(
            json.dumps({
                "node_id": "node01", "seq": 1, "temperature": 25.0, "humidity": 60.0,
                "gas": 80, "presence": 1, "risk": 0, "event": "NORMAL",
                "relay_state_mask": 1, "cloud_perm_mask": 15,
            }).encode()
        )
        high = parse_status_payload(
            json.dumps({
                "node_id": "node01", "seq": 2, "temperature": 25.0, "humidity": 60.0,
                "gas": 180, "presence": 1, "risk": 0, "event": "GAS_RISK",
                "relay_state_mask": 1, "cloud_perm_mask": 15,
            }).encode()
        )
        repository.insert_raw_status(high)
        repository.insert_analysis_result(analyze_status(high))
        for event in project_status_transition(low, high, analyze_status(high)):
            repository.insert_derived_event(event)
        repository.insert_availability(AvailabilityPayload("node01", "online", "online"))
        repository.insert_relay_state(RelayStatePayload("node01", 1, "ON", 9, '{"state":"ON"}'))
        decision_id = repository.insert_notification_decision(NotificationDecision("node01", "family", "suggest_notice", "{}"))
        repository.insert_notification_delivery(decision_id, NotificationDelivery("pushplus", "family", True, 200, "success"))
        repository.upsert_alarm_state(
            '{"node_id":"node01","active":false,"risk":0,"state":"CLEARED","message":"已恢复"}',
            "STATUS_RECOVERY",
            True,
        )

    def test_cloud_apis_return_consolidated_evidence(self):
        self.assertFalse(self.main.current_alarm()["active"])
        self.assertEqual("online", self.main.device_availability()["state"])
        self.assertTrue(self.main.system_health()["device"]["online"])
        self.assertEqual("ON", self.main.latest_relay_states()["relays"][0]["state"])
        self.assertTrue(self.main.recent_notifications()["notifications"][0]["delivery"]["sent"])
        event = self.main.recent_events()["events"][0]
        self.assertTrue(event["is_derived"])
        self.assertEqual("CLOUD_DERIVED", event["origin"])


if __name__ == "__main__":
    unittest.main()
