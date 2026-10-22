#!/usr/bin/env bash
# Maintainer helper: regenerate patches/sdl2/0003-0007 from the edited tree in
# build/sdl2-ps2-opt/src. Each file belongs to exactly one patch; every patch is
# a diff against SDL 2.32.10 + all lower-numbered patches.
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
W="$REPO/build/sdl2-ps2-opt/src"
T="$REPO/build/host-sdl2/SDL2-2.32.10.tar.gz"
P="$REPO/patches/sdl2"
declare -A FILES=(
  [0003-ps2-sw-render-stats-and-dead-clears]="src/render/software/SDL_render_sw.c"
  [0004-ps2-same-format-8888-fast-paths]="src/video/SDL_blit.c src/render/software/SDL_blendfillrect.c"
  [0005-ps2-render-copy-without-softfloat-division]="src/render/SDL_render.c"
  [0006-ps2-window-framebuffer-in-texture-memory]="src/video/SDL_video.c src/render/ps2/SDL_render_ps2.c"
  [0007-ps2-main-boot-messages]="src/main/ps2/SDL_ps2_main.c"
)
base=$(mktemp -d)
trap 'rm -rf "$base"' EXIT
tar -xzf "$T" -C "$base" --strip-components=1
for p in "$P"/0001-*.patch "$P"/0002-*.patch; do patch -d "$base" -p1 -s < "$p"; done
for name in $(printf '%s\n' "${!FILES[@]}" | sort); do
    out="$P/$name.patch"
    : > "$out"
    for f in ${FILES[$name]}; do
        (cd "$W" && diff -u --label "a/$f" --label "b/$f" "$base/$f" "$f") >> "$out" || true
    done
    patch -d "$base" -p1 -s < "$out"
    echo "$(wc -l < "$out") lines  $name.patch"
done
diff -rq "$base" "$W" && echo "patch series reproduces $W"
