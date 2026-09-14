#!/bin/sh
# Static THP-only libavcodec, so the game needs no distro soname: tools/build-ffmpeg.sh [dest]

set -e

PORT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
DEST="${1:-$PORT/extern/ffmpeg-static}"
VERSION=9.0.1
URL="https://ffmpeg.org/releases/ffmpeg-$VERSION.tar.xz"
WANT_SHA="cf38e0e28c7e5605942c4a77755349b0145804a397af37eb1fb4c77cb237f635"
STAMP="$DEST/.strikers-ffmpeg"

case "$(uname -s)" in
MINGW*|MSYS*|CYGWIN*) WINDOWS=1; AVCODEC_LIB=avcodec.lib ;;
*)                    WINDOWS=0; AVCODEC_LIB=libavcodec.a ;;
esac

if [ -f "$DEST/lib/$AVCODEC_LIB" ] && [ -f "$STAMP" ] &&
   [ "$(cat "$STAMP")" = "$WANT_SHA  $URL" ]; then
    echo "==> ffmpeg $VERSION already built in $DEST"
    exit 0
fi

sha256_of() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | awk '{print $1}'
    elif command -v shasum >/dev/null 2>&1; then
        shasum -a 256 "$1" | awk '{print $1}'
    fi
}

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT INT TERM

echo "==> fetching $URL"
curl -fsSL --retry 3 --retry-delay 5 -o "$WORK/ffmpeg.tar.xz" "$URL"
GOT_SHA=$(sha256_of "$WORK/ffmpeg.tar.xz")
if [ "$GOT_SHA" != "$WANT_SHA" ]; then
    echo "build-ffmpeg.sh: sha256 mismatch for $URL" >&2
    echo "    expected $WANT_SHA" >&2
    echo "    got      ${GOT_SHA:-nothing (no sha256sum or shasum on PATH)}" >&2
    exit 1
fi
tar -xJf "$WORK/ffmpeg.tar.xz" -C "$WORK"

# --disable-autodetect keeps host libraries (VA-API, zlib, iconv) out; x86-64 needs nasm except on Windows.
if [ "$WINDOWS" = 1 ]; then
    set -- --toolchain=msvc --target-os=win64 --arch=x86_64 \
        --cc="${CC:-clang-cl}" --ld=lld-link --ar=llvm-ar \
        --disable-x86asm --extra-cflags=-MT
else
    set -- --cc="${CC:-cc}" --enable-pic
fi

cd "$WORK/ffmpeg-$VERSION"
./configure --prefix="$DEST" "$@" \
    --enable-static --disable-shared \
    --disable-autodetect --disable-programs --disable-doc --disable-network \
    --disable-avformat --disable-avfilter --disable-avdevice \
    --disable-swscale --disable-swresample \
    --disable-everything --enable-decoder=thp \
    > "$WORK/configure.log" 2>&1 || {
    echo "build-ffmpeg.sh: configure failed:" >&2
    tail -n 40 "$WORK/configure.log" >&2
    exit 1
}

JOBS=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)
make -j"$JOBS" > "$WORK/make.log" 2>&1 || {
    echo "build-ffmpeg.sh: make failed:" >&2
    tail -n 40 "$WORK/make.log" >&2
    exit 1
}

rm -rf "$DEST"
make install > /dev/null
printf '%s  %s\n' "$WANT_SHA" "$URL" > "$STAMP"

echo "==> ffmpeg $VERSION (thp decoder only) in $DEST"
ls "$DEST/lib" | sed 's/^/    /'
