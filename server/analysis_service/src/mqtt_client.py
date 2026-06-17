import json
import logging
from concurrent.futures import Future, ThreadPoolExecutor

import paho.mqtt.client as mqtt

from .config import Config
from .llm_service import LlmService
from .notifier import build_notification_decisions
from .repository import Repository
from .rules_engine import analyze_status
from .schemas import EventPayload, PayloadValidationError, parse_event_payload, parse_status_payload


class MqttStatusIngestor:
    def __init__(self, config: Config, repository: Repository) -> None:
        self._config = config
        self._repository = repository
        self._logger = logging.getLogger(__name__)
        self._llm_service = LlmService(config)
        self._executor = ThreadPoolExecutor(max_workers=4, thread_name_prefix="mqtt-ingest")
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

        self._logger.info(
            "MQTT connected, subscribing %s and %s",
            self._config.mqtt_status_topic,
            self._config.mqtt_event_topic,
        )
        client.subscribe(self._config.mqtt_status_topic)
        client.subscribe(self._config.mqtt_event_topic)

    def _on_disconnect(self, _client: mqtt.Client, _userdata, *args) -> None:
        reason_code = args[-2] if len(args) >= 2 else args[-1] if args else "unknown"
        self._logger.warning("MQTT disconnected: %s", reason_code)

    def _on_message(self, _client: mqtt.Client, _userdata, message: mqtt.MQTTMessage) -> None:
        if message.topic == self._config.mqtt_status_topic:
            self._submit_message(self._handle_status_message, bytes(message.payload), message.topic)
            return

        if message.topic == self._config.mqtt_event_topic:
            self._submit_message(self._handle_event_message, bytes(message.payload), message.topic)
            return

    def _submit_message(self, handler, payload: bytes, topic: str) -> None:
        future = self._executor.submit(handler, payload)
        future.add_done_callback(lambda done: self._log_worker_failure(done, topic))

    def _log_worker_failure(self, future: Future, topic: str) -> None:
        try:
            future.result()
        except Exception:
            self._logger.exception("failed to process MQTT message topic=%s", topic)

    def _handle_status_message(self, payload: bytes) -> None:
        try:
            status = parse_status_payload(payload)
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

        alarm_published = False
        alarm_ok = False
        if analysis.cloud_risk >= 3:
            if self._repository.has_clear_event_after_raw_status(row_id):
                self._logger.info(
                    "suppress status alarm because a clear event arrived after raw status row=%s node=%s seq=%s",
                    row_id,
                    status.node_id,
                    status.seq,
                )
            else:
                alarm_payload = _analysis_alarm_payload(analysis)
                alarm_ok = _publish_text(
                    self._client,
                    self._config.mqtt_alarm_topic,
                    alarm_payload,
                    retain=True,
                )
                alarm_published = True

        notice_count = 0
        for decision in build_notification_decisions(analysis):
            self._repository.insert_notification_decision(decision)
            notice_count += 1

        self._logger.info(
            "stored status row=%s analysis row=%s node=%s seq=%s risk=%s cloud_risk=%s notices=%s publish_analysis=%s alarm_published=%s publish_alarm=%s",
            row_id,
            analysis_row_id,
            status.node_id,
            status.seq,
            status.risk,
            analysis.cloud_risk,
            notice_count,
            analysis_ok,
            alarm_published,
            alarm_ok,
        )

    def _handle_event_message(self, payload: bytes) -> None:
        try:
            event = parse_event_payload(payload)
        except PayloadValidationError as exc:
            self._logger.warning("drop invalid event payload: %s", exc)
            return

        row_id = self._repository.insert_event_log(event)
        alarm_payload = _event_alarm_payload(event)
        alarm_publish_ok: bool | None = None
        if alarm_payload is not None:
            alarm_publish_ok = _publish_text(
                self._client,
                self._config.mqtt_alarm_topic,
                alarm_payload,
                retain=True,
            )

        self._logger.info(
            "stored event row=%s node=%s event_id=%s scenario=%s type=%s state=%s->%s risk=%s result=%s publish_alarm=%s",
            row_id,
            event.node_id,
            event.event_id,
            event.scenario,
            event.event_type,
            event.state_before,
            event.state_after,
            event.risk,
            event.result,
            alarm_publish_ok,
        )


def _publish_text(client: mqtt.Client, topic: str, payload: str, *, retain: bool) -> bool:
    result = client.publish(topic, payload, qos=0, retain=retain)
    return result.rc == mqtt.MQTT_ERR_SUCCESS


def _event_alarm_payload(event: EventPayload) -> str | None:
    should_clear = (
        event.event_type == "CLEAR_ALARM"
        or event.state_after == "CLEARED"
        or event.result in ("ACKNOWLEDGED", "CLEARED")
    )
    should_activate = (
        event.risk >= 3
        or event.state_after in ("ALARM", "NO_RESPONSE")
        or event.result == "ESCALATED"
        or (event.flags & 0x04) != 0
    )

    if not should_clear and not should_activate:
        return None

    active = not should_clear
    payload = {
        "node_id": event.node_id,
        "active": active,
        "event_id": event.event_id,
        "scenario": event.scenario,
        "state": event.state_after,
        "risk": event.risk if active else 0,
        "message": _event_alarm_message(event, active),
        "updated_at_ms": event.timestamp_ms,
    }
    return json.dumps(payload, ensure_ascii=False, separators=(",", ":"))


def _analysis_alarm_payload(analysis) -> str:
    payload = {
        "node_id": analysis.node_id,
        "active": True,
        "event_id": None,
        "scenario": "NONE",
        "state": "ALARM",
        "risk": analysis.cloud_risk,
        "message": analysis.summary,
        "updated_at_ms": None,
    }
    return json.dumps(payload, ensure_ascii=False, separators=(",", ":"))


def _event_alarm_message(event: EventPayload, active: bool) -> str:
    if not active:
        return "告警已清除或用户已确认安全"
    if event.state_after == "NO_RESPONSE":
        return "确认超时，已升级为无响应告警"
    if event.result == "ESCALATED":
        return "事件已升级为高风险告警"
    if event.network_state == "OFFLINE":
        return "离线期间发生高风险事件，已本地缓存"
    return "高风险事件已触发"
