import os
from dataclasses import dataclass


@dataclass(frozen=True)
class Config:
    mqtt_host: str
    mqtt_port: int
    mqtt_client_id: str
    mqtt_status_topic: str
    mqtt_event_topic: str
    mqtt_analysis_topic: str
    mqtt_alarm_topic: str
    db_path: str
    log_level: str
    llm_enabled: str
    llm_api_key: str
    llm_base_url: str
    llm_model: str
    llm_timeout_seconds: float
    llm_min_risk: int
    llm_min_gas: int


def load_config() -> Config:
    llm_base_url = _normalize_llm_base_url(
        os.getenv("LLM_BASE_URL", "https://api.openai.com/v1/chat/completions")
    )
    return Config(
        mqtt_host=os.getenv("MQTT_HOST", "localhost"),
        mqtt_port=int(os.getenv("MQTT_PORT", "1883")),
        mqtt_client_id=os.getenv("MQTT_CLIENT_ID", "eldercare-analysis"),
        mqtt_status_topic=os.getenv("MQTT_STATUS_TOPIC", "eldercare/node01/status"),
        mqtt_event_topic=os.getenv("MQTT_EVENT_TOPIC", "eldercare/node01/event"),
        mqtt_analysis_topic=os.getenv("MQTT_ANALYSIS_TOPIC", "eldercare/node01/analysis"),
        mqtt_alarm_topic=os.getenv("MQTT_ALARM_TOPIC", "eldercare/node01/alarm"),
        db_path=os.getenv("DB_PATH", "data/eldercare.db"),
        log_level=os.getenv("LOG_LEVEL", "INFO").upper(),
        llm_enabled=os.getenv("LLM_ENABLED", "auto").lower(),
        llm_api_key=os.getenv("OPENAI_API_KEY", ""),
        llm_base_url=llm_base_url,
        llm_model=os.getenv("LLM_MODEL", "gpt-4o-mini"),
        llm_timeout_seconds=float(os.getenv("LLM_TIMEOUT_SECONDS", "8")),
        llm_min_risk=int(os.getenv("LLM_MIN_RISK", "2")),
        llm_min_gas=int(os.getenv("LLM_MIN_GAS", "650")),
    )


def _normalize_llm_base_url(url: str) -> str:
    value = url.strip()
    if value.endswith("/"):
        value = value[:-1]
    if value.endswith("/v1"):
        return f"{value}/chat/completions"
    return value
