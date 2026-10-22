#!/usr/bin/env bash
# Prepare a FAT32-ready folder for a first real-hardware test (wLaunchELF + USB).
#
#   scripts/make-usb-package.sh <your data.zip> [<out-dir>] [--no-build] [--image <file.img>]
#   scripts/make-usb-package.sh <your data.zip> [<out-dir>] --from-release release/VVVVVV-PS2-<ID>
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

DATA_ZIP= OUT= BUILD=1 IMAGE= RELEASE=
if [[ -f "$HERE/SHA256SUMS" && -d "$HERE/bin" ]]; then RELEASE="$HERE"; fi # running the copy inside a release
while (($#)); do
    case "$1" in
        --no-build) BUILD=0 ;;
        --from-release) RELEASE="$(cd "$2" && pwd)"; shift ;;
        --image) IMAGE="$2"; shift ;;
        -*) echo "unknown option $1" >&2; exit 2 ;;
        *) if [[ -z "$DATA_ZIP" ]]; then DATA_ZIP="$1"; elif [[ -z "$OUT" ]]; then OUT="$1"; else exit 2; fi ;;
    esac
    shift
done
[[ -n "$DATA_ZIP" && -f "$DATA_ZIP" ]] || { sed -n 2,4p "$0" >&2; exit 2; }

# The input must be the game's data.zip with everything the PS2 build loads.
python3 -I - "$DATA_ZIP" <<'EOF'
import sys, zipfile
graphics = ("covered elephant ending entcolours flipgamecomplete fliplevelcomplete flipsprites gamecomplete "
            "levelcomplete minimap site site2 site3 site4 sprites teleporter tiles tiles2 tiles3").split()
sounds = ("blip blip2 coin combine countdown crash crashing crew1 crew2 crew3 crew4 crew5 crew6 crumble gamesaved "
          "go hurt jump jump2 newrecord preteleport rescue save souleyeminijingle teleport terminal trophy vanish").split()
needed = ["vvvvvvmusic.vvv", "VVVVVV.png"] + [f"graphics/{g}.png" for g in graphics] + [f"sounds/{x}.wav" for x in sounds]
try:
    names = set(zipfile.ZipFile(sys.argv[1]).namelist())
except zipfile.BadZipFile as e:
    sys.exit(f"{sys.argv[1]}: not a zip archive ({e})")
missing = [n for n in needed if n not in names]
if missing:
    sys.exit(f"{sys.argv[1]}: not a complete VVVVVV data.zip, missing {len(missing)}: {', '.join(missing[:8])}")
print(f"data.zip: all {len(needed)} required entries present ({len(names)} entries)")
EOF
if [[ -n "$RELEASE" ]]; then
    BUILD=0
    (cd "$RELEASE" && sha256sum -c --quiet SHA256SUMS) || { echo "$RELEASE: SHA256SUMS mismatch, release modified?" >&2; exit 2; }
    # the release's own copy of this script: write next to the caller, the
    # release folder itself is read-only
    if [[ "$HERE" == "$RELEASE" ]]; then OUT="${OUT:-$(pwd)/usb-package}"; fi
fi
OUT="${OUT:-$REPO/build/usb-package}"
PKG="$OUT/VVVVVV"

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
    REPACK="$RELEASE/repack-data-zip.py"
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
REPACK="$REPO/scripts/repack-data-zip.py"
README="$REPO/docs/ps2-usb-readme.txt"
fi
python3 -I "$REPACK" "$DATA_ZIP" "$PKG/data-ps2.zip"
cp --no-preserve=mode "$README" "$PKG/README-PS2.txt"
mkdir -p "$PKG/optional"
printf 'preset=hardware\n' > "$PKG/optional/bench.txt"
printf 'overlay on at start (L3+R3 toggles it)\n' > "$PKG/optional/overlay.txt"
# CRC32s for fs_smoke.elf: whole small files plus the ELF, and fixed 32 KB
# chunks spread over data-ps2.zip (exercises seeking in the 63 MB file).
python3 -I - "$PKG" <<'EOF'
import os, sys, zlib
pkg = sys.argv[1]
out = ["# fs_smoke.elf re-reads these from the USB stick and compares CRC32s"]
for rel in ["README-PS2.txt", "SOURCE.txt", "fonts/font.png", "lang/en/meta.xml", "VVVVVV.elf"]:
    data = open(os.path.join(pkg, rel), "rb").read()
    out.append(f"file {rel} {len(data)} {zlib.crc32(data):08x}")
zp = os.path.join(pkg, "data-ps2.zip")
size = os.path.getsize(zp)
with open(zp, "rb") as f:
    for i in range(24):
        off = 0 if i == 0 else size - 32768 if i == 23 else (size // 23) * i + (i * 4099) % 32768
        f.seek(off)
        data = f.read(32768)
        out.append(f"chunk data-ps2.zip {off} {len(data)} {zlib.crc32(data):08x}")
open(os.path.join(pkg, "fs_smoke_expect.txt"), "w").write("\n".join(out) + "\n")
EOF
(cd "$PKG" && find . -type f ! -name MANIFEST.sha256 -print0 | sort -z | xargs -0 sha256sum > MANIFEST.sha256)

# FAT32 rules: files < 4 GiB, no characters FAT forbids, no two names that
# differ only in case, paths within 255 characters.
python3 -I - "$PKG" <<'EOF'
import os, sys
root = sys.argv[1]
bad, seen, total = [], {}, 0
for d, dirs, files in os.walk(root):
    for n in dirs + files:
        p = os.path.join(d, n)
        rel = os.path.relpath(p, os.path.dirname(root))
        if any(c in n for c in '<>:"\\|?*') or n.endswith((" ", ".")):
            bad.append(f"name not allowed on FAT: {rel}")
        key = os.path.join(d, n).lower()
        if key in seen:
            bad.append(f"names differ only in case: {rel} / {seen[key]}")
        seen[key] = rel
        if len(rel) > 255:
            bad.append(f"path longer than 255: {rel}")
        if os.path.isfile(p):
            size = os.path.getsize(p)
            total += size
            if size >= 4 << 30:
                bad.append(f"file over 4 GiB: {rel}")
print(f"package: {len(seen)} entries, {total / 1048576:.1f} MiB")
for b in bad:
    print("FAT32 problem:", b)
sys.exit(1 if bad else 0)
EOF

if [[ -n "$IMAGE" ]]; then
    command -v mcopy >/dev/null || { echo "--image needs mtools (mcopy)" >&2; exit 2; }
    size_mb=$(( $(du -sm "$PKG" | cut -f1) + 40 ))
    rm -f "$IMAGE"
    mkfs.fat -C -F 32 -n VVVVVVPS2 "$IMAGE" $((size_mb * 1024)) >/dev/null
    MTOOLS_SKIP_CHECK=1 mcopy -s -i "$IMAGE" "$PKG" ::/
    fsck.fat -n "$IMAGE"
    echo "FAT32 image: $IMAGE"
fi
(cd "$PKG" && sha256sum -c --quiet MANIFEST.sha256) && echo "checksums OK"
echo "ready: $PKG  (copy the VVVVVV folder to the root of a FAT32 USB stick)"
