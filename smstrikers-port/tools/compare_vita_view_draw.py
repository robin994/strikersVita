#!/usr/bin/env python3
"""Compare two complete sealed view/draw captures for *structural* equivalence.

This is an A1c prerequisite, not a renderer pixel/depth oracle. Frame IDs and
timing/uniform upload counters are excluded intentionally: they may differ in
equivalent runs. Draw order, shader identities and all visible state must match.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from analyze_vita_view_draw import analyze

DRAW_FIELDS = (
    "view", "logical_draw", "target", "tev_stages", "texture_mask", "texgen_mask",
    "vertex_count", "index_count", "program_native", "indexed_pn", "blend",
    "depth", "alpha", "alpha_ref0", "alpha_ref1", "alpha_op", "scissor",
    "pipeline_requested", "pipeline_active", "vertex_hash", "fragment_hash",
    "vertex_payload_hash", "index_payload_hash", "uniform_payload_hash",
    "draw_state_hash",
)


def _events(path: Path) -> list[tuple]:
    events = []
    for line in Path(path).read_text().splitlines()[1:-1]:
        record = json.loads(line)
        kind = record["type"]
        if kind == "view":
            events.append((kind, record["view"]))
        elif kind == "draw":
            events.append((kind, *(record[name] for name in DRAW_FIELDS)))
        else:
            events.append((kind,))
    return events


def compare(control: Path, candidate: Path, *, allow_ini_change: bool = False,
            require_payloads: bool = False) -> dict:
    """Return a report or reject incomplete/incompatible trace identities."""
    reference, trial = analyze(Path(control)), analyze(Path(candidate))
    a, b = reference["identities"], trial["identities"]
    for key in ("self_sha256", "shader_cache_sha256"):
        if a[key] != b[key]:
            raise ValueError(f"Incompatible captures: {key} differs")
    if a["ini_sha256"] != b["ini_sha256"] and not allow_ini_change:
        raise ValueError("INI differs; use --allow-ini-change only after reviewing the exact differences")
    if reference["completed_frames"] != trial["completed_frames"]:
        raise ValueError("Different number of completed frames: cannot align draw sequences")
    if reference["payload_hashes_complete"] != trial["payload_hashes_complete"]:
        raise ValueError("Payload hashing mode differs between captures")
    if require_payloads and not reference["payload_hashes_complete"]:
        raise ValueError("Payload hashing required for A1c: enable vita_view_draw_payloads=1")

    rows_a, rows_b = _events(control), _events(candidate)
    first_diff = next((i for i in range(min(len(rows_a), len(rows_b)))
                       if rows_a[i] != rows_b[i]), None)
    if first_diff is None and len(rows_a) != len(rows_b):
        first_diff = min(len(rows_a), len(rows_b))
    # Do not silently align sequences by finding a close frame. Comparisons
    # require precisely the same deterministic scenario and frame window.
    equivalent = first_diff is None
    return {
        "structurally_equivalent": equivalent,
        "completed_frames": reference["completed_frames"],
        "payload_hashes_compared": reference["payload_hashes_complete"],
        "events_control": len(rows_a),
        "events_candidate": len(rows_b),
        "first_different_event": None if equivalent else first_diff + 1,
        "control_event": None if equivalent or first_diff >= len(rows_a) else rows_a[first_diff],
        "candidate_event": None if equivalent or first_diff >= len(rows_b) else rows_b[first_diff],
        "identity_check": "same SELF and shader cache; " +
            ("INI change explicitly allowed" if a["ini_sha256"] != b["ini_sha256"] else "same INI"),
        "limits": "With payload mode: compares submitted indexed vertex bytes, index bytes, uniform snapshots and sampler/scissor/viewport state. Does not prove texture pixel identity, rendered pixels, depth, or GPU execution time.",
    }


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("control", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--allow-ini-change", action="store_true")
    parser.add_argument("--require-payloads", action="store_true")
    args = parser.parse_args()
    report = compare(args.control, args.candidate, allow_ini_change=args.allow_ini_change,
                     require_payloads=args.require_payloads)
    print(json.dumps(report, indent=2, sort_keys=True))
    if not report["structurally_equivalent"]:
        raise SystemExit(2)
