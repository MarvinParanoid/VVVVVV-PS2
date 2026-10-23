#!/usr/bin/env bash
# Run the PS2 build of VVVVVV in PCSX2 and collect evidence.
#
#   PCSX2_BIOS=/path/bios.bin [VVVVVV_DATA=/path/data-ps2.zip] [RUN_SECONDS=90] \
#       [PCSX2_GUI=1] [PCSX2_OSD_FPS=1] [VVVVVV_ELF=VVVVVV-nomusic.elf] [SOAK=1] \
#       [PAD_SCENARIO=newgame-explore] [LOG_TAG=name] scripts/run-vvvvvv-pcsx2.sh
#
# - VVVVVV_DATA: default ~/.cache/vvvvvv-ps2/assets/data-ps2.zip if present
#   (scripts/repack-data-zip.py), else data.zip (music then stays disabled).
# - SOAK=1: puts music_soak.txt next to the ELF -> scripted music soak test.
# - PAD_SCENARIO: drive pad 1 with scripts/virtual-pad.py (needs /dev/uinput).
# - BENCH="settle=4 measure=20 over30=1 scenes=tower,map": scene benchmark
#   (src/PS2Bench.cpp); the run ends at "BENCH done". bench_*.csv, prof_*.txt
#   (EE PC samples, see scripts/ps2-profile.py) and frame_*.png are collected.
#
# - FLAG_FILES="overlay storage_real crashtest snapshots": create <name>.txt
#   (<name>=<text> writes <text> into it, e.g. crashtest=badsp)
#   next to the ELF (switches read at start-up, see docs/ps2-h1-hardware-readiness.md).
# - PCSX2_OSD_FPS=1: PCSX2 on-screen FPS/VPS/speed overlay (in the window,
#   not in the GS snapshots).
# - Stages build/vvvvvv-run/ = out/vvvvvv-ps2/* + a symlink to your data.zip
#   (default ~/.cache/vvvvvv-ps2/assets/data.zip). data.zip is never copied
#   into the repo or the ELF.
# - Boots VVVVVV.elf from that folder with host: filesystem enabled, so the
#   game reads data.zip / lang / fonts and writes savedata/, vvvvvv_ps2.log and
#   shot_*.ppm (GS framebuffer snapshots) there.
# - Stops after RUN_SECONDS (the game never exits on its own); PCSX2_GUI=1
#   keeps the normal PCSX2 window/menu for interactive play instead.
# - Logs are copied to logs/pcsx2/VVVVVV/ (snapshots converted to PNG if
#   Pillow is available).
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ASSETS="${XDG_CACHE_HOME:-$HOME/.cache}/vvvvvv-ps2/assets"
if [[ -n "${PAD_SCENARIO:-}" ]]; then export PCSX2_VIRTUAL_PAD=1; fi
source "$REPO/scripts/pcsx2-common.sh"
if [[ -n "${VVVVVV_DATA:-}" ]]; then DATA_ZIP="$VVVVVV_DATA";
elif [[ -f "$ASSETS/data-ps2.zip" ]]; then DATA_ZIP="$ASSETS/data-ps2.zip";
else DATA_ZIP="$ASSETS/data.zip"; fi
ELF_NAME="${VVVVVV_ELF:-VVVVVV.elf}"
RUN_SECONDS="${RUN_SECONDS:-90}"
RUNDIR="$REPO/build/vvvvvv-run"
LOGS="$REPO/logs/pcsx2/${LOG_TAG:-VVVVVV}"

[[ -f "$REPO/out/vvvvvv-ps2/$ELF_NAME" ]] || { echo "build first: scripts/build-vvvvvv-ps2.sh" >&2; exit 2; }
[[ -f "$DATA_ZIP" ]] || { echo "data.zip not found at $DATA_ZIP (set VVVVVV_DATA)" >&2; exit 2; }

mkdir -p "$RUNDIR"
# fresh run folder, but keep savedata/ between runs unless RESET_SAVE=1.
# Benchmark runs always start without savedata (KEEP_SAVE=1 overrides): saved
# records (e.g. the Super Gravitron best time) are drawn in HUDs and would
# change golden frames from run to run (found in P3.1).
find "${RUNDIR:?}" -maxdepth 1 -mindepth 1 ! -name savedata -exec rm -rf {} +
if [[ "${RESET_SAVE:-0}" == 1 || ( -n "${BENCH+x}" && "${KEEP_SAVE:-0}" != 1 ) ]]; then rm -rf "${RUNDIR:?}/savedata"; fi
cp -r "$REPO/out/vvvvvv-ps2/." "$RUNDIR/"
ln -sf "$(realpath "$DATA_ZIP")" "$RUNDIR/$(basename "$DATA_ZIP")"
if [[ "${SOAK:-0}" == 1 ]]; then echo "scripted music soak test" > "$RUNDIR/music_soak.txt"; fi
if [[ -n "${BENCH+x}" ]]; then printf '%s\n' ${BENCH} > "$RUNDIR/bench.txt"; fi
for f in ${FLAG_FILES:-}; do echo "${f#*=}" > "$RUNDIR/${f%%=*}.txt"; done

mkdir -p "$LOGS"
rm -f "${LOGS:?}"/*
args=(-datapath "$DATA" -fastboot -earlyconsolelog -logfile "$LOGS/emulog.txt")
if [[ "${PCSX2_GUI:-0}" != 1 ]]; then args=(-batch -nogui "${args[@]}"); fi
echo "[vvvvvv] $ELF_NAME with $(basename "$DATA_ZIP"), ${RUN_SECONDS}s from $RUNDIR"
pad_pid=
if [[ -n "${PAD_SCENARIO:-}" ]]; then
    python3 -I "$REPO/scripts/virtual-pad.py" "$PAD_SCENARIO" "$RUN_SECONDS" > "$LOGS/pad.log" 2>&1 &
    pad_pid=$!
    sleep 2   # device must exist before PCSX2 enumerates SDL controllers
fi
"$APPRUN" "${args[@]}" -elf "$RUNDIR/$ELF_NAME" > "$LOGS/pcsx2.stdout" 2>&1 &
pid=$!
end=$((SECONDS + RUN_SECONDS))
start=$SECONDS
while kill -0 "$pid" 2>/dev/null && (( SECONDS < end )); do
    sleep 1
    if [[ -n "${BENCH+x}" ]] && grep -q '^BENCH done' "$RUNDIR/vvvvvv_ps2.log" 2>/dev/null; then sleep 2; break; fi
done
echo "wall_seconds=$((SECONDS - start))" > "$LOGS/wall.txt"
kill "$pid" 2>/dev/null || true
wait "$pid" 2>/dev/null || true
if [[ -n "$pad_pid" ]]; then kill "$pad_pid" 2>/dev/null || true; wait "$pad_pid" 2>/dev/null || true; fi

cp "$RUNDIR"/vvvvvv_ps2.log "$LOGS/" 2>/dev/null || echo "[vvvvvv] no vvvvvv_ps2.log written"
cp "$RUNDIR"/bench_*.csv "$RUNDIR"/bench_results*.txt "$RUNDIR"/prof_*.txt "$RUNDIR"/vvvvvv_ps2_*.log "$LOGS/" 2>/dev/null || true
for f in "$RUNDIR"/shot_*.ppm "$RUNDIR"/frame_*.ppm "$RUNDIR"/golden_*.ppm; do
    [[ -f "$f" ]] || continue
    python3 -I -c 'import sys
from PIL import Image
Image.open(sys.argv[1]).save(sys.argv[2])' "$f" "$LOGS/$(basename "${f%.ppm}").png" 2>/dev/null || cp "$f" "$LOGS/"
done
grep -E 'MILESTONE|MUSIC opened|SOAK start|music' "$LOGS/vvvvvv_ps2.log" 2>/dev/null | head -20 || true
