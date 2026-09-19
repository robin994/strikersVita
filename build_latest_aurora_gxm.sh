#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PORT_DIR="$ROOT_DIR/smstrikers-port"
AURORA_REL="smstrikers-port/extern/aurora-vita"
AURORA_DIR="$ROOT_DIR/$AURORA_REL"
AURORA_BRANCH="${AURORA_BRANCH:-vita-experiment}"
BUILD_DIR="${BUILD_DIR:-$PORT_DIR/build-vita-gxm-latest}"
VITASDK="${VITASDK:-/usr/local/vitasdk}"
TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake"
PREFIX_PATH="$VITASDK/arm-vita-eabi"

usage() {
  cat <<'EOF'
Usage: ./build_latest_aurora_gxm.sh [--clean]

Builds Strikers for PS Vita with the latest aurora-vita vita-experiment branch
using the native GXM backend and all available logical CPU cores.

Environment overrides:
  AURORA_BRANCH   Aurora branch to build (default: vita-experiment)
  BUILD_DIR       CMake build directory
  VITASDK         VitaSDK root (default: /usr/local/vitasdk)
EOF
}

CLEAN=0
for arg in "$@"; do
  case "$arg" in
    --clean) CLEAN=1 ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown argument: $arg" >&2
      usage >&2
      exit 2
      ;;
  esac
done

if [[ ! -d "$PORT_DIR" ]]; then
  echo "Missing port directory: $PORT_DIR" >&2
  exit 1
fi

if [[ ! -f "$TOOLCHAIN_FILE" ]]; then
  echo "VitaSDK toolchain not found: $TOOLCHAIN_FILE" >&2
  echo "Set VITASDK to the correct VitaSDK root." >&2
  exit 1
fi

if [[ ! -d "$AURORA_DIR/.git" && ! -f "$AURORA_DIR/.git" ]]; then
  echo "Initializing aurora-vita submodule..."
  git -C "$ROOT_DIR" submodule update --init "$AURORA_REL"
fi

if [[ -n "$(git -C "$AURORA_DIR" status --porcelain)" ]]; then
  echo "aurora-vita has local modifications; refusing to overwrite them:" >&2
  git -C "$AURORA_DIR" status --short >&2
  echo "Commit/stash those changes, then rerun the build." >&2
  exit 1
fi

echo "Updating aurora-vita from origin/$AURORA_BRANCH..."
git -C "$AURORA_DIR" fetch origin "$AURORA_BRANCH"

if git -C "$AURORA_DIR" show-ref --verify --quiet "refs/heads/$AURORA_BRANCH"; then
  git -C "$AURORA_DIR" switch "$AURORA_BRANCH"
else
  git -C "$AURORA_DIR" switch -c "$AURORA_BRANCH" --track "origin/$AURORA_BRANCH"
fi

git -C "$AURORA_DIR" merge --ff-only "origin/$AURORA_BRANCH"
AURORA_REV="$(git -C "$AURORA_DIR" rev-parse HEAD)"
AURORA_DESC="$(git -C "$AURORA_DIR" log -1 --pretty='%h %s')"

if command -v sysctl >/dev/null 2>&1; then
  CORES="$(sysctl -n hw.logicalcpu 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || true)"
else
  CORES=""
fi
if [[ -z "$CORES" ]] && command -v getconf >/dev/null 2>&1; then
  CORES="$(getconf _NPROCESSORS_ONLN 2>/dev/null || true)"
fi
CORES="${CORES:-1}"

if [[ "$CLEAN" -eq 1 ]]; then
  echo "Removing build directory: $BUILD_DIR"
  rm -rf "$BUILD_DIR"
fi

echo
echo "Aurora: $AURORA_DESC"
echo "Aurora full revision: $AURORA_REV"
echo "Build directory: $BUILD_DIR"
echo "Parallel jobs: $CORES"
echo

cmake   -S "$PORT_DIR"   -B "$BUILD_DIR"   -G "Unix Makefiles"   -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE"   -DCMAKE_BUILD_TYPE=Release   -DSTRIKERS_AURORA=ON   -DSTRIKERS_FFMPEG=OFF   -DAURORA_VITA_RENDERER=GXM   -DCMAKE_PREFIX_PATH="$PREFIX_PATH"

cmake --build "$BUILD_DIR"   --target strikers_vita.vpk-vpk   --parallel "$CORES"

VPK="$BUILD_DIR/strikers_vita.vpk"
SELF="$BUILD_DIR/strikers_vita.self"

if [[ ! -f "$VPK" ]]; then
  echo "Build completed but VPK was not found: $VPK" >&2
  exit 1
fi

echo
echo "Build complete."
echo "Aurora revision: $AURORA_REV"
echo "VPK: $VPK"

if command -v shasum >/dev/null 2>&1; then
  shasum -a 256 "$VPK"
  [[ -f "$SELF" ]] && shasum -a 256 "$SELF"
elif command -v sha256sum >/dev/null 2>&1; then
  sha256sum "$VPK"
  [[ -f "$SELF" ]] && sha256sum "$SELF"
fi
