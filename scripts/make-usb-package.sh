#!/usr/bin/env bash
# Prepare a FAT32-ready folder for a first real-hardware test (wLaunchELF + USB).
#
#   scripts/make-usb-package.sh <your data.zip> [<out-dir>] [--no-build] [--image <file.img>]
#   scripts/make-usb-package.sh <your data.zip> [<out-dir>] --from-release release/VVVVVV-PS2-<ID>
#   ... [--folder NAME] [--label TEXT]
#   scripts/make-usb-package.sh --no-data [<out-dir>] [--from-release ... | --no-build] [--folder ...]
#
# --no-data: lay out the folder without game data (no data.zip needed). The
# tester (or anyone) then runs prepare-ps2-usb.py <their data.zip> <folder>,
# which adds exactly what the full mode adds (public tester releases).
# --folder NAME: package folder name instead of VVVVVV (e.g. VVVVVV-P31DEV for an
# experimental build next to a release on the same stick: saves and logs are
# written next to the ELF, so separate folders never share them).
# --label TEXT: a banner at the top of README-PS2.txt (e.g. "EXPERIMENTAL").
#
# --from-release packages a frozen release candidate (scripts/freeze-release.sh)
# exactly as it is: its stripped ELFs, game files and README; nothing is
# built. The copy of this script inside a release folder works the same way
# (it then finds repack-data-zip.py and the release next to itself).
#
# Output (default build/usb-package/, which git ignores):
#   <out>/VVVVVV/VVVVVV.elf          the game (music streamed)
#   <out>/VVVVVV/VVVVVV-nomusic.elf  same game without music (fallback)
#   <out>/VVVVVV/fs_smoke.elf        storage self-test, run this first
#   <out>/VVVVVV/data-ps2.zip        made from YOUR data.zip by repack-data-zip.py
#   <out>/VVVVVV/lang/ fonts/        open-source game files from the VVVVVV repo
#   <out>/VVVVVV/README-PS2.txt      how to launch, what to send back
#   <out>/VVVVVV/fs_smoke_expect.txt CRC32s that fs_smoke.elf re-reads from the stick
#   <out>/VVVVVV/optional/           bench.txt, overlay.txt: copy next to the ELF to enable
#   <out>/VVVVVV/MANIFEST.sha256     checksums of everything above
# Copy the VVVVVV folder to the root of a FAT32 USB stick.
#
# Nothing is downloaded and nothing from data.zip goes into the repository:
# the input archive is only read, data-ps2.zip is written under <out>.
# Needs no console: the folder can be checked here (FAT32 name/size rules and
# checksums are verified at the end; --image builds a FAT32 disk image when
# mtools is installed).
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/.." && pwd)"

DATA_ZIP= OUT= BUILD=1 IMAGE= RELEASE= FOLDER=VVVVVV LABEL= NODATA=0
if [[ -f "$HERE/SHA256SUMS" && -d "$HERE/bin" ]]; then RELEASE="$HERE"; fi # running the copy inside a release
while (($#)); do
    case "$1" in
        --no-build) BUILD=0 ;;
        --from-release) RELEASE="$(cd "$2" && pwd)"; shift ;;
        --image) IMAGE="$2"; shift ;;
        --folder) FOLDER="$2"; shift ;;
        --label) LABEL="$2"; shift ;;
        --no-data) NODATA=1 ;;
        -*) echo "unknown option $1" >&2; exit 2 ;;
        *) if [[ -z "$DATA_ZIP" && $NODATA == 0 ]]; then DATA_ZIP="$1"; elif [[ -z "$OUT" ]]; then OUT="$1"; else exit 2; fi ;;
    esac
    shift
done
if ((NODATA)); then
    # positional arguments before --no-data: the first one is the out-dir
    if [[ -n "$DATA_ZIP" && -z "$OUT" ]]; then OUT="$DATA_ZIP"; DATA_ZIP=; fi
    [[ -z "$DATA_ZIP" ]] || { echo "--no-data takes no data.zip" >&2; exit 2; }
    [[ -z "$IMAGE" ]] || { echo "--image needs game data" >&2; exit 2; }
else
    [[ -n "$DATA_ZIP" && -f "$DATA_ZIP" ]] || { sed -n 2,4p "$0" >&2; exit 2; }
fi

# The input must be the game's data.zip with everything the PS2 build loads
# (prepare-ps2-usb.py holds the list; testers use the same tool).
if ((!NODATA)); then python3 -I "$HERE/prepare-ps2-usb.py" --check "$DATA_ZIP"; fi
if [[ -n "$RELEASE" ]]; then
    BUILD=0
    (cd "$RELEASE" && sha256sum -c --quiet SHA256SUMS) || { echo "$RELEASE: SHA256SUMS mismatch, release modified?" >&2; exit 2; }
    # the release's own copy of this script: write next to the caller, the
    # release folder itself is read-only
    if [[ "$HERE" == "$RELEASE" ]]; then OUT="${OUT:-$(pwd)/usb-package}"; fi
fi
OUT="${OUT:-$REPO/build/usb-package}"
[[ "$FOLDER" =~ ^[A-Za-z0-9._-]{1,32}$ ]] || { echo "--folder: 1-32 of [A-Za-z0-9._-]" >&2; exit 2; }
PKG="$OUT/$FOLDER"

if ((BUILD)); then
    "$REPO/scripts/build-vvvvvv-ps2.sh"
    "$REPO/scripts/ps2-env.sh" /bin/sh -euc '
        cmake -S tests/ps2 -B build/ps2-tests-opt -DCMAKE_TOOLCHAIN_FILE=$PS2DEV/share/ps2dev.cmake \
            -DCMAKE_BUILD_TYPE=Release -DPS2_SDL_VARIANT=opt \
            -DSDL2_DIR=/src/build/sdl2-ps2-opt/prefix/lib/cmake/SDL2 >/dev/null
        cmake --build build/ps2-tests-opt --target fs_smoke >/dev/null'
fi
if [[ -z "$RELEASE" ]]; then
    for f in "$REPO/out/vvvvvv-ps2/VVVVVV.elf" "$REPO/out/vvvvvv-ps2/VVVVVV-nomusic.elf" "$REPO/build/ps2-tests-opt/fs_smoke.elf"; do
        [[ -f "$f" ]] || { echo "missing $f (run without --no-build)" >&2; exit 2; }
    done
fi

rm -rf "${PKG:?}"
mkdir -p "$PKG"
if [[ -n "$RELEASE" ]]; then
    cp --no-preserve=mode "$RELEASE"/bin/*.elf "$PKG/"
    cp -r --no-preserve=mode "$RELEASE/files/lang" "$RELEASE/files/fonts" "$RELEASE/SOURCE.txt" "$PKG/"
    {
        echo "release $(basename "$RELEASE"); unstripped ELFs (for symbolising crash addresses):"
        (cd "$RELEASE" && grep ' ./symbols/' SHA256SUMS)
    } >> "$PKG/SOURCE.txt"
    README="$RELEASE/README-PS2.txt"
else
# Stripped ELFs (about half the size; wLaunchELF only loads the program
# segments anyway). The unstripped originals in out/ and build/ have the same
# code addresses, so crash PCs from the console can be symbolised with them.
rel_pkg="$(realpath --relative-to="$REPO" "$PKG")"
case "$rel_pkg" in ../*) echo "the output folder must be inside the repository for the build container" >&2; exit 2 ;; esac
"$REPO/scripts/ps2-env.sh" /bin/sh -euc "
    for e in out/vvvvvv-ps2/VVVVVV.elf out/vvvvvv-ps2/VVVVVV-nomusic.elf build/ps2-tests-opt/fs_smoke.elf; do
        mips64r5900el-ps2-elf-strip -o $rel_pkg/\$(basename \$e) \$e
    done"
cp -r "$REPO/out/vvvvvv-ps2/lang" "$REPO/out/vvvvvv-ps2/fonts" "$REPO/out/vvvvvv-ps2/SOURCE.txt" "$PKG/"
{
    echo "unstripped ELFs (for symbolising crash addresses):"
    (cd "$REPO" && sha256sum out/vvvvvv-ps2/VVVVVV.elf out/vvvvvv-ps2/VVVVVV-nomusic.elf build/ps2-tests-opt/fs_smoke.elf)
} >> "$PKG/SOURCE.txt"
README="$REPO/docs/ps2-usb-readme.txt"
fi
if [[ -n "$LABEL" ]]; then
    { printf '*** %s ***\n*** build: %s, folder: %s ***\n\n' "$LABEL" "$(head -1 "$PKG/SOURCE.txt")" "$FOLDER"
      cat "$README"; } > "$PKG/README-PS2.txt"
else
    cp --no-preserve=mode "$README" "$PKG/README-PS2.txt"
fi
mkdir -p "$PKG/optional"
printf 'preset=hardware\n' > "$PKG/optional/bench.txt"
printf 'overlay on at start (L3+R3 toggles it)\n' > "$PKG/optional/overlay.txt"
# data-ps2.zip (repack-data-zip.py), fs_smoke_expect.txt, MANIFEST.sha256,
# FAT32 rules and a checksum pass: the same tool testers run on their own
# data.zip (scripts/prepare-ps2-usb.py).
if ((NODATA)); then
    echo "laid out without game data: $PKG (next: prepare-ps2-usb.py <data.zip> $PKG)"
    exit 0
fi
python3 -I "$HERE/prepare-ps2-usb.py" "$DATA_ZIP" "$PKG"

if [[ -n "$IMAGE" ]]; then
    command -v mcopy >/dev/null || { echo "--image needs mtools (mcopy)" >&2; exit 2; }
    size_mb=$(( $(du -sm "$PKG" | cut -f1) + 40 ))
    rm -f "$IMAGE"
    mkfs.fat -C -F 32 -n VVVVVVPS2 "$IMAGE" $((size_mb * 1024)) >/dev/null
    MTOOLS_SKIP_CHECK=1 mcopy -s -i "$IMAGE" "$PKG" ::/
    fsck.fat -n "$IMAGE"
    echo "FAT32 image: $IMAGE"
fi
echo "ready: $PKG  (copy the $FOLDER folder to the root of a FAT32 USB stick)"
