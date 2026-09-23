#!/bin/sh
# Fetch the pinned Dawn with the Switch changes into extern/dawn-switch.
set -e

PORT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

DAWN_REPO=https://github.com/new-coke/dawn-switch.git
# The Switch's bundled compiled shaders load only under this commit.
DAWN_REF=8d6453c82de3c1511aa2993e53b2a5b148407900
DAWN_DEST="$PORT/extern/dawn-switch"
# How far below DAWN_REF Aurora's Dawn commit may sit.
DAWN_DEPTH=50

# DAWN_REF must build on the Dawn Aurora pins, so the WebGPU headers match Aurora's code.
AURORA_DAWN_REF=$(sed -n 's/^_aurora_dependency_version(AURORA_DAWN_REF "\([0-9a-f]*\)".*/\1/p' \
    "$PORT/extern/aurora/cmake/AuroraDependencyVersions.cmake")
if [ -z "$AURORA_DAWN_REF" ]; then
    echo "fetch-switch-deps.sh: no AURORA_DAWN_REF in AuroraDependencyVersions.cmake" >&2
    exit 1
fi

DAWN_STAMP="$DAWN_REF $AURORA_DAWN_REF"

if [ -f "$DAWN_DEST/CMakeLists.txt" ] && [ "$(cat "$DAWN_DEST/.strikers-dawn" 2>/dev/null)" = "$DAWN_STAMP" ]; then
    echo "==> Dawn $DAWN_REF already in $DAWN_DEST"
    exit 0
fi

echo "==> fetching Dawn $DAWN_REF"
rm -rf "$DAWN_DEST"
mkdir -p "$DAWN_DEST"
git -C "$DAWN_DEST" init -q
git -C "$DAWN_DEST" remote add origin "$DAWN_REPO"
git -C "$DAWN_DEST" fetch -q --depth "$DAWN_DEPTH" origin "$DAWN_REF"
git -C "$DAWN_DEST" -c advice.detachedHead=false checkout -q FETCH_HEAD

if ! git -C "$DAWN_DEST" merge-base --is-ancestor "$AURORA_DAWN_REF" HEAD 2>/dev/null; then
    echo "fetch-switch-deps.sh: $DAWN_REF does not build on Aurora's Dawn $AURORA_DAWN_REF" \
        "within $DAWN_DEPTH commits; rebase it onto that commit." >&2
    exit 1
fi

# Dawn's own configure applies patches/abseil-cpp.patch to these when DAWN_PLATFORM_SWITCH is on.
echo "==> fetching Dawn's third-party dependencies"
(cd "$DAWN_DEST" && python3 tools/fetch_dawn_dependencies.py --shallow)

echo "$DAWN_STAMP" > "$DAWN_DEST/.strikers-dawn"
echo "==> ok"
