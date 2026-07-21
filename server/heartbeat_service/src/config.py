import os
from dataclasses import dataclass


@dataclass(frozen=True)
class Config:
    mqtt_host: str
    mqtt_port: int
    mqtt_client_id: str
    mqtt_status_topic: str
    mqtt_ingest_ack_topic: str
    node_id: str
    log_level: str


def load_config() -> Config:
    return Config(
        mqtt_host=os.getenv("MQTT_HOST", "localhost"),
        mqtt_port=int(os.getenv("MQTT_PORT", "1883")),
        mqtt_client_id=os.getenv("MQTT_CLIENT_ID", "eldercare-heartbeat"),
        mqtt_status_topic=os.getenv("MQTT_STATUS_TOPIC", "eldercare/node01/status"),
        mqtt_ingest_ack_topic=os.getenv(
            "MQTT_INGEST_ACK_TOPIC", "eldercare/node01/ingest_ack"
        ),
        node_id=os.getenv("NODE_ID", "node01"),
        log_level=os.getenv("LOG_LEVEL", "INFO").upper(),
    )
