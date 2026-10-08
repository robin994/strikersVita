#!/usr/bin/env python3
"""Bind a raw Vita view/draw capture to verified local artifacts and validate it.

The raw Vita file deliberately has no guessed build/INI/shader-cache identity.
The user supplies downloaded artifact bytes, not pretyped hashes. For a shader
cache directory the hash is over sorted relative filenames and file contents.
"""

import argparse
import hashlib
import json
from pathlib import Path
import re

from analyze_vita_view_draw import SCHEMA, analyze

FOOTER = re.compile(r"# end records=(\d+) dropped=(\d+) capacity=(\d+)")


def identity(path: Path) -> str:
    if path.is_file():
        return hashlib.sha256(path.read_bytes()).hexdigest()
    if not path.is_dir():
        raise ValueError(f"Artifact missing: {path}")
    files = sorted(p for p in path.rglob("*") if p.is_file())
    if not files:
        raise ValueError(f"Shader cache directory empty: {path}")
    digest = hashlib.sha256()
    for item in files:
        name = item.relative_to(path).as_posix().encode()
        digest.update(len(name).to_bytes(4, "big"))
        digest.update(name)
        digest.update(hashlib.sha256(item.read_bytes()).digest())
    return digest.hexdigest()


def seal(raw: Path, output: Path, self_file: Path, ini_file: Path,
         shader_cache: Path, capture_id: str) -> dict:
    data = Path(raw).read_bytes()
    if not data.endswith(b"\n"):
        raise ValueError("Raw capture truncated")
    lines = data.decode("utf-8", errors="strict").splitlines()
    if len(lines) < 4 or lines[0] != "# aurora-vita-view-draw-raw-v2":
        raise ValueError("Unknown or empty raw capture")
    match = FOOTER.fullmatch(lines[-1])
    if not match:
        raise ValueError("Raw capture footer missing")
    count, dropped, capacity = map(int, match.groups())
    if dropped:
        raise ValueError("Raw capture is incomplete: lost records")
    if count != len(lines) - 2 or not count or not 0 < count <= capacity <= 65536:
        raise ValueError("Raw record count or capacity mismatch")
    if not capture_id or len(capture_id) > 128:
        raise ValueError("Invalid capture_id")
    header = {"type": "header", "schema": SCHEMA, "capture_id": capture_id,
              "capacity": capacity, "self_sha256": identity(Path(self_file)),
              "ini_sha256": identity(Path(ini_file)),
              "shader_cache_sha256": identity(Path(shader_cache))}
    footer = {"type": "footer", "records": count, "last_sequence": count,
              "dropped": dropped, "truncated": False}
    payload = "\n".join((json.dumps(header, sort_keys=True), *lines[1:-1],
                         json.dumps(footer, sort_keys=True))) + "\n"
    output = Path(output)
    tmp = output.with_name(output.name + ".unverified")
    try:
        tmp.write_text(payload)
        result = analyze(tmp)
        tmp.replace(output)
    finally:
        tmp.unlink(missing_ok=True)
    result["sha256"] = hashlib.sha256(payload.encode()).hexdigest()
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("raw", type=Path)
    parser.add_argument("--self", type=Path, required=True)
    parser.add_argument("--ini", type=Path, required=True)
    parser.add_argument("--shader-cache", type=Path, required=True)
    parser.add_argument("--capture-id", required=True)
    parser.add_argument("--out", type=Path, required=True)
    a = parser.parse_args()
    print(json.dumps(seal(a.raw, a.out, a.self, a.ini, a.shader_cache, a.capture_id), indent=2))
