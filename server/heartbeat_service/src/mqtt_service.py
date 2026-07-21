import logging

import paho.mqtt.client as mqtt

from .config import Config
from .protocol import InvalidStatusPayload, build_ingest_ack


INGEST_ACK_QOS = 1


class HeartbeatService:
    def __init__(self, config: Config) -> None:
        self._config = config
        self._logger = logging.getLogger(__name__)
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

    def _on_connect(
        self, client: mqtt.Client, _userdata, _flags, reason_code, _properties=None
    ) -> None:
        if str(reason_code) not in ("0", "Success"):
            self._logger.error("MQTT connect failed: %s", reason_code)
            return

        result, _mid = client.subscribe(self._config.mqtt_status_topic, qos=1)
        if result != mqtt.MQTT_ERR_SUCCESS:
            self._logger.error(
                "failed to subscribe status topic=%s rc=%s",
                self._config.mqtt_status_topic,
                result,
            )
            return

        self._logger.info(
            "heartbeat ACK ready status_topic=%s ack_topic=%s node=%s",
            self._config.mqtt_status_topic,
            self._config.mqtt_ingest_ack_topic,
            self._config.node_id,
        )

    def _on_disconnect(self, _client: mqtt.Client, _userdata, *args) -> None:
        reason_code = args[-2] if len(args) >= 2 else args[-1] if args else "unknown"
        self._logger.warning("MQTT disconnected: %s", reason_code)

    def _on_message(
        self, client: mqtt.Client, _userdata, message: mqtt.MQTTMessage
    ) -> None:
        if message.topic != self._config.mqtt_status_topic:
            return

        try:
            ack = build_ingest_ack(bytes(message.payload), self._config.node_id)
        except InvalidStatusPayload as exc:
            self._logger.warning("status not acknowledged: %s", exc)
            return

        result = client.publish(
            self._config.mqtt_ingest_ack_topic,
            ack.to_json(),
            qos=INGEST_ACK_QOS,
            retain=False,
        )
        if result.rc != mqtt.MQTT_ERR_SUCCESS:
            self._logger.error(
                "failed to publish heartbeat ACK node=%s seq=%s rc=%s",
                ack.node_id,
                ack.seq,
                result.rc,
            )
            return

        self._logger.info("heartbeat ACK published node=%s seq=%s", ack.node_id, ack.seq)
