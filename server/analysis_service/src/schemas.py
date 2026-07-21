import json
from dataclasses import dataclass
from typing import Any


class PayloadValidationError(ValueError):
    pass


@dataclass(frozen=True)
class StatusPayload:
    node_id: str
    seq: int
    temperature: float
    humidity: float
    gas: int
    presence: int
    risk: int
    event: str
    relay_state_mask: int
    cloud_perm_mask: int
    raw_json: str


@dataclass(frozen=True)
class EventPayload:
    node_id: str
    event_id: int
    scenario: str
    event_type: str
    trigger_source: str
    state_before: str
    state_after: str
    risk: int
    result: str
    network_state: str
    power_state: str
    flags: int
    timestamp_ms: int
    raw_json: str


@dataclass(frozen=True)
class RelayStatePayload:
    node_id: str
    relay_id: int
    state: str
    request_id: int | None
    raw_json: str


@dataclass(frozen=True)
class RelayResultPayload:
    node_id: str
    relay_id: int
    request_id: int | None
    result: str
    state: str
    reason: str
    raw_json: str


@dataclass(frozen=True)
class AvailabilityPayload:
    node_id: str
    state: str
    raw_payload: str


SCENARIOS = frozenset(
    {
        "NONE",
        "SOS_OR_FALL_SIM",
        "LONG_STILL_NO_RESPONSE",
        "OFFLINE_AUTONOMY",
        "GAS_RISK",
    }
)

EVENT_TYPES = frozenset(
    {
        "STATUS_ONLY",
        "REMOTE_TRIGGER",
        "SOS_BUTTON",
        "LONG_STILL",
        "USER_ACK",
        "ACK_TIMEOUT",
        "CLEAR_ALARM",
        "NETWORK_LOST",
        "NETWORK_RESTORED",
        "POWER_BACKUP_ENTER",
        "POWER_NORMAL_RESTORED",
        "GAS_RISK",
        "VOICE_RISK",
        "EDGE_AI_RISK",
    }
)

TRIGGER_SOURCES = frozenset(
    {"LOCAL", "REMOTE", "BUTTON", "RADAR", "NETWORK", "POWER", "SENSOR", "VOICE", "AI", "GAS", "EDGE_AI"}
)
APP_STATES = frozenset({"NORMAL", "NOTICE", "ACK_WAIT", "ALARM", "NO_RESPONSE", "CLEARED"})
EVENT_RESULTS = frozenset(
    {
        "CREATED",
        "WAITING_ACK",
        "ACKNOWLEDGED",
        "ESCALATED",
        "CLEARED",
        "OFFLINE_CACHED",
        "BACKFILLED",
        "FAILED",
    }
)
NETWORK_STATES = frozenset({"ONLINE", "OFFLINE", "RESTORED"})
POWER_STATES = frozenset({"NORMAL", "BACKUP", "LOW"})


def parse_status_payload(payload: bytes) -> StatusPayload:
    data = _decode_json_object(payload)

    node_id = _required_str(data, "node_id")
    seq = _required_int(data, "seq", minimum=0)
    temperature = _required_float(data, "temperature")
    humidity = _required_float(data, "humidity")
    gas = _required_int(data, "gas", minimum=0)
    presence = _required_int(data, "presence", minimum=0, maximum=1)
    risk = _required_int(data, "risk", minimum=0, maximum=3)
    event = _required_str(data, "event")
    relay_state_mask = _optional_int(data, "relay_state_mask", default=0, minimum=0, maximum=15)
    cloud_perm_mask = _optional_int(data, "cloud_perm_mask", default=15, minimum=0, maximum=15)

    return StatusPayload(
        node_id=node_id,
        seq=seq,
        temperature=temperature,
        humidity=humidity,
        gas=gas,
        presence=presence,
        risk=risk,
        event=event,
        relay_state_mask=relay_state_mask,
        cloud_perm_mask=cloud_perm_mask,
        raw_json=json.dumps(data, ensure_ascii=False, separators=(",", ":")),
    )


def parse_event_payload(payload: bytes) -> EventPayload:
    data = _decode_json_object(payload)

    scenario = _required_enum(data, "scenario", SCENARIOS)
    event_type = _required_enum(data, "event_type", EVENT_TYPES)
    trigger_source = _required_enum(data, "trigger_source", TRIGGER_SOURCES)
    state_before = _required_enum(data, "state_before", APP_STATES)
    state_after = _required_enum(data, "state_after", APP_STATES)
    result = _required_enum(data, "result", EVENT_RESULTS)
    network_state = _required_enum(data, "network_state", NETWORK_STATES)
    power_state = _required_enum(data, "power_state", POWER_STATES)

    return EventPayload(
        node_id=_required_str(data, "node_id"),
        event_id=_required_int(data, "event_id", minimum=0),
        scenario=scenario,
        event_type=event_type,
        trigger_source=trigger_source,
        state_before=state_before,
        state_after=state_after,
        risk=_required_int(data, "risk", minimum=0, maximum=3),
        result=result,
        network_state=network_state,
        power_state=power_state,
        flags=_required_int(data, "flags", minimum=0),
        timestamp_ms=_required_int(data, "timestamp_ms", minimum=0),
        raw_json=json.dumps(data, ensure_ascii=False, separators=(",", ":")),
    )


def parse_relay_state_payload(payload: bytes) -> RelayStatePayload:
    data = _decode_json_object(payload)
    state = _required_enum(data, "state", frozenset({"ON", "OFF"}))
    return RelayStatePayload(
        node_id=_required_str(data, "node_id"),
        relay_id=_required_int(data, "relay_id", minimum=1, maximum=4),
        state=state,
        request_id=_optional_nullable_int(data, "request_id", minimum=0),
        raw_json=json.dumps(data, ensure_ascii=False, separators=(",", ":")),
    )


def parse_relay_result_payload(payload: bytes) -> RelayResultPayload:
    data = _decode_json_object(payload)
    return RelayResultPayload(
        node_id=_required_str(data, "node_id"),
        relay_id=_required_int(data, "relay_id", minimum=1, maximum=4),
        request_id=_optional_nullable_int(data, "request_id", minimum=0),
        result=_required_str(data, "result"),
        state=_required_enum(data, "state", frozenset({"ON", "OFF"})),
        reason=str(data.get("reason", ""))[:200],
        raw_json=json.dumps(data, ensure_ascii=False, separators=(",", ":")),
    )


def parse_availability_payload(payload: bytes, node_id: str) -> AvailabilityPayload:
    try:
        value = payload.decode("utf-8").strip().lower()
    except UnicodeDecodeError as exc:
        raise PayloadValidationError("availability payload is not valid UTF-8") from exc
    if value not in {"online", "offline"}:
        raise PayloadValidationError("availability must be online or offline")
    return AvailabilityPayload(node_id=node_id, state=value, raw_payload=value)


def _decode_json_object(payload: bytes) -> dict[str, Any]:
    try:
        text = payload.decode("utf-8")
    except UnicodeDecodeError as exc:
        raise PayloadValidationError("payload is not valid UTF-8") from exc

    try:
        data = json.loads(text)
    except json.JSONDecodeError as exc:
        raise PayloadValidationError(f"payload is not valid JSON: {exc}") from exc

    if not isinstance(data, dict):
        raise PayloadValidationError("payload must be a JSON object")

    return data


def _required_str(data: dict[str, Any], key: str) -> str:
    value = data.get(key)
    if not isinstance(value, str) or value == "":
        raise PayloadValidationError(f"{key} must be a non-empty string")
    return value


def _required_enum(data: dict[str, Any], key: str, allowed: frozenset[str]) -> str:
    value = _required_str(data, key)
    if value not in allowed:
        allowed_text = ", ".join(sorted(allowed))
        raise PayloadValidationError(f"{key} must be one of: {allowed_text}")
    return value


def _required_float(data: dict[str, Any], key: str) -> float:
    value = data.get(key)
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise PayloadValidationError(f"{key} must be a number")
    return float(value)


def _required_int(
    data: dict[str, Any],
    key: str,
    *,
    minimum: int | None = None,
    maximum: int | None = None,
) -> int:
    value = data.get(key)
    if isinstance(value, bool) or not isinstance(value, int):
        raise PayloadValidationError(f"{key} must be an integer")
    _check_range(key, value, minimum, maximum)
    return value


def _optional_int(
    data: dict[str, Any],
    key: str,
    *,
    default: int,
    minimum: int | None = None,
    maximum: int | None = None,
) -> int:
    if key not in data:
        return default
    value = data[key]
    if isinstance(value, bool) or not isinstance(value, int):
        raise PayloadValidationError(f"{key} must be an integer")
    _check_range(key, value, minimum, maximum)
    return value


def _optional_nullable_int(
    data: dict[str, Any], key: str, *, minimum: int | None = None, maximum: int | None = None
) -> int | None:
    value = data.get(key)
    if value is None:
        return None
    if isinstance(value, bool) or not isinstance(value, int):
        raise PayloadValidationError(f"{key} must be an integer or null")
    _check_range(key, value, minimum, maximum)
    return value


def _check_range(key: str, value: int, minimum: int | None, maximum: int | None) -> None:
    if minimum is not None and value < minimum:
        raise PayloadValidationError(f"{key} must be >= {minimum}")
    if maximum is not None and value > maximum:
        raise PayloadValidationError(f"{key} must be <= {maximum}")
