#!/usr/bin/env python3
"""Build simple sliding-window features from AI_SAMPLE CSV files.

Usage:
  python tools/ai_window_features.py ai_samples.csv ai_windows.csv
  python tools/ai_window_features.py ai_samples.csv ai_windows.csv 10 5

The optional numbers are window_seconds and stride_seconds.
"""

from __future__ import annotations

import csv
import statistics
import sys
from collections import defaultdict
from pathlib import Path


NUMERIC_FIELDS = [
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
    "risk",
    "scene_sev",
    "scene_conf",
    "scene_count",
    "scene_ev1",
    "scene_ev2",
    "edge_ai_risk",
    "edge_ai_conf",
    "edge_ai_stab",
    "edge_ai_ev",
    "edge_ai_trend",
    "edge_ai_score",
    "edge_ai_ms",
    "edge_ai_max_ms",
    "edge_ai_age_ms",
    "edge_ai_skip",
    "edge_ai_ran",
    "edge_ai_stale",
    "ack_ms",
    "relay",
    "manual",
    "auto",
]

TEXT_LAST_FIELDS = [
    "label",
    "state",
    "scenario",
    "risk_src",
    "scene_top",
    "scene_action",
    "edge_ai_scene",
    "edge_ai_raw",
    "event_type",
    "trigger",
]


def read_csv(path: Path) -> list[dict[str, str]]:
    with path.open("r", encoding="utf-8", newline="") as handle:
        return list(csv.DictReader(handle))


def as_float(row: dict[str, str], key: str) -> float | None:
    value = row.get(key)
    if value is None or value == "":
        return None
    try:
        if value.lower().startswith("0x"):
            return float(int(value, 16))
        return float(value)
    except ValueError:
        return None


def group_rows(rows: list[dict[str, str]]) -> dict[str, list[dict[str, str]]]:
    groups: dict[str, list[dict[str, str]]] = defaultdict(list)
    for row in rows:
        session = row.get("session") or "0"
        label = row.get("label") or "unknown"
        groups[f"{session}:{label}"].append(row)
    for grouped in groups.values():
        grouped.sort(key=lambda row: as_float(row, "t") or 0.0)
    return groups


def build_window(
    rows: list[dict[str, str]],
    start_ms: float,
    end_ms: float,
    group_key: str,
) -> dict[str, str] | None:
    window_rows = [
        row for row in rows
        if (timestamp := as_float(row, "t")) is not None and start_ms <= timestamp < end_ms
    ]
    if not window_rows:
        return None

    session, label = group_key.split(":", 1)
    out: dict[str, str] = {
        "session": session,
        "label": label,
        "window_start_ms": f"{start_ms:.0f}",
        "window_end_ms": f"{end_ms:.0f}",
        "n": str(len(window_rows)),
    }

    for field in TEXT_LAST_FIELDS:
        out[f"{field}_last"] = window_rows[-1].get(field, "")

    for field in NUMERIC_FIELDS:
        values = [value for row in window_rows if (value := as_float(row, field)) is not None]
        if not values:
            continue
        out[f"{field}_mean"] = f"{statistics.fmean(values):.4f}"
        out[f"{field}_min"] = f"{min(values):.4f}"
        out[f"{field}_max"] = f"{max(values):.4f}"
        out[f"{field}_last"] = f"{values[-1]:.4f}"

    return out


def build_features(
    rows: list[dict[str, str]],
    window_seconds: float,
    stride_seconds: float,
) -> list[dict[str, str]]:
    features: list[dict[str, str]] = []
    window_ms = window_seconds * 1000.0
    stride_ms = stride_seconds * 1000.0

    for group_key, grouped in group_rows(rows).items():
        times = [value for row in grouped if (value := as_float(row, "t")) is not None]
        if not times:
            continue
        start = min(times)
        last = max(times)
        cursor = start
        while cursor <= last:
            window = build_window(grouped, cursor, cursor + window_ms, group_key)
            if window is not None:
                features.append(window)
            cursor += stride_ms
    return features


def ordered_fields(rows: list[dict[str, str]]) -> list[str]:
    preferred = ["session", "label", "window_start_ms", "window_end_ms", "n"]
    seen = set(preferred)
    result = list(preferred)
    for row in rows:
        for key in row:
            if key not in seen:
                result.append(key)
                seen.add(key)
    return result


def write_csv(rows: list[dict[str, str]], path: Path) -> None:
    fieldnames = ordered_fields(rows)
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)


def main(argv: list[str]) -> int:
    if len(argv) not in (3, 5):
        print(
            "usage: python tools/ai_window_features.py input.csv output.csv [window_seconds stride_seconds]",
            file=sys.stderr,
        )
        return 2

    input_path = Path(argv[1])
    output_path = Path(argv[2])
    window_seconds = float(argv[3]) if len(argv) == 5 else 10.0
    stride_seconds = float(argv[4]) if len(argv) == 5 else 5.0
    if window_seconds <= 0 or stride_seconds <= 0:
        print("window_seconds and stride_seconds must be positive", file=sys.stderr)
        return 2

    rows = read_csv(input_path)
    features = build_features(rows, window_seconds, stride_seconds)
    if not features:
        print(f"no window features built from {input_path}", file=sys.stderr)
        return 1

    write_csv(features, output_path)
    print(f"wrote {len(features)} windows to {output_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
