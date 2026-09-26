#!/usr/bin/env bash
# A/B testbed for the aurora-vita gxm-optimization branch.
#
#   ./build_gxm_testbed.sh <aurora-rev> [label]
#
# Checks out <aurora-rev> in the aurora-vita submodule (the "local" remote points
# at ~/Documents/Code/PSVita/aurora-vita), builds Strikers with the same options
# as build_latest_aurora_gxm.sh into its own build directory, archives
# VPK/SELF/ELF plus revision and hashes under ab-artifacts/<label>/, then restores
# the submodule to the commit recorded before the run. Builds share the
# submodule working tree, so run them one at a time.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PORT_DIR="$ROOT_DIR/smstrikers-port"
AURORA_DIR="$PORT_DIR/extern/aurora-vita"
VITASDK="${VITASDK:-/usr/local/vitasdk}"

REV="${1:?usage: $0 <aurora-rev> [label]}"
git -C "$AURORA_DIR" fetch -q local 2>/dev/null || true
FULL_REV="$(git -C "$AURORA_DIR" rev-parse --verify "$REV^{commit}")"
SHORT_REV="$(git -C "$AURORA_DIR" rev-parse --short=8 "$FULL_REV")"
LABEL="${2:-$SHORT_REV}"
BUILD_DIR="$PORT_DIR/build-vita-gxm-ab-$LABEL"
OUT_DIR="$ROOT_DIR/ab-artifacts/$LABEL"

if [[ -n "$(git -C "$AURORA_DIR" status --porcelain)" ]]; then
  echo "aurora-vita submodule has local modifications; refusing to switch." >&2
  exit 1
fi
ORIGINAL_REV="$(git -C "$AURORA_DIR" rev-parse HEAD)"
restore() { git -C "$AURORA_DIR" checkout -q "$ORIGINAL_REV"; }
trap restore EXIT
git -C "$AURORA_DIR" checkout -q "$FULL_REV"

CORES="$(sysctl -n hw.logicalcpu 2>/dev/null || getconf _NPROCESSORS_ONLN)"
echo "Aurora: $(git -C "$AURORA_DIR" log -1 --pretty='%h %s')"
echo "Build:  $BUILD_DIR"

cmake -S "$PORT_DIR" -B "$BUILD_DIR" -G "Unix Makefiles" \
  -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake" \
  -DCMAKE_BUILD_TYPE=Release -DSTRIKERS_AURORA=ON -DSTRIKERS_FFMPEG=OFF \
  -DAURORA_VITA_RENDERER=GXM \
  -DSTRIKERS_VITA_NO_LOGS="${VITA_NO_LOGS:-ON}" \
  -DSTRIKERS_VITA_FORCE_60HZ="${VITA_FORCE_60HZ:-ON}" \
  -DSTRIKERS_VITA_GC_NATIVE_RES="${VITA_GC_NATIVE_RES:-OFF}" \
  -DCMAKE_PREFIX_PATH="$VITASDK/arm-vita-eabi" >/dev/null
cmake --build "$BUILD_DIR" --target strikers_vita.vpk-vpk --parallel "$CORES"

mkdir -p "$OUT_DIR"
for f in strikers_vita.vpk strikers_vita.self strikers_vita eboot.bin; do
  [[ -f "$BUILD_DIR/$f" ]] && cp "$BUILD_DIR/$f" "$OUT_DIR/"
done
{
  echo "aurora=$FULL_REV"
  echo "aurora_subject=$(git -C "$AURORA_DIR" log -1 --pretty=%s)"
  echo "strikers=$(git -C "$ROOT_DIR" rev-parse HEAD)$( [[ -n "$(git -C "$ROOT_DIR" status --porcelain -- smstrikers-port/src smstrikers-port/include)" ]] && echo '+dirty')"
  echo "built=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  (cd "$OUT_DIR" && shasum -a 256 strikers_vita.vpk strikers_vita.self 2>/dev/null)
} > "$OUT_DIR/BUILD_INFO.txt"
cat "$OUT_DIR/BUILD_INFO.txt"
