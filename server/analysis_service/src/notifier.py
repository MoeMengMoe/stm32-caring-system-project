import json
import logging
import urllib.error
import urllib.request
from dataclasses import dataclass

from .config import Config
from .rules_engine import AnalysisResult


@dataclass(frozen=True)
class NotificationDecision:
    node_id: str
    notice_type: str
    decision: str
    payload_json: str


@dataclass(frozen=True)
class NotificationDelivery:
    provider: str
    notice_type: str
    sent: bool
    status_code: int | None
    message: str


class PushPlusNotifier:
    def __init__(self, config: Config) -> None:
        self._config = config
        self._logger = logging.getLogger(__name__)

    def send(self, decision: NotificationDecision) -> NotificationDelivery:
        if not self._config.pushplus_enabled:
            return NotificationDelivery("pushplus", decision.notice_type, False, None, "disabled")

        if not self._config.pushplus_token:
            return NotificationDelivery("pushplus", decision.notice_type, False, None, "missing token")

        payload = json.loads(decision.payload_json)
        analysis = payload.get("analysis", {})
        title = _pushplus_title(decision.notice_type, analysis)
        content = _pushplus_content(decision.notice_type, analysis)
        request_body: dict[str, object] = {
            "token": self._config.pushplus_token,
            "title": title,
            "content": content,
            "template": self._config.pushplus_template,
        }

        if self._config.pushplus_topic:
            request_body["topic"] = self._config.pushplus_topic
        if self._config.pushplus_channel:
            request_body["channel"] = self._config.pushplus_channel

        request_data = json.dumps(request_body, ensure_ascii=False).encode("utf-8")
        request = urllib.request.Request(
            self._config.pushplus_url,
            data=request_data,
            headers={"Content-Type": "application/json; charset=utf-8"},
            method="POST",
        )

        try:
            with urllib.request.urlopen(request, timeout=self._config.pushplus_timeout_seconds) as response:
                response_text = response.read().decode("utf-8", errors="replace")
                ok, message = _parse_pushplus_response(response_text)
                if not ok:
                    self._logger.warning("PushPlus rejected notification: %s", response_text)
                return NotificationDelivery(
                    "pushplus",
                    decision.notice_type,
                    ok,
                    response.status,
                    message,
                )
        except urllib.error.HTTPError as exc:
            detail = exc.read().decode("utf-8", errors="replace")
            return NotificationDelivery("pushplus", decision.notice_type, False, exc.code, detail)
        except (urllib.error.URLError, TimeoutError, OSError) as exc:
            return NotificationDelivery("pushplus", decision.notice_type, False, None, str(exc))


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


def _pushplus_title(notice_type: str, analysis: dict[str, object]) -> str:
    risk = analysis.get("cloud_risk", analysis.get("source_risk", "?"))
    notice_text = {
        "family": "家属通知",
        "community": "社区通知",
        "hospital": "医疗通知",
    }.get(notice_type, "看护通知")
    return f"老人看护{notice_text}：风险等级 {risk}"


def _pushplus_content(notice_type: str, analysis: dict[str, object]) -> str:
    notice_text = {
        "family": "建议家属尽快确认现场情况。",
        "community": "建议社区人员介入确认。",
        "hospital": "建议联系医疗资源或应急联系人。",
    }.get(notice_type, "建议确认现场情况。")

    return "\n".join(
        [
            "<h3>独居老人看护告警</h3>",
            f"<p><b>节点：</b>{analysis.get('node_id', '-')}</p>",
            f"<p><b>状态序号：</b>{analysis.get('source_seq', '-')}</p>",
            f"<p><b>设备风险：</b>{analysis.get('source_risk', '-')}</p>",
            f"<p><b>云端风险：</b>{analysis.get('cloud_risk', '-')}</p>",
            f"<p><b>风险评分：</b>{analysis.get('risk_score', '-')}</p>",
            f"<p><b>判断摘要：</b>{analysis.get('summary', '-')}</p>",
            f"<p><b>建议动作：</b>{notice_text}</p>",
        ]
    )


def _parse_pushplus_response(response_text: str) -> tuple[bool, str]:
    try:
        data = json.loads(response_text)
    except json.JSONDecodeError:
        return False, response_text[:200]

    code = data.get("code")
    message = str(data.get("msg", data.get("message", "")))
    return code == 200, message
