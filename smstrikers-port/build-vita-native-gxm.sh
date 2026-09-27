#!/bin/zsh
set -euo pipefail

ROOT="${0:A:h}"
BUILD_DIR="${ROOT}/build-vita-gxm-latest"
JOBS="${JOBS:-$(sysctl -n hw.ncpu)}"

cd "${ROOT}"

cmake -S . -B "${BUILD_DIR}" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=/usr/local/vitasdk/share/vita.toolchain.cmake \
  -DSTRIKERS_VITA=ON \
  -DSTRIKERS_AURORA=ON \
  -DSTRIKERS_VITA_NO_LOGS=ON \
  -DSTRIKERS_VITA_AUDIO_THREAD=ON \
  -DSTRIKERS_VITA_GX_THREAD=OFF \
  -DSTRIKERS_VITA_GXM_DIRECT_STREAM_WRITE=ON \
  -DSTRIKERS_VITA_GXM_DIRECT_DRAW_SUBMIT=ON \
  -DSTRIKERS_VITA_SHADER_PROFILE=SEALED \
  -DSTRIKERS_VITA_NATIVE_CMPR=OFF \
  -DSTRIKERS_VITA_NATIVE_GX_TEXTURES=ON \
  -DSTRIKERS_VITA_FORCE_60HZ=ON

# Force regeneration of the final Vita artifacts. Object files and static
# libraries stay cached, so only stale dependencies are rebuilt.
rm -f \
  "${BUILD_DIR}/strikers" \
  "${BUILD_DIR}/strikers.velf" \
  "${BUILD_DIR}/strikers_vita.self" \
  "${BUILD_DIR}/strikers_vita.vpk"

cmake --build "${BUILD_DIR}" --target strikers -j"${JOBS}"
cmake --build "${BUILD_DIR}" --target strikers-velf -j"${JOBS}"
cmake --build "${BUILD_DIR}" --target strikers_vita.self-self -j"${JOBS}"
cmake --build "${BUILD_DIR}" --target strikers_vita.vpk-vpk -j"${JOBS}"

echo
echo "Build complete:"
shasum -a 256 \
  "${BUILD_DIR}/strikers_vita.self" \
  "${BUILD_DIR}/strikers_vita.vpk"
ls -lh \
  "${BUILD_DIR}/strikers_vita.self" \
  "${BUILD_DIR}/strikers_vita.vpk"
