#!/usr/bin/env bash
# Supplementary runtime check in Play! (HLE BIOS, needs no BIOS dump).
# Play! is NOT the reference emulator for this project: its IOP/GS/SPU2 are less
# accurate than PCSX2's, so results here are indicative only. Use run-pcsx2.sh
# (and hardware) for acceptance.
#
#   scripts/run-play.sh sdl_smoke [rt_soft ...]
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PLAY_VER=0.72
PLAY_FILE='Play!-8de4a71f-x86_64.AppImage'
PLAY_SHA256=5523586c7d89f90e0e6eb9cbc70008d1d6ef3d38656fea809517d3f5fd3004d3
CACHE="${XDG_CACHE_HOME:-$HOME/.cache}/vvvvvv-ps2/play-$PLAY_VER"
PHOME="$CACHE/home"
VFS_HOST="$PHOME/.config/Play Data Files/vfs/host"
TIMEOUT="${PLAY_TIMEOUT:-240}"

[[ $# -ge 1 ]] || { echo "usage: $0 <test> [test...]" >&2; exit 2; }
if [[ ! -x "$CACHE/squashfs-root/AppRun" ]]; then
    mkdir -p "$CACHE"
    curl -fL -o "$CACHE/play.AppImage" "https://purei.org/downloads/play/stable/$PLAY_VER/$PLAY_FILE"
    echo "$PLAY_SHA256  $CACHE/play.AppImage" | sha256sum -c -
    chmod +x "$CACHE/play.AppImage"
    (cd "$CACHE" && ./play.AppImage --appimage-extract >/dev/null)
fi

# Play! pauses the whole VM (incl. the emulated EE timer) while its window is
# unfocused (ui.pausewhenfocuslost, default true). Timings stay valid but runs
# take far longer in wall time, so keep the VM running in the background.
CONFIG_XML="$PHOME/.config/Play Data Files/config.xml"
if [[ -f "$CONFIG_XML" ]]; then
    sed -i 's|<Preference Name="ui.pausewhenfocuslost" Type="boolean" Value="true" />|<Preference Name="ui.pausewhenfocuslost" Type="boolean" Value="false" />|' "$CONFIG_XML"
fi

status=0
for t in "$@"; do
    ELF="$REPO/out/ps2-tests/$t.elf"
    RUN="$REPO/logs/play/$t"
    [[ -f "$ELF" ]] || { echo "missing $ELF" >&2; exit 2; }
    mkdir -p "$RUN" "$VFS_HOST"
    rm -f "$RUN"/*.log "$RUN"/*.bmp "$RUN"/play.stdout "$VFS_HOST/$t.log" "$VFS_HOST/$t"_*.bmp
    echo "[play] $t: running (timeout ${TIMEOUT}s)"
    HOME="$PHOME" XDG_CONFIG_HOME="$PHOME/.config" XDG_DATA_HOME="$PHOME/.local/share" \
        QT_QPA_PLATFORM=xcb "$CACHE/squashfs-root/AppRun" --elf "$ELF" > "$RUN/play.stdout" 2>&1 &
    pid=$!
    deadline=$((SECONDS + TIMEOUT))
    teardown_deadline=
    while kill -0 "$pid" 2>/dev/null; do
        if [[ -f "$VFS_HOST/$t.log" ]]; then
            if grep -q '^END' "$VFS_HOST/$t.log"; then
                sleep 2
                break
            fi
            if [[ -z "$teardown_deadline" ]] && grep -q '^teardown: start' "$VFS_HOST/$t.log"; then
                teardown_deadline=$((SECONDS + 30))
            fi
        fi
        if [[ -n "$teardown_deadline" ]] && (( SECONDS >= teardown_deadline )); then
            echo "[play] $t: results complete, but SDL teardown did not finish within 30s" | tee -a "$RUN/play.stdout"
            break
        fi
        if (( SECONDS >= deadline )); then
            echo "[play] $t: TIMEOUT after ${TIMEOUT}s" | tee -a "$RUN/play.stdout"
            status=1
            break
        fi
        sleep 1
    done
    kill "$pid" 2>/dev/null || true
    wait "$pid" 2>/dev/null || true
    cp "$VFS_HOST/$t.log" "$RUN/" 2>/dev/null || echo "[play] $t: no log written"
    cp "$VFS_HOST/$t"_*.bmp "$RUN/" 2>/dev/null || true
    grep -E '^RESULT' "$RUN/$t.log" 2>/dev/null || status=1
done
exit $status
