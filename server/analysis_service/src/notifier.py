import json
from dataclasses import dataclass

from .rules_engine import AnalysisResult


@dataclass(frozen=True)
class NotificationDecision:
    node_id: str
    notice_type: str
    decision: str
    payload_json: str


def build_notification_decisions(result: AnalysisResult) -> list[NotificationDecision]:
    decisions: list[NotificationDecision] = []
    payload = result.to_payload_dict()

    if result.need_family_notice:
        decisions.append(_decision(result.node_id, "family", "suggest_notice", payload))
    if result.need_community_notice:
        decisions.append(_decision(result.node_id, "community", "suggest_notice", payload))
    if result.need_hospital_notice:
        decisions.append(_decision(result.node_id, "hospital", "suggest_notice", payload))

    return decisions


def _decision(
    node_id: str,
    notice_type: str,
    decision: str,
    payload: dict[str, object],
) -> NotificationDecision:
    body = {
        "node_id": node_id,
        "notice_type": notice_type,
        "decision": decision,
        "analysis": payload,
    }
    return NotificationDecision(
        node_id=node_id,
        notice_type=notice_type,
        decision=decision,
        payload_json=json.dumps(body, ensure_ascii=False, separators=(",", ":")),
    )
