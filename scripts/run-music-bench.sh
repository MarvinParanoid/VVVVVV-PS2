#!/usr/bin/env bash
# P2b research benchmark in PCSX2: PhysicsFS-in-zip seek cost (deflated vs
# stored music blob) and stb_vorbis decode cost on the EE.
#
#   PCSX2_BIOS=/path/bios.bin [VVVVVV_DATA=/path/data.zip] scripts/run-music-bench.sh
#
# Needs a VVVVVV PS2 build first (scripts/build-vvvvvv-ps2.sh) for PhysicsFS.
# Uses your data.zip in place and a stored-music copy in build/ (never committed).
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$REPO/scripts/pcsx2-common.sh"
DATA_ZIP="${VVVVVV_DATA:-${XDG_CACHE_HOME:-$HOME/.cache}/vvvvvv-ps2/assets/data.zip}"
RUN="$REPO/build/music-bench-run"

"$REPO/scripts/ps2-env.sh" /bin/sh -euc '
    cmake -S tests/ps2 -B build/ps2-tests-opt -DCMAKE_TOOLCHAIN_FILE=$PS2DEV/share/ps2dev.cmake \
        -DCMAKE_BUILD_TYPE=Release -DPS2_SDL_VARIANT=opt \
        -DSDL2_DIR=/src/build/sdl2-ps2-opt/prefix/lib/cmake/SDL2 >/dev/null
    cmake --build build/ps2-tests-opt --target music_bench
'
mkdir -p "$RUN"
cp "$REPO/build/ps2-tests-opt/music_bench.elf" "$RUN/"
ln -sf "$(realpath "$DATA_ZIP")" "$RUN/data.zip"
[[ -f "$RUN/data-stored.zip" ]] || python3 -I "$REPO/scripts/repack-data-zip.py" "$DATA_ZIP" "$RUN/data-stored.zip"
rm -f "${RUN:?}/music_bench.log"
"$APPRUN" -datapath "$DATA" -batch -nogui -fastboot -earlyconsolelog -logfile "$RUN/emulog.txt" \
    -elf "$RUN/music_bench.elf" > "$RUN/pcsx2.stdout" 2>&1 &
pid=$!
for _ in $(seq 1 600); do
    sleep 1
    grep -q '^END' "$RUN/music_bench.log" 2>/dev/null && break
done
kill "$pid" 2>/dev/null || true
wait "$pid" 2>/dev/null || true
mkdir -p "$REPO/logs/pcsx2/music_bench"
cp "$RUN/music_bench.log" "$REPO/logs/pcsx2/music_bench/"
grep -E '^(SEEK|READ|DECODE)' "$RUN/music_bench.log"
