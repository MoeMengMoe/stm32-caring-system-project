#!/usr/bin/env python3
"""Convert COM serial logs containing [AI_SAMPLE] lines into CSV.

Usage:
  python tools/ai_sample_to_csv.py COM6-115200.log ai_samples.csv

The parser keeps unknown key=value fields automatically, so firmware-side
AI_SAMPLE additions do not require changing this script.
"""

from __future__ import annotations

import csv
import re
import sys
from pathlib import Path


SAMPLE_PREFIX = "[AI_SAMPLE]"
FIELD_RE = re.compile(r"(\w+)=([^\s]+)")


def parse_sample_line(line: str) -> dict[str, str] | None:
    if SAMPLE_PREFIX not in line:
        return None

    payload = line.split(SAMPLE_PREFIX, 1)[1]
    fields = dict(FIELD_RE.findall(payload))
    return fields or None


def collect_samples(path: Path) -> list[dict[str, str]]:
    samples: list[dict[str, str]] = []
    with path.open("r", encoding="utf-8", errors="ignore") as handle:
        for line in handle:
            sample = parse_sample_line(line)
            if sample is not None:
                samples.append(sample)
    return samples


def ordered_fieldnames(samples: list[dict[str, str]]) -> list[str]:
    preferred = [
        "t",
        "session",
        "label",
        "temp",
        "hum",
        "env_valid",
        "gas_valid",
        "gas_mv",
        "gas_base",
        "gas_ppm",
        "gas_dbg_offset",
        "gas_delta",
        "presence",
        "pir",
        "rd03_ot2",
        "radar_valid",
        "radar_presence",
        "radar_cm",
        "zone",
        "peak_gate",
        "peak_cm",
        "peak_energy",
        "active_gates",
        "motion",
        "energy",
        "still",
        "occupied",
        "radar_age_ms",
    "state",
    "scenario",
    "risk",
    "risk_src",
    "scene_top",
    "scene_action",
    "scene_sev",
    "scene_conf",
    "scene_count",
    "scene_mask",
    "scene_ev1",
    "scene_ev2",
    "event_id",
    "event_type",
        "trigger",
        "flags",
        "ack_ms",
        "relay",
        "manual",
        "auto",
    ]

    seen = set()
    result: list[str] = []
    for name in preferred:
        if any(name in sample for sample in samples):
            result.append(name)
            seen.add(name)

    for sample in samples:
        for name in sample:
            if name not in seen:
                result.append(name)
                seen.add(name)
    return result


def write_csv(samples: list[dict[str, str]], path: Path) -> None:
    fieldnames = ordered_fieldnames(samples)
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(samples)


def main(argv: list[str]) -> int:
    if len(argv) != 3:
        print("usage: python tools/ai_sample_to_csv.py input.log output.csv", file=sys.stderr)
        return 2

    input_path = Path(argv[1])
    output_path = Path(argv[2])
    samples = collect_samples(input_path)
    if not samples:
        print(f"no AI_SAMPLE lines found in {input_path}", file=sys.stderr)
        return 1

    write_csv(samples, output_path)
    print(f"wrote {len(samples)} samples to {output_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
