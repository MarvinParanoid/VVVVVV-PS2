#!/usr/bin/env bash
# Run an ELF from a USB-package folder (or a copy of one) in PCSX2, the way it
# runs from the stick: its own folder is the working directory (host: here,
# mass: on a console), and logs/saves are written there.
#
#   PCSX2_BIOS=/path/bios.bin scripts/run-package-pcsx2.sh <folder> <elf> <log tag> <seconds> [pad scenario]
#
# - Stops after <seconds>, or earlier once vvvvvv_ps2.log says "BENCH done".
# - Removes a previous run's vvvvvv_ps2.log and bench_results.txt from
#   <folder> first; copies *.log, bench_results*.txt, bench_*.csv, prof_*.txt and shot_*.ppm (as .png) to
#   logs/pcsx2/<log tag>/.
# - Use a COPY of build/usb-package/VVVVVV: runs add files (savedata/, logs).
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
[[ $# -ge 4 ]] || { sed -n 2,11p "$0" >&2; exit 2; }
DIR="$(realpath "$1")" ELF="$2" TAG="$3" SECS="$4" PAD="${5:-}"
[[ -f "$DIR/$ELF" ]] || { echo "no $DIR/$ELF" >&2; exit 2; }
if [[ -n "$PAD" ]]; then export PCSX2_VIRTUAL_PAD=1; fi
source "$REPO/scripts/pcsx2-common.sh"
# outputs of a previous run in this folder would end the wait at once
rm -f "$DIR"/vvvvvv_ps2*.log "$DIR"/bench_results*.txt
LOGS="$REPO/logs/pcsx2/$TAG"
mkdir -p "$LOGS"
rm -f "${LOGS:?}"/*
pad_pid=
if [[ -n "$PAD" ]]; then
    python3 -I "$REPO/scripts/virtual-pad.py" "$PAD" "$SECS" > "$LOGS/pad.log" 2>&1 &
    pad_pid=$!
    sleep 2
fi
"$APPRUN" -datapath "$DATA" -batch -nogui -fastboot -earlyconsolelog -logfile "$LOGS/emulog.txt" \
    -elf "$DIR/$ELF" > "$LOGS/pcsx2.stdout" 2>&1 &
pid=$!
end=$((SECONDS + SECS))
while kill -0 "$pid" 2>/dev/null && (( SECONDS < end )); do
    sleep 1
    if grep -q '^BENCH done' "$DIR/vvvvvv_ps2.log" 2>/dev/null; then sleep 3; break; fi
done
kill "$pid" 2>/dev/null || true
wait "$pid" 2>/dev/null || true
if [[ -n "$pad_pid" ]]; then kill "$pad_pid" 2>/dev/null || true; wait "$pad_pid" 2>/dev/null || true; fi
for f in "$DIR"/*.log "$DIR"/bench_results*.txt "$DIR"/bench_*.csv "$DIR"/prof_*.txt; do
    [[ -f "$f" ]] && cp "$f" "$LOGS/"
done
for f in "$DIR"/shot_*.ppm "$DIR"/golden_*.ppm; do
    [[ -f "$f" ]] || continue
    python3 -I -c 'import sys
from PIL import Image
Image.open(sys.argv[1]).save(sys.argv[2])' "$f" "$LOGS/$(basename "${f%.ppm}").png" 2>/dev/null || cp "$f" "$LOGS/"
done
echo "[package] $ELF from $DIR, logs in $LOGS"
