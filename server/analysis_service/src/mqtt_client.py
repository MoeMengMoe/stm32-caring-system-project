import json
import logging
from concurrent.futures import Future, ThreadPoolExecutor

import paho.mqtt.client as mqtt

from .config import Config
from .cloud_projection import project_status_transition
from .llm_service import LlmService
from .notifier import PushPlusNotifier, build_notification_decisions
from .repository import Repository
from .rules_engine import analyze_status
from .schemas import (
    EventPayload,
    PayloadValidationError,
    parse_availability_payload,
    parse_event_payload,
    parse_relay_result_payload,
    parse_relay_state_payload,
    parse_status_payload,
)


PUSHPLUS_CHANNEL = "pushplus"
HA_ALARM_CHANNEL = "homeassistant"
HA_STATUS_ALARM_NOTICE_TYPE = "status_alarm"
NOTICE_TYPES = ("family", "community", "hospital")
INGEST_ACK_QOS = 1


class MqttStatusIngestor:
    def __init__(self, config: Config, repository: Repository) -> None:
        self._config = config
        self._repository = repository
        self._logger = logging.getLogger(__name__)
        self._llm_service = LlmService(config)
        self._pushplus_notifier = PushPlusNotifier(config)
        self._executor = ThreadPoolExecutor(max_workers=4, thread_name_prefix="mqtt-ingest")
        self._ordered_executor = ThreadPoolExecutor(max_workers=1, thread_name_prefix="mqtt-safety-order")
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

        topics = (
            self._config.mqtt_status_topic,
            self._config.mqtt_event_topic,
            self._config.mqtt_availability_topic,
            self._config.mqtt_relay_state_topic,
            self._config.mqtt_relay_result_topic,
        )
        self._logger.info("MQTT connected, subscribing %s", ", ".join(topics))
        for topic in topics:
            client.subscribe(topic)
        self._logger.info(
            "status ingest ACK enabled topic=%s qos=%s retain=false",
            self._config.mqtt_ingest_ack_topic,
            INGEST_ACK_QOS,
        )

    def _on_disconnect(self, _client: mqtt.Client, _userdata, *args) -> None:
        reason_code = args[-2] if len(args) >= 2 else args[-1] if args else "unknown"
        self._logger.warning("MQTT disconnected: %s", reason_code)

    def _on_message(self, _client: mqtt.Client, _userdata, message: mqtt.MQTTMessage) -> None:
        if message.topic == self._config.mqtt_status_topic:
            self._submit_message(self._handle_status_message, bytes(message.payload), message.topic, ordered=True)
            return

        if message.topic == self._config.mqtt_event_topic:
            self._submit_message(self._handle_event_message, bytes(message.payload), message.topic, ordered=True)
            return

        if message.topic == self._config.mqtt_availability_topic:
            self._submit_message(self._handle_availability_message, bytes(message.payload), message.topic)
            return

        if mqtt.topic_matches_sub(self._config.mqtt_relay_state_topic, message.topic):
            self._submit_message(self._handle_relay_state_message, bytes(message.payload), message.topic)
            return

        if mqtt.topic_matches_sub(self._config.mqtt_relay_result_topic, message.topic):
            self._submit_message(self._handle_relay_result_message, bytes(message.payload), message.topic)
            return

    def _submit_message(self, handler, payload: bytes, topic: str, *, ordered: bool = False) -> None:
        executor = self._ordered_executor if ordered else self._executor
        future = executor.submit(handler, payload)
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

        previous_status = self._repository.latest_status(status.node_id)
        row_id = self._repository.insert_raw_status(status)
        ingest_ack_ok = _publish_text(
            self._client,
            self._config.mqtt_ingest_ack_topic,
            _ingest_ack_payload(status),
            qos=INGEST_ACK_QOS,
            retain=False,
        )
        if not ingest_ack_ok:
            self._logger.error(
                "failed to publish status ingest ACK topic=%s node=%s seq=%s",
                self._config.mqtt_ingest_ack_topic,
                status.node_id,
                status.seq,
            )
        rules_analysis = analyze_status(status)
        analysis = self._llm_service.maybe_refine(status, rules_analysis)
        analysis_row_id = self._repository.insert_analysis_result(analysis)
        derived_events = project_status_transition(previous_status, status, analysis)
        for derived_event in derived_events:
            self._repository.insert_derived_event(derived_event)
        analysis_payload = analysis.to_json()
        analysis_ok = _publish_text(
            self._client,
            self._config.mqtt_analysis_topic,
            analysis_payload,
            retain=True,
        )

        alarm_published = False
        alarm_ok = False
        status_cleared_by_event = False
        if analysis.cloud_risk >= 3:
            status_cleared_by_event = self._repository.has_clear_event_after_raw_status(row_id)
            if status_cleared_by_event:
                self._logger.info(
                    "suppress status alarm because a clear event arrived after raw status row=%s node=%s seq=%s",
                    row_id,
                    status.node_id,
                    status.seq,
                )
                self._repository.clear_notification_state(
                    HA_ALARM_CHANNEL,
                    analysis.node_id,
                    HA_STATUS_ALARM_NOTICE_TYPE,
                )
            else:
                alarm_state_key = _analysis_alarm_state_key(analysis)
                if self._repository.claim_notification_state(
                    HA_ALARM_CHANNEL,
                    analysis.node_id,
                    HA_STATUS_ALARM_NOTICE_TYPE,
                    alarm_state_key,
                ):
                    alarm_payload = _analysis_alarm_payload(analysis)
                    alarm_ok = _publish_text(
                        self._client,
                        self._config.mqtt_alarm_topic,
                        alarm_payload,
                        retain=True,
                    )
                    alarm_published = True
                    self._repository.upsert_alarm_state(alarm_payload, "STATUS_ANALYSIS", alarm_ok)
                else:
                    self._logger.info(
                        "suppress duplicate HA alarm notification node=%s seq=%s state=%s",
                        analysis.node_id,
                        analysis.source_seq,
                        alarm_state_key,
                    )
                    self._repository.upsert_alarm_state(
                        _analysis_alarm_payload(analysis), "STATUS_ANALYSIS", None
                    )
        else:
            current_alarm = self._repository.get_alarm_state(analysis.node_id)
            edge_event_still_active = bool(
                current_alarm
                and current_alarm.get("active")
                and current_alarm.get("source") == "EDGE_EVENT"
            )
            alarm_was_active = False
            if not edge_event_still_active:
                alarm_was_active = self._repository.clear_notification_state(
                    HA_ALARM_CHANNEL,
                    analysis.node_id,
                    HA_STATUS_ALARM_NOTICE_TYPE,
                )
            if alarm_was_active:
                clear_payload = _analysis_alarm_clear_payload(analysis)
                alarm_ok = _publish_text(
                    self._client,
                    self._config.mqtt_alarm_topic,
                    clear_payload,
                    retain=True,
                )
                alarm_published = True
                self._repository.upsert_alarm_state(clear_payload, "STATUS_RECOVERY", alarm_ok)

        notice_count = 0
        notice_sent_count = 0
        decisions = [] if status_cleared_by_event else build_notification_decisions(analysis)
        active_notice_types = {decision.notice_type for decision in decisions}
        for notice_type in NOTICE_TYPES:
            if notice_type not in active_notice_types:
                self._repository.clear_notification_state(PUSHPLUS_CHANNEL, analysis.node_id, notice_type)

        for decision in decisions:
            state_key = _notification_state_key(decision)
            if not self._repository.claim_notification_state(
                PUSHPLUS_CHANNEL,
                decision.node_id,
                decision.notice_type,
                state_key,
            ):
                self._logger.info(
                    "suppress duplicate PushPlus notification node=%s type=%s seq=%s state=%s",
                    decision.node_id,
                    decision.notice_type,
                    analysis.source_seq,
                    state_key,
                )
                continue

            decision_id = self._repository.insert_notification_decision(decision)
            delivery = self._pushplus_notifier.send(decision)
            self._repository.insert_notification_delivery(decision_id, delivery)
            if delivery.sent:
                notice_sent_count += 1
            self._logger.info(
                "notification provider=%s type=%s sent=%s status=%s message=%s",
                delivery.provider,
                delivery.notice_type,
                delivery.sent,
                delivery.status_code,
                delivery.message,
            )
            notice_count += 1

        self._logger.info(
            "stored status row=%s analysis row=%s node=%s seq=%s risk=%s cloud_risk=%s notices=%s sent=%s ingest_ack=%s publish_analysis=%s alarm_published=%s publish_alarm=%s",
            row_id,
            analysis_row_id,
            status.node_id,
            status.seq,
            status.risk,
            analysis.cloud_risk,
            notice_count,
            notice_sent_count,
            ingest_ack_ok,
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
            if _event_alarm_active(event):
                alarm_state_key = _event_alarm_state_key(event)
                if self._repository.claim_notification_state(
                    HA_ALARM_CHANNEL,
                    event.node_id,
                    HA_STATUS_ALARM_NOTICE_TYPE,
                    alarm_state_key,
                ):
                    alarm_publish_ok = _publish_text(
                        self._client,
                        self._config.mqtt_alarm_topic,
                        alarm_payload,
                        retain=True,
                    )
                    self._repository.upsert_alarm_state(alarm_payload, "EDGE_EVENT", alarm_publish_ok)
                else:
                    alarm_publish_ok = False
                    self._logger.info(
                        "suppress duplicate HA event notification node=%s event_id=%s state=%s",
                        event.node_id,
                        event.event_id,
                        alarm_state_key,
                    )
                    self._repository.upsert_alarm_state(alarm_payload, "EDGE_EVENT", None)
            else:
                for notice_type in NOTICE_TYPES:
                    self._repository.clear_notification_state(PUSHPLUS_CHANNEL, event.node_id, notice_type)
                self._repository.clear_notification_state(
                    HA_ALARM_CHANNEL,
                    event.node_id,
                    HA_STATUS_ALARM_NOTICE_TYPE,
                )
                alarm_publish_ok = _publish_text(
                    self._client,
                    self._config.mqtt_alarm_topic,
                    alarm_payload,
                    retain=True,
                )
                self._repository.upsert_alarm_state(alarm_payload, "EDGE_EVENT", alarm_publish_ok)

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

    def _handle_availability_message(self, payload: bytes) -> None:
        node_id = self._config.mqtt_availability_topic.split("/")[-2]
        try:
            availability = parse_availability_payload(payload, node_id)
        except PayloadValidationError as exc:
            self._logger.warning("drop invalid availability payload: %s", exc)
            return
        row_id = self._repository.insert_availability(availability)
        self._logger.info("stored availability row=%s node=%s state=%s", row_id, node_id, availability.state)

    def _handle_relay_state_message(self, payload: bytes) -> None:
        try:
            state = parse_relay_state_payload(payload)
        except PayloadValidationError as exc:
            self._logger.warning("drop invalid relay state payload: %s", exc)
            return
        row_id = self._repository.insert_relay_state(state)
        self._logger.info("stored relay state row=%s node=%s relay=%s state=%s", row_id, state.node_id, state.relay_id, state.state)

    def _handle_relay_result_message(self, payload: bytes) -> None:
        try:
            result = parse_relay_result_payload(payload)
        except PayloadValidationError as exc:
            self._logger.warning("drop invalid relay result payload: %s", exc)
            return
        row_id = self._repository.insert_relay_result(result)
        self._logger.info(
            "stored relay result row=%s node=%s relay=%s request=%s result=%s",
            row_id, result.node_id, result.relay_id, result.request_id, result.result,
        )


def _publish_text(
    client: mqtt.Client,
    topic: str,
    payload: str,
    *,
    retain: bool,
    qos: int = 0,
) -> bool:
    result = client.publish(topic, payload, qos=qos, retain=retain)
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


def _event_alarm_active(event: EventPayload) -> bool:
    return not (
        event.event_type == "CLEAR_ALARM"
        or event.state_after == "CLEARED"
        or event.result in ("ACKNOWLEDGED", "CLEARED")
    )


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


def _ingest_ack_payload(status) -> str:
    payload = {
        "node_id": status.node_id,
        "seq": status.seq,
        "stored": True,
    }
    return json.dumps(payload, ensure_ascii=False, separators=(",", ":"))


def _analysis_alarm_clear_payload(analysis) -> str:
    payload = {
        "node_id": analysis.node_id,
        "active": False,
        "event_id": None,
        "scenario": "NONE",
        "state": "CLEARED",
        "risk": 0,
        "message": "风险状态已恢复，告警已自动清除",
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


def _analysis_alarm_state_key(analysis) -> str:
    state = {
        "active": True,
        "state": "ALARM",
        "risk": analysis.cloud_risk,
    }
    return json.dumps(state, ensure_ascii=False, sort_keys=True, separators=(",", ":"))


def _event_alarm_state_key(event: EventPayload) -> str:
    state = {
        "active": True,
        "state": event.state_after,
        "risk": event.risk,
    }
    return json.dumps(state, ensure_ascii=False, sort_keys=True, separators=(",", ":"))


def _notification_state_key(decision) -> str:
    payload = json.loads(decision.payload_json)
    analysis = payload.get("analysis", {})
    state = {
        "decision": decision.decision,
        "notice_type": decision.notice_type,
        "source_risk": analysis.get("source_risk"),
        "cloud_risk": analysis.get("cloud_risk"),
    }
    return json.dumps(state, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
