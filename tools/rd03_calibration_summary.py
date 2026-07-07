#!/usr/bin/env python3
"""Summarize Rd-03 V2 [RADAR_CAL] logs.

Usage:
  python tools/rd03_calibration_summary.py COM6-115200.log
  python tools/rd03_calibration_summary.py COM6-115200.log --csv rd03_cal.csv --label empty_room
"""

from __future__ import annotations

import argparse
import csv
import re
import statistics
from collections import Counter
from pathlib import Path


PREFIX = "[RADAR_CAL]"
FIELD_RE = re.compile(r"(\w+)=([^\s]+)")
GATE_COUNT = 32


def parse_int(value: str | None) -> int | None:
    if value is None:
        return None
    try:
        return int(value, 0)
    except ValueError:
        return None


def parse_line(line: str) -> dict[str, str] | None:
    if PREFIX not in line:
        return None
    fields = dict(FIELD_RE.findall(line.split(PREFIX, 1)[1]))
    if not fields:
        return None
    gates = fields.get("gates")
    if gates:
        values = gates.split(",")
        for index, value in enumerate(values[:GATE_COUNT]):
            fields[f"g{index:02d}"] = value
    return fields


def read_samples(path: Path) -> list[dict[str, str]]:
    samples: list[dict[str, str]] = []
    with path.open("r", encoding="utf-8", errors="ignore") as handle:
        for line in handle:
            sample = parse_line(line)
            if sample is not None:
                samples.append(sample)
    return samples


def numeric(samples: list[dict[str, str]], key: str, *, positive: bool = False) -> list[int]:
    values: list[int] = []
    for sample in samples:
        value = parse_int(sample.get(key))
        if value is None:
            continue
        if positive and value <= 0:
            continue
        values.append(value)
    return values


def ratio(samples: list[dict[str, str]], key: str) -> float:
    if not samples:
        return 0.0
    return 100.0 * sum(1 for sample in samples if parse_int(sample.get(key)) == 1) / len(samples)


def summarize_range(samples: list[dict[str, str]], key: str, *, positive: bool = False) -> None:
    values = numeric(samples, key, positive=positive)
    if not values:
        print(f"{key}: no data")
        return
    print(
        f"{key}: min={min(values)} avg={statistics.mean(values):.1f} "
        f"max={max(values)} n={len(values)}"
    )


def summarize(samples: list[dict[str, str]]) -> None:
    if not samples:
        print("No [RADAR_CAL] samples found.")
        return

    times = numeric(samples, "t")
    valid_samples = [sample for sample in samples if parse_int(sample.get("valid")) == 1]
    presence_samples = [sample for sample in valid_samples if parse_int(sample.get("presence")) == 1]
    mismatch_count = sum(
        1
        for sample in valid_samples
        if parse_int(sample.get("presence")) != parse_int(sample.get("ot2"))
    )

    print(f"samples: {len(samples)}")
    if times:
        print(f"duration_ms: {max(times) - min(times)}")
    print(f"valid: {len(valid_samples)}/{len(samples)} ({ratio(samples, 'valid'):.1f}%)")
    print(f"presence_uart: {ratio(valid_samples, 'presence'):.1f}%")
    print(f"presence_ot2: {ratio(valid_samples, 'ot2'):.1f}%")
    print(f"uart_ot2_mismatch: {mismatch_count}")

    if presence_samples:
        print("presence distance:")
        summarize_range(presence_samples, "dist_cm", positive=True)
        summarize_range(presence_samples, "peak_gate")
        summarize_range(presence_samples, "peak_cm")
        summarize_range(presence_samples, "active")
        summarize_range(presence_samples, "motion")
        print("peak_gate distribution:")
        for gate, count in Counter(sample.get("peak_gate", "") for sample in presence_samples).most_common(8):
            if gate:
                print(f"  gate {gate}: {count}")

    rx_ovf_values = numeric(samples, "rx_ovf")
    if rx_ovf_values:
        print(f"rx_ovf_max: {max(rx_ovf_values)}")

    gate_means: list[tuple[int, float]] = []
    for gate in range(GATE_COUNT):
        values = numeric(valid_samples, f"g{gate:02d}")
        if values:
            gate_means.append((gate, statistics.mean(values)))
    if gate_means:
        print("top gate mean energy:")
        for gate, mean_value in sorted(gate_means, key=lambda item: item[1], reverse=True)[:8]:
            print(f"  g{gate:02d}: {mean_value:.1f}")


def write_csv(samples: list[dict[str, str]], path: Path, label: str) -> None:
    fieldnames = [
        "label",
        "t",
        "valid",
        "presence",
        "ot2",
        "dist_cm",
        "zone",
        "peak_gate",
        "peak_cm",
        "peak_energy",
        "active",
        "motion",
        "energy",
        "still",
        "occupied",
        "rx_ovf",
    ] + [f"g{index:02d}" for index in range(GATE_COUNT)]

    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for sample in samples:
            row = {key: sample.get(key, "") for key in fieldnames}
            row["label"] = label
            writer.writerow(row)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    parser.add_argument("--csv", type=Path, help="Optional output CSV path.")
    parser.add_argument("--label", default="unlabeled", help="Label written to CSV rows.")
    args = parser.parse_args()

    samples = read_samples(args.log)
    summarize(samples)
    if args.csv is not None:
        write_csv(samples, args.csv, args.label)
        print(f"csv_written: {args.csv}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
