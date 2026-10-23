#!/usr/bin/env bash
# Lay out a USB stick for the first hardware sessions (docs/ps2-hardware-test-plan.md):
#
#   [EXP_ID=P3.2-T1] scripts/make-hw-test-stick.sh <your data.zip> [<out-dir>]   (default build/usb-hw)
#
#   <out>/VVVVVV/         package A: the frozen release candidate H1-RC2, packaged
#                         from release/VVVVVV-PS2-H1-RC2 (nothing is rebuilt)
#   <out>/VVVVVV-P31DEV/  package B: the current development build (out/vvvvvv-ps2,
#                         must be built with PS2_BUILD_ID=$EXP_ID), labelled
#                         EXPERIMENTAL, with tagged benchmark configurations
#   <out>/GSPOC/          optional: the G1 GS/software prototypes (tests/ps2/gs_poc)
#
# Each package keeps its saves and logs in its own folder, so they never
# overwrite each other. Copy all three folders to the root of a FAT32 stick.
# No game data is committed or downloaded: data.zip is only read.
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DATA_ZIP="${1:?usage: make-hw-test-stick.sh <data.zip> [<out-dir>]}"
OUT="${2:-$REPO/build/usb-hw}"
REL="$REPO/release/VVVVVV-PS2-H1-RC2"
EXP_ID="${EXP_ID:-P3.2-T1}"

grep -q "^build $EXP_ID\$" "$REPO/out/vvvvvv-ps2/SOURCE.txt" || {
    echo "out/vvvvvv-ps2 is not a $EXP_ID build: PS2_BUILD_ID=$EXP_ID scripts/build-vvvvvv-ps2.sh" >&2; exit 2; }
if grep -q '^WARNING' "$REPO/out/vvvvvv-ps2/SOURCE.txt"; then
    grep '^WARNING' "$REPO/out/vvvvvv-ps2/SOURCE.txt" >&2; exit 2
fi
for e in build/ps2-tests-opt/fs_smoke.elf build/ps2-tests-opt/gs_poc_sw.elf build/ps2-tests-opt/gs_poc_gs.elf; do
    [[ -f "$REPO/$e" ]] || { echo "missing $e: scripts/build-ps2-tests.sh" >&2; exit 2; }
done
rm -rf "${OUT:?}/VVVVVV" "$OUT/VVVVVV-P31DEV" "$OUT/GSPOC"

# A: frozen release, untouched
"$REPO/scripts/make-usb-package.sh" "$DATA_ZIP" "$OUT" --from-release "$REL"

# B: experimental build in its own folder
"$REPO/scripts/make-usb-package.sh" "$DATA_ZIP" "$OUT" --no-build --folder VVVVVV-P31DEV \
    --label "$EXP_ID - EXPERIMENTAL, not a release candidate"
B="$OUT/VVVVVV-P31DEV"
printf 'preset=hardware\ntag=normal\n' > "$B/optional/bench-normal.txt"
printf 'preset=hardware\nflip=1\ntag=flip\n' > "$B/optional/bench-flip.txt"
printf 'preset=hardware\nflip=1\nopts=0xffffffef\ntag=flipoff\n' > "$B/optional/bench-flipoff.txt"
rm -f "$B/optional/bench.txt"
python3 -I "$REPO/scripts/prepare-ps2-usb.py" --manifest-only "$B"

# G1 prototypes (stripped), optional
G="$OUT/GSPOC"
mkdir -p "$G"
rel_g="$(realpath --relative-to="$REPO" "$G")"
case "$rel_g" in ../*) echo "the output folder must be inside the repository for the build container" >&2; exit 2 ;; esac
"$REPO/scripts/ps2-env.sh" /bin/sh -euc "
    for e in gs_poc_sw gs_poc_gs; do mips64r5900el-ps2-elf-strip -o $rel_g/\$e.elf build/ps2-tests-opt/\$e.elf; done"
cp "$REPO/tests/ps2/gs_poc/README-GSPOC.txt" "$G/"
(cd "$G" && sha256sum gs_poc_sw.elf gs_poc_gs.elf README-GSPOC.txt > MANIFEST.sha256)

(cd "$REL" && sha256sum -c --quiet SHA256SUMS) && echo "release H1-RC2 unchanged"
cmp -s "$OUT/VVVVVV/VVVVVV.elf" "$REL/bin/VVVVVV.elf" && echo "package A runs the frozen RC2 binary"
echo "stick layout ready in $OUT ($(du -sh "$OUT" | cut -f1)): copy VVVVVV, VVVVVV-P31DEV and GSPOC to the stick root"
