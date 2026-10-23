#!/usr/bin/env bash
# Run the host reference binaries headless; logs and BMPs land in logs/host/.
# Exits non-zero if a gating test fails or prints no RESULT line.
# sdl_smoke runs for information only: it checks PS2 drivers (game
# controller, PS2 render driver vsync modes) and always reports 3 FAILs here.
set -uo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="$REPO/logs/host"
mkdir -p "$OUT"
cd "$OUT"
status=0
for v in stock opt; do
    tests="sdl_smoke rt_soft rt_micro sw_exact rt_scene"
    if [[ $v == opt ]]; then tests="$tests flip_exact"; fi # needs the patched SDL
    for t in $tests; do
        # flip_exact names its log without the variant
        log="${t}_$v.log"; [[ $t == flip_exact ]] && log="$t.log"
        rm -f "$log"
        SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout 600 "$REPO/build/host-tests-$v/$t" > "${t}_$v.stdout" 2>&1
        rc=$?
        line="$(grep -E "^RESULT" "$log" 2>/dev/null)"
        if [[ $t == sdl_smoke ]]; then
            echo "${line:-${t}_$v: no RESULT line} (informational)"
        elif [[ -z "$line" ]]; then
            echo "${t}_$v: no RESULT line (exit $rc)"; status=1
        else
            echo "$line"
            [[ $rc == 0 ]] || { echo "${t}_$v: FAILED (exit $rc)"; grep -E "^FAIL" "$log"; status=1; }
        fi
    done
done
exit $status
