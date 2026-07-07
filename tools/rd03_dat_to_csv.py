#!/usr/bin/env python3
"""Parse official XenD101H/Rd-03 SaveData .dat files.

The official upper-computer tool stores raw report frames:
  header: F4 F3 F2 F1
  length: little-endian, normally 0x0083
  payload: presence(1) + distance_cm(2) + 32 * uint32 gate energies
  footer: F8 F7 F6 F5

Usage:
  python tools/rd03_dat_to_csv.py RadarData_2024_12_10_15_01_50.dat --csv rd03_official.csv
"""

from __future__ import annotations

import argparse
import csv
import statistics
from collections import Counter
from pathlib import Path


HEADER = bytes([0xF4, 0xF3, 0xF2, 0xF1])
FOOTER = bytes([0xF8, 0xF7, 0xF6, 0xF5])
PAYLOAD_LEN = 1 + 2 + 32 * 4
FRAME_LEN = len(HEADER) + 2 + PAYLOAD_LEN + len(FOOTER)
GATE_COUNT = 32


def le_u16(data: bytes, offset: int) -> int:
    return data[offset] | (data[offset + 1] << 8)


def le_u32(data: bytes, offset: int) -> int:
    return (
        data[offset]
        | (data[offset + 1] << 8)
        | (data[offset + 2] << 16)
        | (data[offset + 3] << 24)
    )


def parse_frames(blob: bytes) -> list[dict[str, int]]:
    frames: list[dict[str, int]] = []
    offset = 0
    frame_index = 0

    while offset < len(blob):
        pos = blob.find(HEADER, offset)
        if pos < 0:
            break
        if pos + FRAME_LEN > len(blob):
            break

        payload_len = le_u16(blob, pos + 4)
        payload_start = pos + 6
        footer_start = payload_start + payload_len
        footer_end = footer_start + len(FOOTER)
        if payload_len != PAYLOAD_LEN or blob[footer_start:footer_end] != FOOTER:
            offset = pos + 1
            continue

        payload = blob[payload_start:footer_start]
        presence = payload[0]
        distance_cm = le_u16(payload, 1)
        gates = [le_u32(payload, 3 + gate * 4) for gate in range(GATE_COUNT)]
        peak_gate = max(range(GATE_COUNT), key=lambda gate: gates[gate])
        peak_energy = gates[peak_gate]
        active_threshold = max(1, peak_energy // 6) if peak_energy > 0 else 0
        active_gates = sum(1 for value in gates if value >= active_threshold) if active_threshold else 0

        frame: dict[str, int] = {
            "frame": frame_index,
            "presence": presence,
            "distance_cm": distance_cm,
            "peak_gate": peak_gate,
            "peak_cm": peak_gate * 10,
            "peak_energy": peak_energy,
            "active_gates": active_gates,
            "energy_sum": sum(gates),
        }
        for gate, value in enumerate(gates):
            frame[f"g{gate:02d}"] = value
        frames.append(frame)

        frame_index += 1
        offset = footer_end

    return frames


def summarize(frames: list[dict[str, int]]) -> None:
    if not frames:
        print("No valid Rd-03 frames found.")
        return

    presence_frames = [frame for frame in frames if frame["presence"] != 0]
    print(f"frames: {len(frames)}")
    print(f"presence: {len(presence_frames)}/{len(frames)} ({100.0 * len(presence_frames) / len(frames):.1f}%)")

    if presence_frames:
        distances = [frame["distance_cm"] for frame in presence_frames if frame["distance_cm"] > 0]
        if distances:
            print(
                f"distance_cm: min={min(distances)} avg={statistics.mean(distances):.1f} "
                f"max={max(distances)}"
            )
        print("peak_gate distribution:")
        for gate, count in Counter(frame["peak_gate"] for frame in presence_frames).most_common(8):
            print(f"  g{gate:02d}: {count}")

    gate_means: list[tuple[int, float]] = []
    for gate in range(GATE_COUNT):
        values = [frame[f"g{gate:02d}"] for frame in frames]
        gate_means.append((gate, statistics.mean(values)))
    print("top gate mean energy:")
    for gate, mean_value in sorted(gate_means, key=lambda item: item[1], reverse=True)[:8]:
        print(f"  g{gate:02d}: {mean_value:.1f}")


def write_csv(frames: list[dict[str, int]], path: Path, label: str) -> None:
    fieldnames = [
        "label",
        "frame",
        "presence",
        "distance_cm",
        "peak_gate",
        "peak_cm",
        "peak_energy",
        "active_gates",
        "energy_sum",
    ] + [f"g{gate:02d}" for gate in range(GATE_COUNT)]

    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for frame in frames:
            row = {key: frame.get(key, "") for key in fieldnames}
            row["label"] = label
            writer.writerow(row)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dat", type=Path)
    parser.add_argument("--csv", type=Path)
    parser.add_argument("--label", default="official")
    args = parser.parse_args()

    frames = parse_frames(args.dat.read_bytes())
    summarize(frames)
    if args.csv is not None:
        write_csv(frames, args.csv, args.label)
        print(f"csv_written: {args.csv}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
