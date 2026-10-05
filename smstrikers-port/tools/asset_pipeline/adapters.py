"""Game-specific discovery/validation. Packaging has no game-specific rules."""
from __future__ import annotations
from collections import Counter
from pathlib import Path
import struct
import subprocess
import sys


def prepare_gamecube(iso: Path, workspace: Path, *, strikers: bool) -> tuple[list[Path], dict]:
    with iso.open("rb") as handle:
        header = handle.read(0x440)
    if len(header) != 0x440 or struct.unpack_from(">I", header, 0x1c)[0] != 0xc2339f3d:
        raise ValueError("input must be a plain GameCube ISO/GCM")
    game_id = header[:6].decode("ascii")
    if not game_id.isalnum():
        raise ValueError("invalid disc identity")
    if strikers and game_id not in ("G4QE01", "G4QP01", "G4QJ01"):
        raise ValueError(f"{game_id} is not a supported Strikers disc")
    tree = workspace / game_id
    extractor = Path(__file__).resolve().parents[1] / "extract-disc.py"
    try:
        subprocess.run([sys.executable, str(extractor), str(iso), str(tree)], check=True)
    except subprocess.CalledProcessError as exc:
        raise ValueError("disc extraction failed; archive was not published") from exc
    roots = [tree / "files", tree / "sys"]
    if strikers and not (roots[0] / "common.ini").is_file():
        raise ValueError("Strikers disc lacks common.ini")
    inventory = Counter(p.suffix.lower() or "[no extension]"
                        for p in roots[0].rglob("*") if p.is_file())
    return roots, {
        "game_id": game_id,
        "payload_policy": "original bytes; no runtime decompression, downsampling or endian rewrite",
        "file_types": dict(sorted(inventory.items())),
        "native_geometry": "requires a game-specific compiler and renderer consumer",
        "native_textures": "requires a game-specific compiler and texture consumer",
    }
