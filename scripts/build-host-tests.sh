#!/usr/bin/env bash
# Host reference build: the same test sources against the same SDL release
# (2.32.10) built for the host, with only dummy/offscreen video and dummy audio.
#   stock: pristine tarball
#   opt:   tarball + patches/sdl2 (0001 is PS2-only and compiles out on the host;
#          0002's software-blit fast paths are generic and get exercised here)
# Purpose: prove test expectations / bit-exactness before trusting a PS2 result.
# It says nothing about PS2 behaviour or speed.
#
# Output: build/host-tests-{stock,opt}/; run with scripts/run-host-tests.sh
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SDL_VER=2.32.10
SDL_SHA256=5f5993c530f084535c65a6879e9b26ad441169b3e25d789d83287040a9ca5165
TARBALL="$REPO/build/host-sdl2/SDL2-$SDL_VER.tar.gz"

if [[ ! -f "$TARBALL" ]]; then
    mkdir -p "$(dirname "$TARBALL")"
    curl -fL -o "$TARBALL" "https://github.com/libsdl-org/SDL/releases/download/release-$SDL_VER/SDL2-$SDL_VER.tar.gz"
fi
echo "$SDL_SHA256  $TARBALL" | sha256sum -c -

for v in stock opt; do
    B="$REPO/build/host-sdl2-$v"
    stamp="$B/prefix/.patches"
    want="$v"
    if [[ $v == opt ]]; then want="$v $(cat "$REPO"/patches/sdl2/*.patch | sha256sum | cut -c1-16)"; fi
    if [[ ! -f "$stamp" || "$(cat "$stamp")" != "$want" ]]; then
        rm -rf "$B" && mkdir -p "$B/src"
        tar -xzf "$TARBALL" -C "$B/src" --strip-components=1
        if [[ $v == opt ]]; then
            for p in "$REPO"/patches/sdl2/*.patch; do patch -d "$B/src" -p1 --forward -s < "$p"; done
        fi
        cmake -S "$B/src" -B "$B/build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_INSTALL_PREFIX="$B/prefix" \
            -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TEST=OFF \
            -DSDL_X11=OFF -DSDL_WAYLAND=OFF -DSDL_KMSDRM=OFF -DSDL_OPENGL=OFF -DSDL_OPENGLES=OFF \
            -DSDL_VULKAN=OFF -DSDL_RPI=OFF -DSDL_DIRECTFB=OFF \
            -DSDL_PULSEAUDIO=OFF -DSDL_PIPEWIRE=OFF -DSDL_ALSA=OFF -DSDL_JACK=OFF -DSDL_SNDIO=OFF \
            -DSDL_IBUS=OFF -DSDL_DBUS=OFF -DSDL_HIDAPI=OFF >/dev/null
        cmake --build "$B/build" >/dev/null
        cmake --install "$B/build" >/dev/null
        echo "$want" > "$stamp"
    fi
    cmake -S "$REPO/tests/ps2" -B "$REPO/build/host-tests-$v" -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_PREFIX_PATH="$B/prefix" -DPS2_SDL_VARIANT=$v >/dev/null
    cmake --build "$REPO/build/host-tests-$v"
done
