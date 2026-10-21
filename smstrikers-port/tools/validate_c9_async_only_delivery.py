#!/usr/bin/env python3
"""Verify the four deliverables and record a reproducible C9 async-only manifest."""
from __future__ import annotations

import hashlib
import json
import subprocess
from pathlib import Path
from zipfile import ZipFile


def digest(path: Path) -> str:
    sha = hashlib.sha256()
    with path.open("rb") as handle:
        for part in iter(lambda: handle.read(1024 * 1024), b""):
            sha.update(part)
    return sha.hexdigest()


def main() -> None:
    source = Path(__file__).resolve().parents[1]
    worktree = source.parent
    build = source / "build-vita-async-delivery"
    config = (source / "src/platform/config.c").read_text()
    example = (source / "strikers-vitagl.ini.example").read_text()
    cache = (build / "CMakeCache.txt").read_text()

    assert '{ "vita_async_snapshot", "1" }' in config
    assert '{ "vita_glx_cache", "0" }' in config
    assert '{ "vita_frameskip", "0" }' in config
    for line in ("vita_async_snapshot = 1", "vita_glx_cache = 0",
                 "vita_frameskip = 0"):
        assert line in example, line
    for key in ("CMAKE_BUILD_TYPE:STRING=Release",
                "STRIKERS_VITA_PROFILER:BOOL=OFF",
                "STRIKERS_VITA_PROFILE_AUTOSTART:BOOL=OFF",
                "STRIKERS_VITA_NO_LOGS:BOOL=OFF"):
        assert key in cache, key
    assert ("CMAKE_HOME_DIRECTORY:INTERNAL=" + str(source)) in cache

    elf = build / "strikers"
    velf = build / "strikers.velf"
    signed = build / "strikers_vita.self"
    package = build / "strikers_vita.vpk"
    for candidate in (elf, velf, signed, package):
        assert candidate.is_file() and candidate.stat().st_size > 100000, candidate
    executable = elf.read_bytes()
    assert b"STRIKERS_VITA_ASYNC_SNAPSHOT" in executable
    assert b"STRIKERS_VITA_GLX_CACHE" in executable

    with ZipFile(package) as archive:
        assert archive.testzip() is None, "VPK archive failed CRC validation"
        assert "eboot.bin" in archive.namelist()
        embedded_eboot = archive.read("eboot.bin")
        assert embedded_eboot == signed.read_bytes(), "SELF differs from VPK eboot"
        resources = sorted(name for name in archive.namelist() if name != "eboot.bin")
        assert len(resources) >= 4, "VPK resource set unexpectedly small"

    eboot = build / "eboot.bin"
    if not eboot.is_file() or eboot.read_bytes() != embedded_eboot:
        eboot.write_bytes(embedded_eboot)
    artifacts = {}
    for name, candidate in (
        ("eboot.bin", eboot),
        ("vpk", package),
        ("self", signed),
        ("velf", velf),
    ):
        artifacts[name] = {
            "name": candidate.name,
            "bytes": candidate.stat().st_size,
            "sha256": digest(candidate),
        }
    head = subprocess.check_output(
        ["git", "-C", str(worktree), "rev-parse", "HEAD"], text=True
    ).strip()
    branch = subprocess.check_output(
        ["git", "-C", str(worktree), "branch", "--show-current"], text=True
    ).strip()
    assert branch == "checkpoint/c9-snapshot-async-only-20261021", branch
    manifest = {
        "profile": "C9 snapshot async ONLY",
        "source_commit": head,
        "branch": branch,
        "vita_config": {
            "vita_async_snapshot": 1,
            "vita_glx_cache": 0,
            "vita_frameskip": 0,
            "profiling": 0,
        },
        "build": "Release, Vita, C9 native direct match, hot LTO, audio worker",
        "packaged_eboot_identical_to_self": True,
        "vpk_resources": resources,
        "artifacts": artifacts,
        "hardware_tested": False,
    }
    target = build / "snapshot-async-only-manifest.json"
    target.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n")
    print("PASS: C9 snapshot async-only profile, package integrity, SELF/eboot identity")
    print(f"MANIFEST {target}")
    for key, value in artifacts.items():
        print(f"{key.upper()} {value['bytes']} bytes SHA256 {value['sha256']}")


if __name__ == "__main__":
    main()
