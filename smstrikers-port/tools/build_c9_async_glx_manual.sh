#!/bin/sh
# MANUAL ONLY. Run locally after reviewing the isolated experiment.
# It builds neither the shared C9 directory nor the PS Vita installation.
set -eu

SOURCE=$(CDPATH= cd "$(dirname "$0")/.." && pwd -P)
WORKTREE=$(CDPATH= cd "$SOURCE/.." && pwd -P)
ROOT=$(CDPATH= cd "$WORKTREE/../.." && pwd -P)
BUILD="$SOURCE/build-vita-c9-async-glx"
REFERENCE="$ROOT/smstrikers-port/build-vita-gxm-latest"
BRANCH=experiment/c9-async-glx-safe-20261021
BASELINE=recovery/c9-hardware-27fps-20261020
ORIGINAL_HEAD=bc468b1843e74e9099ff3396ffb0eac0cc28ebd8
ORIGINAL_INDEX=40c1d863ddedf0872f2c15997948114915a4e0fc
C9_SELF=7c8c6cd8d9f9857173cd51e22507654764e60d1c29b3d9d1b493be53225f3d6e
C9_VPK=5d7714b779ec501f8ab46b769f4a6383e323ab49e82fcfa35cf17c4e410a6132
die() { echo "ERROR: $*" >&2; exit 1; }
sha() { shasum -a 256 "$1" | awk '{print $1}'; }

[ "$(git -C "$WORKTREE" branch --show-current)" = "$BRANCH" ] || die "wrong async-GLX worktree"
git -C "$WORKTREE" merge-base --is-ancestor "$BASELINE" HEAD || die "C9 baseline ancestry missing"
[ -z "$(git -C "$WORKTREE" status --porcelain=v1 -uno)" ] || die "uncommitted source changes in experiment"
[ "$(git -C "$ROOT" rev-parse HEAD)" = "$ORIGINAL_HEAD" ] || die "main checkout HEAD differs"
[ "$(git -C "$ROOT" write-tree)" = "$ORIGINAL_INDEX" ] || die "main index was modified"
[ "$(sha "$REFERENCE/strikers_vita.self")" = "$C9_SELF" ] || die "protected C9 SELF differs"
[ "$(sha "$REFERENCE/strikers_vita.vpk")" = "$C9_VPK" ] || die "protected C9 VPK differs"
grep -Fq 'begin_async(pendingGrab' "$SOURCE/src/Game/RenderSnapshot.cpp" || die "missing asynchronous grab"
grep -Fq 'begin_async(pendingBlend' "$SOURCE/src/Game/RenderSnapshot.cpp" || die "missing asynchronous blend"
grep -Fq 'markerLookup.find' "$SOURCE/src/NL/glx/glxTexture.cpp" || die "missing GLX cache"
grep -Fq 'STRIKERS_VITA_GLX_CACHE' "$SOURCE/src/NL/glx/glxTexture.cpp" || die "missing opt-in GLX switch"
if [ -f "$BUILD/CMakeCache.txt" ]; then
    grep -Fqx "CMAKE_HOME_DIRECTORY:INTERNAL=$SOURCE" "$BUILD/CMakeCache.txt" || die "CMake cache refers to another source checkout"
fi

echo "Source branch: $(git -C "$WORKTREE" rev-parse HEAD)"
echo "Configuring isolated C9 async/GLX candidate (C9 artifacts protected)"
cmake -S "$SOURCE" -B "$BUILD" \
    -DCMAKE_TOOLCHAIN_FILE=/usr/local/vitasdk/share/vita.toolchain.cmake \
    -DVITA=ON -DSTRIKERS_VITA=ON -DCMAKE_BUILD_TYPE=Release \
    -DSTRIKERS_VITA_VITAGL=ON -DSTRIKERS_VITA_NATIVE_MATCH_DIRECT=ON \
    -DSTRIKERS_VITA_AUDIO_THREAD=ON -DSTRIKERS_VITA_HOT_LTO=ON \
    -DSTRIKERS_VITA_NO_LOGS=OFF -DSTRIKERS_VITA_PROFILER=OFF
grep -Fqx "CMAKE_HOME_DIRECTORY:INTERNAL=$SOURCE" "$BUILD/CMakeCache.txt" || die "CMake root changed unexpectedly"
cmake --build "$BUILD" --target strikers_vita.vpk-vpk --parallel 8

python3 - "$BUILD" "$REFERENCE" "$C9_SELF" "$C9_VPK" <<'PY'
import hashlib
from pathlib import Path
import sys
from zipfile import ZipFile

build, baseline = map(Path, sys.argv[1:3])
def digest(path): return hashlib.sha256(path.read_bytes()).hexdigest()
candidate_self = build / 'strikers_vita.self'
candidate_vpk = build / 'strikers_vita.vpk'
assert candidate_self.is_file() and candidate_vpk.is_file(), "Candidate SELF/VPK missing"
assert digest(baseline/'strikers_vita.self') == sys.argv[3], "C9 SELF modified"
assert digest(baseline/'strikers_vita.vpk') == sys.argv[4], "C9 VPK modified"
with ZipFile(candidate_vpk) as test, ZipFile(baseline/'strikers_vita.vpk') as stock:
    assert test.testzip() is None, "Corrupt candidate package"
    assert test.read('eboot.bin') == candidate_self.read_bytes(), "Packaged SELF mismatch"
    assert set(test.namelist()) == set(stock.namelist()), "Different VPK resources"
    for name in stock.namelist():
        if name != 'eboot.bin':
            assert test.read(name) == stock.read(name), "Unexpected VPK resource change: "+name
elf = (build/'strikers').read_bytes()
assert b'STRIKERS_VITA_GLX_CACHE' in elf, "GLX opt-in absent from linked executable"
assert b'STRIKERS_VITA_ASYNC_SNAPSHOT' in elf, "Async opt-in absent from linked executable"
assert digest(candidate_self) != digest(baseline/'strikers_vita.self'), "Candidate same as C9"
print('PASS: isolated binary, exact non-executable VPK resources, original C9 unchanged')
print('SELF:', candidate_self)
print('SELF SHA256:', digest(candidate_self))
print('VPK:', candidate_vpk)
print('VPK SHA256:', digest(candidate_vpk))
PY
