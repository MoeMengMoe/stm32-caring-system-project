import json
import os
import sys
import tempfile
import threading
import types
import unittest
import urllib.error
import urllib.request
from http.server import ThreadingHTTPServer
from pathlib import Path


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
    sys.modules.update(
        {"paho": paho_package, "paho.mqtt": mqtt_package, "paho.mqtt.client": client_module}
    )


class CaretakerHttpTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        root = Path(cls.temp.name)
        os.environ["DB_PATH"] = str(root / "missing-source.db")
        os.environ["CARETAKER_DB_PATH"] = str(root / "sessions.db")
        os.environ["LLM_ENABLED"] = "false"
        install_paho_stub_if_needed()
        from server.dashboard.src import main

        cls.server = ThreadingHTTPServer(("127.0.0.1", 0), main.DashboardHandler)
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()
        cls.base_url = f"http://127.0.0.1:{cls.server.server_port}"

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()
        cls.thread.join(timeout=2)
        cls.temp.cleanup()

    def get_json(self, path):
        with urllib.request.urlopen(self.base_url + path, timeout=3) as response:
            return response.status, json.loads(response.read().decode("utf-8"))

    def post_json(self, path, data):
        request = urllib.request.Request(
            self.base_url + path,
            data=json.dumps(data).encode("utf-8"),
            headers={"Content-Type": "application/json"},
            method="POST",
        )
        try:
            with urllib.request.urlopen(request, timeout=3) as response:
                return response.status, json.loads(response.read().decode("utf-8"))
        except urllib.error.HTTPError as exc:
            return exc.code, json.loads(exc.read().decode("utf-8"))

    def test_suggestions_and_empty_session(self):
        status, suggestions = self.get_json("/api/caretaker/suggestions")
        self.assertEqual(200, status)
        self.assertEqual(4, len(suggestions["suggestions"]))

        status, session = self.get_json("/api/caretaker/session?session_id=empty-test")
        self.assertEqual(200, status)
        self.assertEqual([], session["messages"])

    def test_chat_has_structured_fallback_and_is_persisted(self):
        status, result = self.post_json(
            "/api/caretaker/chat", {"session_id": "http-test", "message": "家里现在安全吗？"}
        )
        self.assertEqual(200, status)
        self.assertEqual("本地规则兜底", result["mode"])
        self.assertIn("tools_used", result)
        self.assertIn("evidence", result)

        _, session = self.get_json("/api/caretaker/session?session_id=http-test")
        self.assertEqual(2, len(session["messages"]))

    def test_chat_validates_input(self):
        status, result = self.post_json(
            "/api/caretaker/chat", {"session_id": "bad/session", "message": "状态"}
        )
        self.assertEqual(400, status)
        self.assertIn("error", result)


if __name__ == "__main__":
    unittest.main()
