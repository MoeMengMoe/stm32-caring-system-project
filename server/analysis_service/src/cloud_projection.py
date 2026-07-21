"""Derive cloud-side safety facts without pretending they came from firmware."""

from dataclasses import dataclass

from .rules_engine import AnalysisResult, analyze_status
from .schemas import StatusPayload


GAS_WARNING_THRESHOLD = 150


@dataclass(frozen=True)
class DerivedEvent:
    node_id: str
    event_type: str
    scenario: str
    state_before: str
    state_after: str
    risk: int
    result: str
    detail: str


def project_status_transition(
    previous: StatusPayload | None,
    current: StatusPayload,
    analysis: AnalysisResult,
) -> list[DerivedEvent]:
    """Return transition-only facts; every result is explicitly cloud-derived."""
    events: list[DerivedEvent] = []
    previous_gas = previous.gas if previous is not None else None
    previous_risk = analyze_status(previous).cloud_risk if previous is not None else 0

    if current.gas >= GAS_WARNING_THRESHOLD and (previous_gas is None or previous_gas < GAS_WARNING_THRESHOLD):
        events.append(
            DerivedEvent(
                current.node_id,
                "CLOUD_GAS_RISK_ENTER",
                "GAS_RISK",
                "NORMAL",
                "NOTICE",
                max(2, analysis.cloud_risk),
                "CREATED",
                f"云端根据燃气读数跨越 {GAS_WARNING_THRESHOLD} 阈值推导，当前值 {current.gas}",
            )
        )
    elif previous_gas is not None and previous_gas >= GAS_WARNING_THRESHOLD > current.gas:
        events.append(
            DerivedEvent(
                current.node_id,
                "CLOUD_GAS_RECOVERED",
                "GAS_RISK",
                "NOTICE",
                "CLEARED",
                0,
                "CLEARED",
                f"云端根据燃气读数回落至阈值以下推导，当前值 {current.gas}",
            )
        )

    if analysis.cloud_risk >= 3 and previous_risk < 3:
        events.append(
            DerivedEvent(
                current.node_id,
                "CLOUD_RISK_ESCALATED",
                "NONE",
                "NOTICE" if previous_risk else "NORMAL",
                "ALARM",
                analysis.cloud_risk,
                "ESCALATED",
                analysis.summary,
            )
        )
    elif previous is not None and previous_risk >= 2 and analysis.cloud_risk < 2:
        events.append(
            DerivedEvent(
                current.node_id,
                "CLOUD_RISK_CLEARED",
                "NONE",
                "NOTICE",
                "CLEARED",
                0,
                "CLEARED",
                analysis.summary,
            )
        )
    return events
