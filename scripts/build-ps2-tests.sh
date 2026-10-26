#!/usr/bin/env bash
# Build the P1 validation ELFs inside the pinned PS2DEV environment, twice:
#   *_stock.elf  against the image's unmodified SDL 2.32.10 (previous implementation)
#   *_opt.elf    against SDL 2.32.10 + patches/sdl2 (run scripts/build-sdl2-ps2.sh first)
# Output: out/ps2-tests/{sdl_smoke,rt_soft}_{stock,opt}.elf plus .size.txt and .map files.
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if [[ ! -f "$REPO/build/sdl2-ps2-opt/prefix/lib/cmake/SDL2/SDL2Config.cmake" ]]; then
    "$REPO/scripts/build-sdl2-ps2.sh"
fi

"$REPO/scripts/ps2-env.sh" /bin/sh -euc '
    mips64r5900el-ps2-elf-gcc --version | head -1
    for v in stock opt; do
        extra=""
        if [ "$v" = opt ]; then extra="-DSDL2_DIR=/src/build/sdl2-ps2-opt/prefix/lib/cmake/SDL2"; fi
        cmake -S tests/ps2 -B build/ps2-tests-$v \
            -DCMAKE_TOOLCHAIN_FILE=$PS2DEV/share/ps2dev.cmake \
            -DCMAKE_BUILD_TYPE=Release -DPS2_SDL_VARIANT=$v $extra
        cmake --build build/ps2-tests-$v -j"$(nproc)"
    done
    rm -f out/ps2-tests/*.elf out/ps2-tests/*.size.txt out/ps2-tests/*.map
    mkdir -p out/ps2-tests
    for v in stock opt; do
        for t in sdl_smoke rt_soft rt_micro sw_exact rt_scene; do
            cp build/ps2-tests-$v/${t}_$v.elf build/ps2-tests-$v/${t}_$v.size.txt build/ps2-tests-$v/${t}_$v.map out/ps2-tests/
        done
    done
    sha256sum out/ps2-tests/*.elf
'
# which commit fs_smoke.elf was built from (make-public-test-release.sh --release checks it)
{
    echo "repo $(git -C "$REPO" describe --always --long --dirty --abbrev=12)"
    (cd "$REPO/build/ps2-tests-opt" && sha256sum fs_smoke.elf) | sed 's/^/elf /'
} > "$REPO/build/ps2-tests-opt/BUILD-STAMP.txt"
