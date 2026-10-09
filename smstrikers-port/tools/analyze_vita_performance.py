#!/usr/bin/env python3
"""Summarize completed GX snapshots without treating diagnostic timing as FPS."""
import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
import statistics

CUMULATIVE = {
    "gx_total_us", "xf_pos_inspected", "xf_pos_unchanged", "xf_pos_changed",
    "geometry_hits", "geometry_misses", "prepared_hits",
    "prepared_misses", "prepared_rejected", "pool_busy_fallbacks",
    "finish_calls", "finish_wait_us", "native_texture_hits",
    "native_geometry_hits", "native_rejected", "native_misses",
    "core3_chunks", "core3_denied", "core3_telemetry_failures", "core3_overruns",
    "native_model_attempts", "native_model_draws", "native_model_fallbacks", "native_model_compiled",
}
REQUIRED = {"frame", "gx_total_us", "producer_wait_us", "consumer_wait_us"}


def distribution(values):
    values = sorted(values)
    return {"median": statistics.median(values),
            "p95": values[math.ceil(len(values) * .95) - 1],
            "maximum": values[-1]}


def analyze(path):
    path = Path(path)
    data = path.read_bytes()
    lines = data.decode("utf-8").splitlines()
    metadata = [line for line in lines if line.startswith("#")]
    if not any(line.startswith("# strikers-consumer-capture-v1 ") for line in metadata):
        raise ValueError("Not a completed-consumer capture")
    reader = csv.DictReader(line for line in lines if line and not line.startswith("#"))
    fields = reader.fieldnames
    if not fields or len(fields) != len(set(fields)) or not REQUIRED.issubset(fields):
        raise ValueError("Missing or duplicate capture columns")
    rows = []
    samples = 0
    for raw in reader:
        if set(raw) != set(fields) or any(raw[key] is None for key in fields):
            raise ValueError("Malformed capture row")
        row = {key: int(value) for key, value in raw.items()}
        if any(value < 0 for value in row.values()) or not row["frame"]:
            raise ValueError("Invalid completed snapshot")
        samples += 1
        if rows and row["frame"] < rows[-1]["frame"]:
            raise ValueError("Consumer frame index went backwards")
        if rows and row["frame"] == rows[-1]["frame"]:
            rows[-1] = row
        else:
            rows.append(row)
    if len(rows) < 2:
        raise ValueError("At least two different completed frames are required")
    cumulative = CUMULATIVE.intersection(fields) | {key for key in fields if key.startswith("native_census_") or
                                                  key.startswith("native_cache_") and not key.startswith("native_cache_peak_")}
    for key in cumulative:
        if any(after[key] < before[key] for before, after in zip(rows, rows[1:])):
            raise ValueError("Cumulative counter reset: " + key)
    interval = rows[-1]["frame"] - rows[0]["frame"]
    result = {
        "path": str(path.resolve()), "sha256": hashlib.sha256(data).hexdigest(),
        "metadata": metadata, "samples": samples, "unique_completed_frames": len(rows),
        "repeated_snapshots": samples - len(rows),
        "frame_first": rows[0]["frame"], "frame_last": rows[-1]["frame"],
        "completed_frame_interval": interval,
        "unsampled_frames": interval + 1 - len(rows),
        "cumulative_endpoints": {key: {"first": rows[0][key], "last": rows[-1][key]}
                                 for key in sorted(cumulative)},
        "cumulative_deltas": {key: rows[-1][key] - rows[0][key] for key in sorted(cumulative)},
        "per_completed_frame": {key: (rows[-1][key] - rows[0][key]) / interval for key in sorted(cumulative)},
        "frame_distributions": {key: distribution(row[key] for row in rows)
                                for key in fields if key not in cumulative and key != "frame"},
        "limits": "Diagnostic samples are not quiet FPS. Nested phases and overlapping threads must not be summed. Scene finish waits are residual, not full GPU execution time.",
    }
    if {"core3_total_pct_x100", "core3_telemetry_valid"}.issubset(fields):
        valid = [row for row in rows if row["core3_telemetry_valid"] == 1]
        result["core3"] = {
            "valid_samples": len(valid), "all_samples_valid": len(valid) == len(rows),
            "maximum_observed_total_percent": max((row["core3_total_pct_x100"] / 100 for row in valid), default=None),
            "limit": "Sampled total-core usage includes external load; it cannot establish the maximum between samples.",
        }
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("path", type=Path)
    parser.add_argument("--out", type=Path)
    args = parser.parse_args()
    output = json.dumps(analyze(args.path), indent=2) + "\n"
    if args.out:
        args.out.write_text(output)
    print(output)
