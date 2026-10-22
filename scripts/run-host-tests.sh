#!/usr/bin/env bash
# Run the host reference binaries headless; logs and BMPs land in logs/host/.
set -uo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="$REPO/logs/host"
mkdir -p "$OUT"
cd "$OUT"
status=0
for v in stock opt; do
    for t in sdl_smoke rt_soft rt_micro sw_exact rt_scene; do
        SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout 600 "$REPO/build/host-tests-$v/$t" > "${t}_$v.stdout" 2>&1 || status=1
        grep -E "^RESULT" "${t}_$v.log" || { echo "${t}_$v: no RESULT line"; status=1; }
    done
done
exit $status
