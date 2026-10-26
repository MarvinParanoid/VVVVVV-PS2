#!/usr/bin/env bash
# Maintainer helper: regenerate patches/vvvvvv/*.patch from the edited tree in
# build/vvvvvv-src against the pristine export in build/vvvvvv-pristine
# (scripts/fetch-vvvvvv.sh keeps both). Each file belongs to exactly one patch.
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
A="$REPO/build/vvvvvv-pristine" B="$REPO/build/vvvvvv-src" P="$REPO/patches/vvvvvv"
S=desktop_version/src
declare -A FILES=(
  [0001-physfs-ps2-platform]="third_party/physfs/src/physfs_platforms.h third_party/physfs/src/physfs_internal.h third_party/physfs/src/physfs_platform_ps2.c third_party/physfs/src/physfs_platform_posix.c"
  [0002-cmake-ps2-target]="desktop_version/CMakeLists.txt"
  [0003-ps2-platform-layer-and-hooks]="$S/PS2Platform.h $S/PS2Mkdir.h $S/PS2Platform.cpp $S/PS2Bench.h $S/PS2Bench.cpp $S/PS2Diag.h $S/PS2Diag.cpp $S/main.cpp $S/FileSystemUtils.cpp $S/ButtonGlyphs.cpp $S/Vlogging.c"
  [0004-music-streaming]="$S/Music.h $S/Music.cpp"
  [0005-ps2-render-optimisations]="$S/Graphics.h $S/Graphics.cpp $S/Screen.cpp $S/GraphicsResources.cpp $S/Game.cpp"
)
rm -f "$P"/*.patch
for name in $(printf '%s\n' "${!FILES[@]}" | sort); do
    out="$P/$name.patch"
    : > "$out"
    for f in ${FILES[$name]}; do
        a="$A/$f"; [[ -e "$a" ]] || a=/dev/null
        diff -Nu --label "a/$f" --label "b/$f" "$a" "$B/$f" >> "$out" || true
    done
    echo "$(wc -l < "$out") lines  $name.patch"
done
t=$(mktemp -d); trap 'rm -rf "$t"' EXIT
cp -a "$A/." "$t/"
for p in "$P"/*.patch; do patch -d "$t" -p1 -s --forward < "$p"; done
if diff -rq "$t" "$B" | grep -v 'InterimVersion\|\.ps2-source'; then
    echo "MISMATCH: some change is not covered by the patch series" >&2; exit 1
fi
echo "patch series reproduces $B"
