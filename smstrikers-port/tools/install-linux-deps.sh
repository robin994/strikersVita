#!/bin/sh
# Build dependencies for a Linux build, from SDL3's README-linux list because Aurora builds SDL3
# from source: tools/install-linux-deps.sh

set -e

SUDO=
if [ "$(id -u)" != 0 ]; then SUDO=sudo; fi
. /etc/os-release

$SUDO apt-get update
$SUDO apt-get install -y --no-install-recommends \
    ca-certificates curl git gnupg xz-utils build-essential python3 python3-pip pkg-config ninja-build nasm \
    libasound2-dev libpulse-dev libaudio-dev libjack-dev libsndio-dev \
    libx11-dev libx11-xcb-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev \
    libxi-dev libxinerama-dev libxss-dev libxtst-dev libxkbcommon-dev \
    libdrm-dev libgbm-dev libgl1-mesa-dev libgles2-mesa-dev libegl1-mesa-dev \
    libwayland-dev wayland-protocols libdecor-0-dev \
    libdbus-1-dev libibus-1.0-dev libudev-dev libusb-1.0-0-dev

if [ "$VERSION_CODENAME" = jammy ]; then
    LLVM_KEY_FPR=6084F3CF814B57C1CF12EFD515CF4D18AF4F7421
    curl -fsSL https://apt.llvm.org/llvm-snapshot.gpg.key | gpg --dearmor > /tmp/llvm.gpg
    if ! gpg --show-keys --with-colons /tmp/llvm.gpg | grep -q "^fpr:*$LLVM_KEY_FPR:"; then
        echo "install-linux-deps.sh: apt.llvm.org key is not $LLVM_KEY_FPR" >&2
        exit 1
    fi
    $SUDO install -m 644 /tmp/llvm.gpg /usr/share/keyrings/llvm.gpg
    echo "deb [signed-by=/usr/share/keyrings/llvm.gpg] http://apt.llvm.org/jammy/ llvm-toolchain-jammy-18 main" |
        $SUDO tee /etc/apt/sources.list.d/llvm-18.list > /dev/null
    $SUDO apt-get update
    # g++-12 for its libstdc++: clang uses the newest GCC it finds, and 11's <ranges> is too old.
    $SUDO apt-get install -y --no-install-recommends clang-18 lld-18 g++-12
    $SUDO update-alternatives --install /usr/bin/clang clang /usr/bin/clang-18 100
    $SUDO update-alternatives --install /usr/bin/clang++ clang++ /usr/bin/clang++-18 100
    # Jammy's CMake 3.22 is below Aurora's minimum.
    $SUDO python3 -m pip install --no-cache-dir 'cmake==3.31.6'
else
    $SUDO apt-get install -y --no-install-recommends clang lld cmake
fi
