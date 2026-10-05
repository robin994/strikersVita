#!/usr/bin/env python3
"""Prepare verified, deduplicated PSARC assets for Strikers or another port.

Uses only the Python standard library. Stored blocks avoid decompression work on
Vita. Game adapters validate/extract the source; packaging preserves all bytes.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import tempfile
import time

from asset_pipeline.adapters import prepare_gamecube
from asset_pipeline.psarc import collect_files, create_psarc, iter_entry, parse_psarc, verify_psarc


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(4 * 1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def verify_sources(archive_path: Path, roots: list[Path]) -> list[dict]:
    """Compare every archived byte against its prepared source, with bounded RAM."""
    parsed = parse_psarc(archive_path)
    sources = {item.archive_path: item for item in collect_files(roots)}
    if set(parsed.paths) != set(sources):
        raise ValueError("archive manifest differs from the prepared input tree")
    result = []
    with archive_path.open("rb", buffering=0) as handle:
        for i, name in enumerate(parsed.paths, start=1):
            h = hashlib.sha256()
            for block in iter_entry(handle, parsed, i):
                h.update(block)
            source = sources[name]
            digest = h.hexdigest()
            if parsed.entries[i].size != source.size or digest != sha256_file(source.source):
                raise ValueError(f"archive/source mismatch: {name}")
            result.append({"path": name, "bytes": source.size, "sha256": digest})
    return result


def prepare(profile: str, iso: Path | None, roots: list[Path], output: Path,
            *, replace: bool = False, deduplicate: bool = True) -> dict:
    if output.is_symlink():
        raise ValueError("refusing a symlink output")
    output = output.absolute()
    if output.exists() and not replace:
        raise FileExistsError(f"{output} exists; use --replace to replace it")
    if profile == "generic":
        if iso or not roots:
            raise ValueError("generic profile requires --root and no --iso")
    elif not iso or roots:
        raise ValueError("gamecube/strikers profile requires --iso and no --root")
    if iso and iso.resolve() == output.resolve():
        raise ValueError("output cannot replace the source disc")
    for root in roots:
        if output.resolve().is_relative_to(root.resolve()):
            raise ValueError("output must be outside the source tree")
    report_path = output.with_suffix(output.suffix + ".json")
    if report_path.is_symlink() or (report_path.exists() and not replace):
        raise FileExistsError(f"report path already exists: {report_path}")
    output.parent.mkdir(parents=True, exist_ok=True)
    started = time.perf_counter()
    # Only this invocation's private temporary workspace is removed.
    with tempfile.TemporaryDirectory(prefix="vita-assets-", dir=output.parent) as tmp:
        workspace = Path(tmp)
        source = None
        if profile != "generic":
            iso = iso.resolve()
            source_hash = sha256_file(iso)
            source = {"path": str(iso), "bytes": iso.stat().st_size, "sha256": source_hash}
            roots, adapter = prepare_gamecube(iso, workspace, strikers=profile == "strikers")
            if sha256_file(iso) != source_hash:
                raise ValueError("source disc changed during extraction")
        else:
            adapter = {"payload_policy": "original bytes", "roots": [str(p.resolve()) for p in roots]}
        candidate = workspace / "candidate.psarc"
        packed = create_psarc(candidate, roots, merge_duplicates=deduplicate)
        checked = verify_psarc(candidate)
        files = verify_sources(candidate, roots)
        archive_hash = sha256_file(candidate)
        packed["archive"] = checked["archive"] = str(output)
        report = {
            "format_version": 1, "profile": profile, "source_disc": source,
            "adapter": adapter, "create": packed, "verify": checked,
            "archive_sha256": archive_hash, "verified_files": files,
            "elapsed_seconds": time.perf_counter() - started,
            "upstream": {"repository": "https://github.com/Rinnegatamante/re4",
                         "commit": "dee72109dd06c0cf52e2fd21c095b3d09b23cad7"},
        }
        report_tmp = workspace / "report.json"
        report_tmp.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
        # Publish only after container validation and full source round-trip.
        os.replace(candidate, output)
        os.replace(report_tmp, report_path)
    return report


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--profile", choices=("strikers", "gamecube", "generic"), default="strikers")
    p.add_argument("--iso", type=Path)
    p.add_argument("--root", type=Path, action="append", default=[])
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--replace", action="store_true")
    p.add_argument("--no-dedup", action="store_true")
    a = p.parse_args()
    try:
        report = prepare(a.profile, a.iso, a.root, a.output,
                         replace=a.replace, deduplicate=not a.no_dedup)
    except (OSError, ValueError, RuntimeError) as exc:
        p.exit(1, f"Asset preparation failed: {exc}\n")
    print(json.dumps({"archive": str(a.output), "sha256": report["archive_sha256"],
                      "create": report["create"], "seconds": report["elapsed_seconds"]}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
