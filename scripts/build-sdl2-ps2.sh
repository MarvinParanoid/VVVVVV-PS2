#!/usr/bin/env bash
# Build SDL 2.32.10 for PS2 with this repo's patches (patches/sdl2/*.patch),
# configured like ps2sdk-ports does, into build/sdl2-ps2-opt/prefix.
# The image's stock SDL (ps2sdk-ports) stays untouched for A/B comparisons.
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SDL_VER=2.32.10
SDL_SHA256=5f5993c530f084535c65a6879e9b26ad441169b3e25d789d83287040a9ca5165
TARBALL="$REPO/build/host-sdl2/SDL2-$SDL_VER.tar.gz"
OUT="$REPO/build/sdl2-ps2-opt"

if [[ ! -f "$TARBALL" ]]; then
    mkdir -p "$(dirname "$TARBALL")"
    curl -fL -o "$TARBALL" "https://github.com/libsdl-org/SDL/releases/download/release-$SDL_VER/SDL2-$SDL_VER.tar.gz"
fi
echo "$SDL_SHA256  $TARBALL" | sha256sum -c -

rm -rf "$OUT/src" "$OUT/build" "$OUT/prefix"
mkdir -p "$OUT/src"
tar -xzf "$TARBALL" -C "$OUT/src" --strip-components=1
for p in "$REPO"/patches/sdl2/*.patch; do
    echo "applying $(basename "$p")"
    patch -d "$OUT/src" -p1 --forward < "$p"
done

"$REPO/scripts/ps2-env.sh" /bin/sh -euc '
    cmake -S build/sdl2-ps2-opt/src -B build/sdl2-ps2-opt/build -Wno-dev \
        -DCMAKE_TOOLCHAIN_FILE=$PS2DEV/share/ps2dev.cmake \
        -DCMAKE_INSTALL_PREFIX=/src/build/sdl2-ps2-opt/prefix \
        -DCMAKE_PREFIX_PATH=$PS2SDK/ports \
        -DBUILD_SHARED_LIBS=OFF -DCMAKE_BUILD_TYPE=RelWithDebInfo \
        -DCMAKE_POSITION_INDEPENDENT_CODE=OFF -DSDL_TESTS=OFF
    cmake --build build/sdl2-ps2-opt/build -j"$(nproc)"
    cmake --install build/sdl2-ps2-opt/build >/dev/null
'
echo "installed: $OUT/prefix"
