import json
import logging
import sys
import types
import unittest
from types import SimpleNamespace


try:
    import paho.mqtt.client  # noqa: F401
except ModuleNotFoundError:
    paho_module = types.ModuleType("paho")
    mqtt_package = types.ModuleType("paho.mqtt")
    mqtt_client_module = types.ModuleType("paho.mqtt.client")
    mqtt_client_module.Client = object
    mqtt_client_module.MQTTMessage = object
    mqtt_client_module.MQTT_ERR_SUCCESS = 0
    paho_module.mqtt = mqtt_package
    mqtt_package.client = mqtt_client_module
    sys.modules["paho"] = paho_module
    sys.modules["paho.mqtt"] = mqtt_package
    sys.modules["paho.mqtt.client"] = mqtt_client_module


from server.heartbeat_service.src.mqtt_service import HeartbeatService


class FakeClient:
    def __init__(self):
        self.messages = []

    def publish(self, topic, payload, qos, retain):
        self.messages.append((topic, json.loads(payload), qos, retain))
        return SimpleNamespace(rc=0)


class HeartbeatServiceTest(unittest.TestCase):
    def setUp(self):
        self.service = HeartbeatService.__new__(HeartbeatService)
        self.service._config = SimpleNamespace(
            mqtt_status_topic="eldercare/node01/status",
            mqtt_ingest_ack_topic="eldercare/node01/ingest_ack",
            node_id="node01",
        )
        self.service._logger = logging.getLogger("test-heartbeat")
        self.client = FakeClient()

    def test_status_message_publishes_existing_ack_contract(self):
        message = SimpleNamespace(
            topic="eldercare/node01/status",
            payload=b'{"node_id":"node01","seq":73,"risk":3}',
        )

        self.service._on_message(self.client, None, message)

        self.assertEqual(
            [
                (
                    "eldercare/node01/ingest_ack",
                    {"node_id": "node01", "seq": 73, "stored": True},
                    1,
                    False,
                )
            ],
            self.client.messages,
        )

    def test_invalid_status_is_not_acknowledged(self):
        message = SimpleNamespace(
            topic="eldercare/node01/status",
            payload=b'{"node_id":"node02","seq":73}',
        )

        self.service._on_message(self.client, None, message)

        self.assertEqual([], self.client.messages)


if __name__ == "__main__":
    unittest.main()
