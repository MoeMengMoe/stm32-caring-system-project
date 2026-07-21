import json
from dataclasses import dataclass


class InvalidStatusPayload(ValueError):
    """Raised when a status message cannot be acknowledged safely."""


@dataclass(frozen=True)
class IngestAck:
    node_id: str
    seq: int

    def to_json(self) -> str:
        return json.dumps(
            {"node_id": self.node_id, "seq": self.seq, "stored": True},
            ensure_ascii=False,
            separators=(",", ":"),
        )


def build_ingest_ack(payload: bytes, expected_node_id: str) -> IngestAck:
    try:
        decoded = json.loads(payload.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise InvalidStatusPayload("status payload is not valid UTF-8 JSON") from exc

    if not isinstance(decoded, dict):
        raise InvalidStatusPayload("status payload must be a JSON object")

    node_id = decoded.get("node_id")
    seq = decoded.get("seq")
    if node_id != expected_node_id:
        raise InvalidStatusPayload(f"unexpected node_id: {node_id!r}")
    if isinstance(seq, bool) or not isinstance(seq, int) or seq < 0:
        raise InvalidStatusPayload(f"invalid seq: {seq!r}")

    return IngestAck(node_id=node_id, seq=seq)
