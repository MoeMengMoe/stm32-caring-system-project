import logging

import paho.mqtt.client as mqtt

from .config import Config
from .llm_service import LlmService
from .notifier import build_notification_decisions
from .repository import Repository
from .rules_engine import analyze_status
from .schemas import PayloadValidationError, parse_status_payload


class MqttStatusIngestor:
    def __init__(self, config: Config, repository: Repository) -> None:
        self._config = config
        self._repository = repository
        self._logger = logging.getLogger(__name__)
        self._llm_service = LlmService(config)
        self._client = self._build_client()

    def run_forever(self) -> None:
        self._logger.info(
            "connecting to MQTT broker %s:%s",
            self._config.mqtt_host,
            self._config.mqtt_port,
        )
        self._client.connect(self._config.mqtt_host, self._config.mqtt_port, keepalive=60)
        self._client.loop_forever()

    def _build_client(self) -> mqtt.Client:
        try:
            client = mqtt.Client(
                mqtt.CallbackAPIVersion.VERSION2,
                client_id=self._config.mqtt_client_id,
            )
        except AttributeError:
            client = mqtt.Client(client_id=self._config.mqtt_client_id)

        client.on_connect = self._on_connect
        client.on_disconnect = self._on_disconnect
        client.on_message = self._on_message
        return client

    def _on_connect(self, client: mqtt.Client, _userdata, _flags, reason_code, _properties=None) -> None:
        if str(reason_code) not in ("0", "Success"):
            self._logger.error("MQTT connect failed: %s", reason_code)
            return

        self._logger.info("MQTT connected, subscribing %s", self._config.mqtt_status_topic)
        client.subscribe(self._config.mqtt_status_topic)

    def _on_disconnect(self, _client: mqtt.Client, _userdata, *args) -> None:
        reason_code = args[-2] if len(args) >= 2 else args[-1] if args else "unknown"
        self._logger.warning("MQTT disconnected: %s", reason_code)

    def _on_message(self, _client: mqtt.Client, _userdata, message: mqtt.MQTTMessage) -> None:
        if message.topic != self._config.mqtt_status_topic:
            return

        try:
            status = parse_status_payload(message.payload)
        except PayloadValidationError as exc:
            self._logger.warning("drop invalid status payload: %s", exc)
            return

        row_id = self._repository.insert_raw_status(status)
        rules_analysis = analyze_status(status)
        analysis = self._llm_service.maybe_refine(status, rules_analysis)
        analysis_row_id = self._repository.insert_analysis_result(analysis)
        analysis_payload = analysis.to_json()
        analysis_ok = _publish_text(
            self._client,
            self._config.mqtt_analysis_topic,
            analysis_payload,
            retain=True,
        )

        alarm_ok = True
        if analysis.cloud_risk >= 3:
            alarm_ok = _publish_text(
                self._client,
                self._config.mqtt_alarm_topic,
                analysis_payload,
                retain=False,
            )

        notice_count = 0
        for decision in build_notification_decisions(analysis):
            self._repository.insert_notification_decision(decision)
            notice_count += 1

        self._logger.info(
            "stored status row=%s analysis row=%s node=%s seq=%s risk=%s cloud_risk=%s notices=%s publish_analysis=%s publish_alarm=%s",
            row_id,
            analysis_row_id,
            status.node_id,
            status.seq,
            status.risk,
            analysis.cloud_risk,
            notice_count,
            analysis_ok,
            alarm_ok,
        )


def _publish_text(client: mqtt.Client, topic: str, payload: str, *, retain: bool) -> bool:
    result = client.publish(topic, payload, qos=0, retain=retain)
    return result.rc == mqtt.MQTT_ERR_SUCCESS
