#!/bin/sh
# Build the pinned NVK driver on arm64 Docker. Usage: build-nvk.sh [dest]
set -e

PORT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
HERE="$PORT/tools/switch"
DEST="${1:-$PORT/extern/nvk-switch}"
SRC="$PORT/extern/mesa-switch"
REPO=https://github.com/danfromtico/mesa-switch.git

. "$HERE/deps.env"

sha256_stdin() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum | awk '{print $1}'
    else
        shasum -a 256 | awk '{print $1}'
    fi
}

STAMP="$MESA_REF $(cat "$0" "$HERE/package-nvk.sh" "$HERE/deps.env" | sha256_stdin)"
if [ -f "$DEST/lib/libvulkan.a" ] && [ "$(cat "$DEST/.strikers-nvk" 2>/dev/null)" = "$STAMP" ]; then
    echo "==> NVK already built in $DEST"
    exit 0
fi

# mesa-switch compiles NVK's Rust for the host it runs on, with no --target.
ARCH=$(docker info --format '{{.Architecture}}')
case "$ARCH" in
    aarch64|arm64) ;;
    *)
        echo "build-nvk.sh: Docker runs $ARCH; mesa-switch needs an arm64 host" >&2
        exit 1 ;;
esac

echo "==> fetching mesa-switch $MESA_REF"
rm -rf "$SRC"
mkdir -p "$SRC"
git -C "$SRC" init -q
git -C "$SRC" remote add origin "$REPO"
git -C "$SRC" fetch -q --depth 1 origin "$MESA_REF"
git -C "$SRC" -c advice.detachedHead=false checkout -q FETCH_HEAD

python3 - "$SRC/Docker.rust" "$DEVKITPRO_IMAGE" "$RUST_TOOLCHAIN" "$PY_TOOLS" "$CARGO_TOOLS" <<'PINS'
import pathlib, sys
path = pathlib.Path(sys.argv[1])
text = path.read_text()
for old, new in [
    ("FROM devkitpro/devkita64:latest", "FROM " + sys.argv[2]),
    ("--default-toolchain nightly &&", "--default-toolchain " + sys.argv[3] + " &&"),
    ("pip3 install --break-system-packages meson mako", "pip3 install --break-system-packages " + sys.argv[4]),
    ("cargo install bindgen-cli cbindgen", "cargo install --locked " + sys.argv[5]),
]:
    if text.count(old) != 1:
        sys.exit("Unexpected Mesa Dockerfile: " + old)
    text = text.replace(old, new)
path.write_text(text)
PINS
IMAGE="strikers-mesa-rust:$(sha256_stdin < "$SRC/Docker.rust" | cut -c1-12)"
python3 - "$SRC/build-switch.sh" "$IMAGE" <<'IMAGE_NAME'
import pathlib, sys
path = pathlib.Path(sys.argv[1])
text = path.read_text()
old = 'IMAGE_NAME="devkitpro-mesa-rust"'
if text.count(old) != 1:
    sys.exit("Unexpected Mesa build script image name")
path.write_text(text.replace(old, 'IMAGE_NAME="' + sys.argv[2] + '"'))
IMAGE_NAME

echo "==> building Mesa"
(cd "$SRC" && bash ./build-switch.sh)

echo "==> packaging into $DEST"
rm -rf "$DEST"
mkdir -p "$DEST"
# The packager needs the devkitA64 tools in the driver's build image.
docker run --rm \
    -v "$SRC:/project:ro" -v "$HERE:/tools:ro" -v "$DEST:/out" \
    -e MESA_REF="$MESA_REF" -e OWNER="$(id -u):$(id -g)" \
    "$IMAGE" sh /tools/package-nvk.sh /project /out

echo "$STAMP" > "$DEST/.strikers-nvk"
echo "==> ok"
