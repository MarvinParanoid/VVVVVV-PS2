#!/usr/bin/env bash
# Build VVVVVV for PS2 inside the pinned PS2DEV environment.
#
#   scripts/build-vvvvvv-ps2.sh            # uses build/vvvvvv-src if present
#   scripts/build-vvvvvv-ps2.sh --refetch  # re-export upstream + apply patches/vvvvvv
#
# Inputs : upstream VVVVVV 2.4-updates (scripts/fetch-vvvvvv.sh) + patches/vvvvvv,
#          SDL 2.32.10 + patches/sdl2 (scripts/build-sdl2-ps2.sh)
# Output : out/vvvvvv-ps2/VVVVVV.elf plus lang/ and fonts/ from the source tree.
#          No proprietary assets: data.zip must be supplied by the user and put
#          next to the ELF (see docs/archive/ps2-p2a-report.md).
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if [[ "${1:-}" == "--refetch" || ! -d "$REPO/build/vvvvvv-src" ]]; then
    "$REPO/scripts/fetch-vvvvvv.sh"
fi
if [[ ! -f "$REPO/build/sdl2-ps2-opt/prefix/lib/libSDL2.a" ]]; then
    "$REPO/scripts/build-sdl2-ps2.sh"
fi

# PS2_BUILD_ID (env, default "dev"): shown on the diagnostic screens and in the
# log, e.g. PS2_BUILD_ID=H1-RC1 for a release candidate.
BUILD_ID="${PS2_BUILD_ID:-dev}"
[[ "$BUILD_ID" =~ ^[A-Za-z0-9._-]{1,24}$ ]] || { echo "PS2_BUILD_ID: 1-24 of [A-Za-z0-9._-]" >&2; exit 2; }

# Two variants: the default (streamed music) and a music-disabled build kept
# for A/B measurements (VVVVVV-nomusic.elf). PS2_MUSIC is always passed
# explicitly so a stale CMake cache cannot change what gets built.
# GIT_CEILING_DIRECTORIES: VVVVVV's version.cmake embeds the git commit it
# finds; without this it finds this repository and every commit changes the ELF.
"$REPO/scripts/ps2-env.sh" /bin/sh -euc '
    export GIT_CEILING_DIRECTORIES=/src/build
    P=/src/build/sdl2-ps2-opt/prefix
    for variant in music nomusic; do
        if [ $variant = music ]; then dir=build/vvvvvv-ps2; opt=ON; else dir=build/vvvvvv-ps2-nomusic; opt=OFF; fi
        cmake -S build/vvvvvv-src/desktop_version -B $dir -Wno-dev \
            -DCMAKE_TOOLCHAIN_FILE=$PS2DEV/share/ps2dev.cmake \
            -DCMAKE_BUILD_TYPE=Release \
            -DREMOVE_ABSOLUTE_PATHS=ON \
            -DPS2_MUSIC=$opt \
            -DPS2_BUILD_ID="$1" \
            -DSDL2_INCLUDE_DIRS="$P/include/SDL2" \
            -DSDL2_LIBRARIES="$P/lib/libSDL2main.a;$P/lib/libSDL2.a;patches;gskit;dmakit;ps2_drivers;atomic;m"
        cmake --build $dir -j"$(nproc)"
        mips64r5900el-ps2-elf-size $dir/VVVVVV.elf
    done
' sh "$BUILD_ID"

OUT="$REPO/out/vvvvvv-ps2"
rm -rf "$OUT"
mkdir -p "$OUT/lang" "$OUT/fonts"
cp "$REPO/build/vvvvvv-ps2/VVVVVV.elf" "$OUT/"
cp "$REPO/build/vvvvvv-ps2-nomusic/VVVVVV.elf" "$OUT/VVVVVV-nomusic.elf"
# English only, Latin fonts + button glyphs only (CJK sheets exceed the GS
# texture limit and would cost ~33 MB at 32 bpp; audit R6)
cp -r "$REPO/build/vvvvvv-src/desktop_version/lang/en" "$OUT/lang/"
for f in font.png font.fontmeta buttons_8x8.png buttons_8x8.fontmeta \
         buttons_10x10.png buttons_10x10.fontmeta buttons_12x12.png buttons_12x12.fontmeta; do
    cp "$REPO/build/vvvvvv-src/desktop_version/fonts/$f" "$OUT/fonts/"
done
# Patch hashes are taken from patches/ now and compared with what the build
# tree was fetched with (build/vvvvvv-src/.ps2-source, written by --refetch)
# and what the SDL library was built from (prefix/.ps2-patches): a stale or
# hand-edited tree is reported instead of being labelled with wrong hashes.
hashes() { (cd "$1" && for p in *.patch; do echo "patch $p $(sha256sum "$p" | cut -c1-16)"; done); }
cur_v="$(hashes "$REPO/patches/vvvvvv")"
cur_s="$(hashes "$REPO/patches/sdl2")"
warn=()
[[ "$cur_v" == "$(grep '^patch ' "$REPO/build/vvvvvv-src/.ps2-source")" ]] ||
    warn+=("WARNING: build/vvvvvv-src was not fetched with the current patches/vvvvvv (stale or edited tree; rebuild with --refetch)")
[[ "$cur_s" == "$(cat "$REPO/build/sdl2-ps2-opt/prefix/.ps2-patches" 2>/dev/null)" ]] ||
    warn+=("WARNING: the SDL library was not built from the current patches/sdl2 (run scripts/build-sdl2-ps2.sh)")
{
    echo "build $BUILD_ID"
    # repository revision, "-dirty" with uncommitted changes
    if git -C "$REPO" rev-parse --git-dir >/dev/null 2>&1; then
        echo "repo $(git -C "$REPO" describe --always --long --dirty --abbrev=12)"
    fi
    grep -v '^patch ' "$REPO/build/vvvvvv-src/.ps2-source"
    echo "$cur_v"
    echo "SDL 2.32.10 + patches/sdl2:"
    echo "$cur_s"
    for w in "${warn[@]}"; do echo "$w"; done
    # the files this record belongs to (make-public-test-release.sh --release checks them)
    (cd "$OUT" && sha256sum VVVVVV.elf VVVVVV-nomusic.elf) | sed 's/^/elf /'
} > "$OUT/SOURCE.txt"
for w in "${warn[@]}"; do echo "$w" >&2; done
sha256sum "$OUT/VVVVVV.elf" "$OUT/VVVVVV-nomusic.elf"
echo "staged: $OUT (add data-ps2.zip there: scripts/repack-data-zip.py data.zip data-ps2.zip)"
