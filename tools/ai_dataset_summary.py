#!/usr/bin/env python3
"""Summarize AI_SAMPLE logs or CSV files.

Usage:
  python tools/ai_dataset_summary.py COM6-115200.log
  python tools/ai_dataset_summary.py ai_samples.csv
"""

from __future__ import annotations

import csv
import re
import statistics
import sys
from collections import Counter
from pathlib import Path


SAMPLE_PREFIX = "[AI_SAMPLE]"
FIELD_RE = re.compile(r"(\w+)=([^\s]+)")


def parse_sample_line(line: str) -> dict[str, str] | None:
    if SAMPLE_PREFIX not in line:
        return None
    fields = dict(FIELD_RE.findall(line.split(SAMPLE_PREFIX, 1)[1]))
    return fields or None


def read_log(path: Path) -> list[dict[str, str]]:
    samples: list[dict[str, str]] = []
    with path.open("r", encoding="utf-8", errors="ignore") as handle:
        for line in handle:
            sample = parse_sample_line(line)
            if sample is not None:
                samples.append(sample)
    return samples


def read_csv(path: Path) -> list[dict[str, str]]:
    with path.open("r", encoding="utf-8", newline="") as handle:
        return list(csv.DictReader(handle))


def read_samples(path: Path) -> list[dict[str, str]]:
    if path.suffix.lower() == ".csv":
        return read_csv(path)
    return read_log(path)


def as_float(sample: dict[str, str], key: str) -> float | None:
    value = sample.get(key)
    if value is None or value == "":
        return None
    try:
        if value.lower().startswith("0x"):
            return float(int(value, 16))
        return float(value)
    except ValueError:
        return None


def as_int(sample: dict[str, str], key: str) -> int | None:
    value = as_float(sample, key)
    if value is None:
        return None
    return int(value)


def summarize_counter(samples: list[dict[str, str]], key: str) -> None:
    counter = Counter(sample.get(key, "") for sample in samples)
    counter.pop("", None)
    if not counter:
        return
    print(f"{key}:")
    for name, count in counter.most_common():
        print(f"  {name}: {count}")


def summarize_numeric(samples: list[dict[str, str]], key: str) -> None:
    values = [value for sample in samples if (value := as_float(sample, key)) is not None]
    if not values:
        return
    print(
        f"{key}: min={min(values):.2f} max={max(values):.2f} "
        f"mean={statistics.fmean(values):.2f}"
    )


def summarize_valid_ratio(samples: list[dict[str, str]], key: str) -> None:
    values = [value for sample in samples if (value := as_int(sample, key)) is not None]
    if not values:
        return
    valid = sum(1 for value in values if value != 0)
    print(f"{key}: {valid}/{len(values)} = {valid / len(values) * 100:.1f}%")


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        print("usage: python tools/ai_dataset_summary.py input.log|input.csv", file=sys.stderr)
        return 2

    path = Path(argv[1])
    samples = read_samples(path)
    if not samples:
        print(f"no samples found in {path}", file=sys.stderr)
        return 1

    print(f"samples: {len(samples)}")
    times = [value for sample in samples if (value := as_float(sample, "t")) is not None]
    if times:
        duration_ms = max(times) - min(times)
        print(f"duration: {duration_ms / 1000.0:.1f}s")

    summarize_counter(samples, "label")
    summarize_counter(samples, "state")
    summarize_counter(samples, "scenario")
    summarize_counter(samples, "risk_src")
    summarize_counter(samples, "scene_top")
    summarize_counter(samples, "scene_action")
    summarize_counter(samples, "edge_ai_scene")
    summarize_counter(samples, "edge_ai_raw")
    summarize_counter(samples, "event_type")
    summarize_counter(samples, "trigger")

    summarize_numeric(samples, "risk")
    summarize_numeric(samples, "scene_sev")
    summarize_numeric(samples, "scene_conf")
    summarize_numeric(samples, "scene_count")
    summarize_numeric(samples, "edge_ai_risk")
    summarize_numeric(samples, "edge_ai_conf")
    summarize_numeric(samples, "edge_ai_stab")
    summarize_numeric(samples, "edge_ai_ev")
    summarize_numeric(samples, "edge_ai_trend")
    summarize_numeric(samples, "edge_ai_score")
    summarize_numeric(samples, "gas_ppm")
    summarize_numeric(samples, "gas_delta")
    summarize_numeric(samples, "radar_cm")
    summarize_numeric(samples, "motion")
    summarize_numeric(samples, "still")

    summarize_valid_ratio(samples, "env_valid")
    summarize_valid_ratio(samples, "gas_valid")
    summarize_valid_ratio(samples, "radar_valid")
    summarize_valid_ratio(samples, "presence")
    summarize_valid_ratio(samples, "pir")
    summarize_valid_ratio(samples, "rd03_ot2")
    summarize_valid_ratio(samples, "radar_presence")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
