#!/usr/bin/env bash
# Fetch the pinned upstream VVVVVV source (SDL2 branch "2.4-updates") with its
# submodules, then apply this repo's PS2 patch series (patches/vvvvvv/*.patch).
# Result: build/vvvvvv-src (regenerated from scratch; never edit it by hand),
# plus the unpatched export in build/vvvvvv-pristine for regen-vvvvvv-patches.sh.
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
URL=https://github.com/TerryCavanagh/VVVVVV.git
COMMIT=79395fa47d35e1de5332e6b4a342249c46ac6fb9   # 2.4-updates head, 2026-10 (SDL2)
CACHE="${XDG_CACHE_HOME:-$HOME/.cache}/vvvvvv-ps2/vvvvvv-git"
DST="$REPO/build/vvvvvv-src"

if [[ ! -d "$CACHE/.git" ]]; then
    git clone --no-checkout --filter=blob:none "$URL" "$CACHE"
fi
git -C "$CACHE" fetch -q origin "$COMMIT" 2>/dev/null || git -C "$CACHE" fetch -q origin 2.4-updates
git -C "$CACHE" -c advice.detachedHead=false checkout -q -f "$COMMIT"
git -C "$CACHE" submodule update --init --recursive -q

rm -rf "$DST"
mkdir -p "$DST"
# Export tracked files of the superproject and submodules (no .git dirs).
git -C "$CACHE" ls-files -z --recurse-submodules | (cd "$CACHE" && xargs -0 cp --parents -t "$DST")
echo "upstream $COMMIT" > "$DST/.ps2-source"
git -C "$CACHE" submodule status --recursive >> "$DST/.ps2-source"
rm -rf "$REPO/build/vvvvvv-pristine"
cp -a "$DST" "$REPO/build/vvvvvv-pristine"

shopt -s nullglob
for p in "$REPO"/patches/vvvvvv/*.patch; do
    echo "applying $(basename "$p")"
    patch -d "$DST" -p1 --forward -s < "$p"
    echo "patch $(basename "$p") $(sha256sum "$p" | cut -c1-16)" >> "$DST/.ps2-source"
done
echo "source ready: $DST"
