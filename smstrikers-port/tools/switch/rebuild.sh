#!/bin/sh
# Compile, generate ELF stubs, link and package the .nro. Usage: tools/switch/rebuild.sh [build-dir]

set -e

PORT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$PORT"
BUILD="${1:-build-switch}"
# switch.specs also reads it at link time.
export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
LOG=$(mktemp)
trap 'rm -f "$LOG"' EXIT INT TERM

echo "==> compile"
# Expected to fail at the link on a clean tree: the stubs do not exist yet.
set +e
cmake --build "$BUILD" --target strikers -- -k 0 >"$LOG" 2>&1
STATUS=$?
set -e
DIAG='\.(c|cc|cpp|h|hpp)[:(][0-9]+[:,][0-9]+\)?: (fatal )?error:'
if grep -qE "$DIAG" "$LOG"; then
    echo "==> COMPILE FAILED" >&2
    grep -E "$DIAG" "$LOG" | head -20 >&2
    exit 1
fi

OTHER=$(sed -n 's/^FAILED: \(\[code=[0-9]*\] \)\{0,1\}//p' "$LOG" \
    | tr -d '\r' | sed 's/[[:space:]]*$//' | grep -vxE 'strikers(\.elf)?' || true)
if [ -n "$OTHER" ] || { [ "$STATUS" -ne 0 ] && ! grep -q 'undefined reference' "$LOG"; }; then
    echo "==> BUILD FAILED before stub generation" >&2
    tail -40 "$LOG" >&2
    exit 1
fi

echo "==> generate stubs"
STRIKERS_SYMBOL_PREFIX= STRIKERS_BUILD_DIR="$BUILD" python3 tools/genstubs.py \
    --noop "^GX|^snd|^AI|^AR" --out "$BUILD/stubs_generated.c"

echo "==> link and package"
set +e
cmake --build "$BUILD" --target strikers_nro >"$LOG" 2>&1
STATUS=$?
set -e
if [ "$STATUS" -ne 0 ]; then
    echo "==> LINK FAILED" >&2
    grep -E 'error|undefined reference|multiple definition' "$LOG" | head -30 >&2
    exit 1
fi

if [ ! -f "$BUILD/strikers.nro" ]; then
    echo "==> FAILED: no .nro produced" >&2
    exit 1
fi
echo "==> ok: $BUILD/strikers.nro"
