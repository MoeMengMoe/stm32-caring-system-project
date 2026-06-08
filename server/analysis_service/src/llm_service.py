import json
import logging
import urllib.error
import urllib.request
from dataclasses import replace
from typing import Any

from .config import Config
from .rules_engine import AnalysisResult
from .schemas import StatusPayload


class LlmService:
    def __init__(self, config: Config) -> None:
        self._config = config
        self._logger = logging.getLogger(__name__)

    def maybe_refine(self, status: StatusPayload, rules_result: AnalysisResult) -> AnalysisResult:
        if not self._should_call(status, rules_result):
            return rules_result

        try:
            model_result = self._call_model(status, rules_result)
        except Exception as exc:
            self._logger.warning("LLM analysis skipped after failure: %s", exc)
            return rules_result

        return _merge_result(rules_result, model_result)

    def _should_call(self, status: StatusPayload, rules_result: AnalysisResult) -> bool:
        if self._config.llm_enabled in ("0", "false", "off", "no"):
            return False
        if self._config.llm_enabled not in ("auto", "1", "true", "on", "yes"):
            self._logger.warning("unknown LLM_ENABLED=%s, disabling LLM", self._config.llm_enabled)
            return False
        if not self._config.llm_api_key:
            return False

        return (
            status.risk >= self._config.llm_min_risk
            or rules_result.cloud_risk >= self._config.llm_min_risk
            or status.gas >= self._config.llm_min_gas
        )

    def _call_model(self, status: StatusPayload, rules_result: AnalysisResult) -> dict[str, Any]:
        request_body = {
            "model": self._config.llm_model,
            "temperature": 0.2,
            "response_format": {"type": "json_object"},
            "messages": [
                {
                    "role": "system",
                    "content": (
                        "你是独居老人看护系统的安全分析助手。"
                        "只输出 JSON，不要输出 Markdown。"
                        "字段必须包含 cloud_risk、risk_score、summary、"
                        "need_family_notice、need_community_notice、need_hospital_notice。"
                        "cloud_risk 范围 0-3，risk_score 范围 0-100。"
                    ),
                },
                {
                    "role": "user",
                    "content": json.dumps(
                        {
                            "status": {
                                "node_id": status.node_id,
                                "seq": status.seq,
                                "temperature": status.temperature,
                                "humidity": status.humidity,
                                "gas": status.gas,
                                "presence": status.presence,
                                "risk": status.risk,
                                "event": status.event,
                                "relay_state_mask": status.relay_state_mask,
                                "cloud_perm_mask": status.cloud_perm_mask,
                            },
                            "rules_result": rules_result.to_payload_dict(),
                            "instruction": "在不降低规则风险等级的前提下，给出更清晰的一句话安全摘要和通知建议。",
                        },
                        ensure_ascii=False,
                    ),
                },
            ],
        }

        request = urllib.request.Request(
            self._config.llm_base_url,
            data=json.dumps(request_body).encode("utf-8"),
            headers={
                "Authorization": f"Bearer {self._config.llm_api_key}",
                "Content-Type": "application/json",
            },
            method="POST",
        )

        try:
            with urllib.request.urlopen(request, timeout=self._config.llm_timeout_seconds) as response:
                response_body = response.read().decode("utf-8")
        except urllib.error.HTTPError as exc:
            detail = exc.read().decode("utf-8", errors="replace")
            raise RuntimeError(f"HTTP {exc.code}: {detail[:300]}") from exc

        data = json.loads(response_body)
        content = data["choices"][0]["message"]["content"]
        if not isinstance(content, str):
            raise RuntimeError("model content is not a string")
        return json.loads(content)


def _merge_result(rules_result: AnalysisResult, model_result: dict[str, Any]) -> AnalysisResult:
    cloud_risk = max(rules_result.cloud_risk, _bounded_int(model_result, "cloud_risk", 0, 3, rules_result.cloud_risk))
    risk_score = max(rules_result.risk_score, _bounded_int(model_result, "risk_score", 0, 100, rules_result.risk_score))
    summary = _bounded_summary(model_result.get("summary"), rules_result.summary)

    return replace(
        rules_result,
        cloud_risk=cloud_risk,
        risk_score=risk_score,
        summary=summary,
        model_used=True,
        need_family_notice=rules_result.need_family_notice
        or _bool_value(model_result.get("need_family_notice")),
        need_community_notice=rules_result.need_community_notice
        or _bool_value(model_result.get("need_community_notice")),
        need_hospital_notice=rules_result.need_hospital_notice
        or _bool_value(model_result.get("need_hospital_notice")),
    )


def _bounded_int(data: dict[str, Any], key: str, minimum: int, maximum: int, default: int) -> int:
    value = data.get(key)
    if isinstance(value, bool) or not isinstance(value, int):
        return default
    return max(minimum, min(maximum, value))


def _bounded_summary(value: Any, default: str) -> str:
    if not isinstance(value, str):
        return default
    text = value.strip()
    if not text:
        return default
    return text[:160]


def _bool_value(value: Any) -> bool:
    return value is True
