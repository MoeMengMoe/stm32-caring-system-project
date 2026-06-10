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


def parse_status_payload(payload: bytes) -> StatusPayload:
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


def _required_str(data: dict[str, Any], key: str) -> str:
    value = data.get(key)
    if not isinstance(value, str) or value == "":
        raise PayloadValidationError(f"{key} must be a non-empty string")
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


def _check_range(key: str, value: int, minimum: int | None, maximum: int | None) -> None:
    if minimum is not None and value < minimum:
        raise PayloadValidationError(f"{key} must be >= {minimum}")
    if maximum is not None and value > maximum:
        raise PayloadValidationError(f"{key} must be <= {maximum}")
