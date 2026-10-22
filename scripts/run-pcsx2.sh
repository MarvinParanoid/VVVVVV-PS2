#!/usr/bin/env bash
# Run a validation ELF in a pinned PCSX2 build and collect evidence.
#
#   PCSX2_BIOS=/path/to/your/bios.bin scripts/run-pcsx2.sh sdl_smoke [rt_soft ...]
#
# - PCSX2 v2.8.2 AppImage (sha256 pinned) is downloaded/extracted to ~/.cache.
# - An isolated PCSX2 data dir is used (never touches ~/.config/PCSX2).
# - The BIOS is YOUR dump (proprietary): it is referenced in place, never copied
#   into this repo.
# - Each ELF runs from logs/pcsx2/<test>/ with host: filesystem enabled, so the
#   test's <test>.log / BMP files land there next to PCSX2's emulog.txt.
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
[[ $# -ge 1 ]] || { echo "usage: $0 <test> [test...]" >&2; exit 2; }
source "$REPO/scripts/pcsx2-common.sh"

status=0
for t in "$@"; do
    ELF="$REPO/out/ps2-tests/$t.elf"
    RUN="$REPO/logs/pcsx2/$t"
    [[ -f "$ELF" ]] || { echo "missing $ELF (run scripts/build-ps2-tests.sh)" >&2; exit 2; }
    rm -rf "$RUN" && mkdir -p "$RUN"
    cp "$ELF" "$RUN/"
    echo "[pcsx2] $t: running (timeout ${TIMEOUT}s)"
    "$APPRUN" -datapath "$DATA" -batch -nogui -fastboot -earlyconsolelog \
        -logfile "$RUN/emulog.txt" -elf "$RUN/$t.elf" > "$RUN/pcsx2.stdout" 2>&1 &
    pid=$!
    deadline=$((SECONDS + TIMEOUT))
    teardown_deadline=
    while kill -0 "$pid" 2>/dev/null; do
        if [[ -f "$RUN/$t.log" ]]; then
            if grep -q '^END' "$RUN/$t.log"; then
                sleep 2
                break
            fi
            if [[ -z "$teardown_deadline" ]] && grep -q '^teardown: start' "$RUN/$t.log"; then
                teardown_deadline=$((SECONDS + 30))
            fi
        fi
        if [[ -n "$teardown_deadline" ]] && (( SECONDS >= teardown_deadline )); then
            echo "[pcsx2] $t: results complete, but SDL teardown did not finish within 30s" | tee -a "$RUN/pcsx2.stdout"
            break
        fi
        if (( SECONDS >= deadline )); then
            echo "[pcsx2] $t: TIMEOUT after ${TIMEOUT}s" | tee -a "$RUN/pcsx2.stdout"
            status=1
            break
        fi
        sleep 1
    done
    kill "$pid" 2>/dev/null || true
    wait "$pid" 2>/dev/null || true
    rm -f "$RUN/$t.elf"
    if [[ -f "$RUN/$t.log" ]]; then
        grep -E '^RESULT' "$RUN/$t.log" || status=1
    else
        echo "[pcsx2] $t: no $t.log written (check emulog.txt for EE/IOP console output)"
        status=1
    fi
done
exit $status
