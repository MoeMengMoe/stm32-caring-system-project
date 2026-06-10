import json
from dataclasses import dataclass

from .schemas import StatusPayload


@dataclass(frozen=True)
class AnalysisResult:
    node_id: str
    source_seq: int
    source_risk: int
    cloud_risk: int
    risk_score: int
    summary: str
    model_used: bool
    need_family_notice: bool
    need_community_notice: bool
    need_hospital_notice: bool

    def to_payload_dict(self) -> dict[str, object]:
        return {
            "node_id": self.node_id,
            "source_seq": self.source_seq,
            "source_risk": self.source_risk,
            "cloud_risk": self.cloud_risk,
            "risk_score": self.risk_score,
            "summary": self.summary,
            "model_used": self.model_used,
            "need_family_notice": self.need_family_notice,
            "need_community_notice": self.need_community_notice,
            "need_hospital_notice": self.need_hospital_notice,
        }

    def to_json(self) -> str:
        return json.dumps(self.to_payload_dict(), ensure_ascii=False, separators=(",", ":"))


def analyze_status(status: StatusPayload) -> AnalysisResult:
    score = _base_score(status.risk)
    reasons: list[str] = []

    if status.gas >= 900:
        score += 35
        reasons.append("燃气读数极高")
    elif status.gas >= 650:
        score += 25
        reasons.append("燃气读数偏高")
    elif status.gas >= 400:
        score += 12
        reasons.append("燃气读数升高")

    if status.temperature >= 35.0:
        score += 15
        reasons.append("室温过高")
    elif status.temperature <= 5.0:
        score += 12
        reasons.append("室温过低")

    if status.humidity >= 85.0:
        score += 8
        reasons.append("湿度过高")
    elif status.humidity <= 25.0:
        score += 6
        reasons.append("湿度过低")

    if status.presence == 0 and status.risk >= 2:
        score += 8
        reasons.append("无人状态下仍存在环境风险")

    score = max(0, min(100, score))
    cloud_risk = _risk_from_score(score)
    cloud_risk = max(cloud_risk, status.risk)

    summary = _summary(status, cloud_risk, score, reasons)
    need_family_notice = cloud_risk >= 2
    need_community_notice = cloud_risk >= 3 or score >= 85
    need_hospital_notice = cloud_risk >= 3 and status.presence == 1 and score >= 90

    return AnalysisResult(
        node_id=status.node_id,
        source_seq=status.seq,
        source_risk=status.risk,
        cloud_risk=cloud_risk,
        risk_score=score,
        summary=summary,
        model_used=False,
        need_family_notice=need_family_notice,
        need_community_notice=need_community_notice,
        need_hospital_notice=need_hospital_notice,
    )


def _base_score(source_risk: int) -> int:
    return {
        0: 10,
        1: 35,
        2: 65,
        3: 90,
    }.get(source_risk, 10)


def _risk_from_score(score: int) -> int:
    if score >= 85:
        return 3
    if score >= 60:
        return 2
    if score >= 30:
        return 1
    return 0


def _summary(status: StatusPayload, cloud_risk: int, score: int, reasons: list[str]) -> str:
    if reasons:
        reason_text = "、".join(reasons[:3])
    else:
        reason_text = "环境状态稳定"

    if cloud_risk >= 3:
        return f"{reason_text}，云端风险评分 {score}，建议立即确认现场安全。"
    if cloud_risk == 2:
        return f"{reason_text}，云端风险评分 {score}，建议尽快检查环境状态。"
    if cloud_risk == 1:
        return f"{reason_text}，云端风险评分 {score}，建议持续观察。"
    return f"{reason_text}，云端风险评分 {score}，当前无需额外处置。"
