#!/usr/bin/env bash
# Build the public, ASSET-FREE tester archive (no game data inside):
#
#   [EXP_ID=P3.2-T1] scripts/make-public-test-release.sh <release name>   e.g. H2-TEST1
#   EXP_ID=<build id> scripts/make-public-test-release.sh --ci <name>     CI test build
#
#   build/public/VVVVVV-PS2-<name>/        the tree that goes into the zip
#     README.txt TESTING.txt BUILD-INFO.txt SHA256SUMS
#     LICENSE-VVVVVV.md THIRD-PARTY-NOTICES.txt
#     tools/prepare-ps2-usb.py tools/repack-data-zip.py
#     usb/VVVVVV/          H1-RC2 (the frozen release candidate's binaries)
#     usb/VVVVVV-P31DEV/   the experimental build $EXP_ID (out/vvvvvv-ps2)
#     usb/GSPOC/           optional G1 prototypes
#   build/public/VVVVVV-PS2-<name>.zip     deterministic zip of that tree
#   build/public/VVVVVV-PS2-<name>-symbols/  unstripped ELFs (crash symbolisation;
#                                            NOT for distribution)
#
# The usb/ folders are laid out by make-usb-package.sh --no-data, i.e. by the
# same code as the developer's stick; testers complete them with their own
# data.zip via tools/prepare-ps2-usb.py. scripts/check-package.py refuses
# anything that looks like game data, a BIOS, a secret or a build cache, and
# then checks the finished zip.
#
# --ci: the archive for one CI build (.github/workflows/ci.yml, docs/ci.md):
# only usb/VVVVVV-CI/, the build $EXP_ID from out/vvvvvv-ps2, with
# README-CI.txt. No frozen release candidate and no GSPOC: a CI archive never
# contains binaries it did not build.
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CI=0
if [[ "${1:-}" == "--ci" ]]; then CI=1; shift; fi
NAME="${1:?usage: make-public-test-release.sh [--ci] <release name, e.g. H2-TEST1>}"
[[ "$NAME" =~ ^[A-Za-z0-9._-]{1,24}$ ]] || { echo "release name: 1-24 of [A-Za-z0-9._-]" >&2; exit 2; }
EXP_ID="${EXP_ID:-P3.2-T1}"
REL="$REPO/release/VVVVVV-PS2-H1-RC2"
PUB="$REPO/build/public"
T="$PUB/VVVVVV-PS2-$NAME"
SYM="$PUB/VVVVVV-PS2-$NAME-symbols"

grep -q "^build $EXP_ID\$" "$REPO/out/vvvvvv-ps2/SOURCE.txt" || {
    echo "out/vvvvvv-ps2 is not a $EXP_ID build: PS2_BUILD_ID=$EXP_ID scripts/build-vvvvvv-ps2.sh" >&2; exit 2; }
if grep -q '^WARNING' "$REPO/out/vvvvvv-ps2/SOURCE.txt"; then
    grep '^WARNING' "$REPO/out/vvvvvv-ps2/SOURCE.txt" >&2; exit 2
fi
if grep -q -- '-dirty$' "$REPO/out/vvvvvv-ps2/SOURCE.txt"; then
    echo "out/vvvvvv-ps2 was built from uncommitted changes; commit, rebuild, then package" >&2; exit 2
fi
if ((CI)); then NEEDED="fs_smoke"; DEV=VVVVVV-CI; else NEEDED="fs_smoke gs_poc_sw gs_poc_gs"; DEV=VVVVVV-P31DEV; fi
for e in $NEEDED; do
    [[ -f "$REPO/build/ps2-tests-opt/$e.elf" ]] || { echo "missing build/ps2-tests-opt/$e.elf: scripts/build-ps2-tests.sh" >&2; exit 2; }
done
if ((!CI)); then
    (cd "$REL" && sha256sum -c --quiet SHA256SUMS) || { echo "release H1-RC2 modified?" >&2; exit 2; }
fi
rm -rf "$T" "$SYM" "$T.zip"
mkdir -p "$T/usb" "$T/tools" "$SYM"
rel_t="$(realpath --relative-to="$REPO" "$T")"

# usb/ folders: same layout code as the developer stick, without game data
if ((CI)); then
    "$REPO/scripts/make-usb-package.sh" --no-data "$T/usb" --no-build --folder "$DEV" \
        --label "CI build $EXP_ID - development build, not a release candidate"
else
    "$REPO/scripts/make-usb-package.sh" --no-data "$T/usb" --from-release "$REL"
    "$REPO/scripts/make-usb-package.sh" --no-data "$T/usb" --no-build --folder "$DEV" \
        --label "$EXP_ID - EXPERIMENTAL, not a release candidate"
fi
B="$T/usb/$DEV"
printf 'preset=hardware\ntag=normal\n' > "$B/optional/bench-normal.txt"
printf 'preset=hardware\nflip=1\ntag=flip\n' > "$B/optional/bench-flip.txt"
printf 'preset=hardware\nflip=1\nopts=0xffffffef\ntag=flipoff\n' > "$B/optional/bench-flipoff.txt"
rm -f "$B/optional/bench.txt"
"$REPO/scripts/ps2-env.sh" /bin/sh -euc "cat /usr/local/ps2dev/ps2sdk/LICENSE > $rel_t/.ps2sdk-license"
if ((!CI)); then
    G="$T/usb/GSPOC"
    mkdir -p "$G"
    "$REPO/scripts/ps2-env.sh" /bin/sh -euc "
        for e in gs_poc_sw gs_poc_gs; do mips64r5900el-ps2-elf-strip -o $rel_t/usb/GSPOC/\$e.elf build/ps2-tests-opt/\$e.elf; done"
    cp "$REPO/tests/ps2/gs_poc/README-GSPOC.txt" "$G/"
fi

# tools, docs, licences
cp "$REPO/scripts/prepare-ps2-usb.py" "$REPO/scripts/repack-data-zip.py" "$T/tools/"
if ((CI)); then
    cp "$REPO/docs/tester/README-CI.txt" "$T/README.txt"
else
    cp "$REPO/docs/tester/README.txt" "$T/README.txt"
    cp "$REPO/docs/tester/TESTING.txt" "$T/TESTING.txt"
fi
cp "$REPO/LICENSES/VVVVVV-Source-Code-License.md" "$T/LICENSE-VVVVVV.md"
{
    echo "Third-party software in the PS2 ELF files of this archive"
    echo "========================================================="
    echo
    echo "The ELF files are statically linked. The components below keep their own"
    echo "licences; their notices follow. VVVVVV itself: see LICENSE-VVVVVV.md."
    echo
    notice() { printf '\n----------------------------------------------------------------------\n%s\n----------------------------------------------------------------------\n' "$1"; cat "$2"; }
    notice "SDL 2.32.10 (zlib licence), with this project's PS2 patches" "$REPO/build/sdl2-ps2-opt/src/LICENSE.txt"
    notice "PhysicsFS (zlib licence), with this project's PS2 platform patch" "$REPO/build/vvvvvv-src/third_party/physfs/LICENSE.txt"
    notice "FAudio (zlib licence)" "$REPO/build/vvvvvv-src/third_party/FAudio/LICENSE"
    notice "tinyxml2 (zlib licence)" "$REPO/build/vvvvvv-src/third_party/tinyxml2/LICENSE.txt"
    notice "lodepng (zlib licence)" "$REPO/build/vvvvvv-src/third_party/lodepng/LICENSE"
    notice "c-hashmap (BSD 3-Clause)" "$REPO/build/vvvvvv-src/third_party/c-hashmap/LICENSE"
    notice "SheenBidi (Apache License 2.0)" "$REPO/build/vvvvvv-src/third_party/SheenBidi/LICENSE"
    printf '\n----------------------------------------------------------------------\nstb_vorbis (inside FAudio): public domain, or MIT at your option\n----------------------------------------------------------------------\n'
    sed -n '/^This software is available under 2 licenses/,$p' "$REPO/build/vvvvvv-src/third_party/FAudio/src/stb_vorbis.h" | sed '/^\*\//,$d'
    notice "ps2sdk (Academic Free License 2.0; some components carry other licences, see https://github.com/ps2dev/ps2sdk). gsKit (https://github.com/ps2dev/gsKit) is also licensed under the Academic Free License 2.0, the same text" "$T/.ps2sdk-license"
    printf '\n----------------------------------------------------------------------\nOther runtime components (texts not included here; see the links)\n----------------------------------------------------------------------\n'
    echo "- newlib C library (ps2dev toolchain): BSD-style licences, https://sourceware.org/newlib/"
    echo "- GCC runtime (libgcc, libstdc++): GPL-3.0 with the GCC Runtime Library Exception,"
    echo "  https://www.gnu.org/licenses/gcc-exception-3.1.html"
    echo "- FatFs (in the USB file system driver): ChaN's FatFs licence (BSD-style), http://elm-chan.org/fsw/ff/"
} > "$T/THIRD-PARTY-NOTICES.txt"
rm -f "$T/.ps2sdk-license"

# build identity
if ((CI)); then
COMMIT="$(git -C "$REPO" rev-parse HEAD)"
{
    echo "VVVVVV-PS2 CI build $EXP_ID - unofficial development build, NOT validated on real PS2 hardware"
    echo
    echo "commit    $COMMIT"
    echo "source    https://github.com/MarvinParanoid/VVVVVV-PS2/tree/$COMMIT"
    if [[ -n "${GITHUB_RUN_ID:-}" ]]; then
        echo "workflow  ${GITHUB_SERVER_URL:-https://github.com}/${GITHUB_REPOSITORY:-}/actions/runs/$GITHUB_RUN_ID (attempt ${GITHUB_RUN_ATTEMPT:-1}, ${GITHUB_EVENT_NAME:-}, ${GITHUB_REF:-})"
    else
        echo "workflow  none (local build)"
    fi
    echo "toolchain ghcr.io/ps2dev/ps2dev@$(sed -n 's/^DIGEST="\(.*\)"$/\1/p' "$REPO/scripts/ps2-env.sh")"
    echo
    echo "usb/$DEV    build $EXP_ID"
    grep -E '^(build|repo|upstream) ' "$B/SOURCE.txt" | sed 's/^/              /'
    echo
    echo "Build tools in the toolchain container (apk):"
    "$REPO/scripts/ps2-env.sh" /bin/sh -c 'cat /.apk-versions.txt 2>/dev/null || apk info -v 2>/dev/null' |
        grep -E '^(build-base|cmake|make|gcc|binutils|musl|git|pkgconf|python3)-[0-9]' | sed 's/^/  /'
    echo
    echo "Stripped ELFs in this archive (sha256):"
    (cd "$T" && sha256sum usb/*/*.elf)
    echo
    echo "Unstripped ELFs (crash symbolisation; the CI run's symbols artifact, sha256):"
    (cd "$REPO" && sha256sum out/vvvvvv-ps2/VVVVVV.elf out/vvvvvv-ps2/VVVVVV-nomusic.elf build/ps2-tests-opt/fs_smoke.elf)
    echo
    echo "Full source identity (upstream commits, patch hashes): usb/$DEV/SOURCE.txt"
} > "$T/BUILD-INFO.txt"
else
{
    echo "VVVVVV-PS2 tester release $NAME - unofficial, NOT validated on real PS2 hardware"
    echo
    echo "usb/VVVVVV          frozen release candidate H1-RC2 (unchanged binaries)"
    grep -E '^(build|repo|upstream) ' "$T/usb/VVVVVV/SOURCE.txt" | sed 's/^/                    /'
    echo "usb/VVVVVV-P31DEV   experimental build $EXP_ID"
    grep -E '^(build|repo|upstream) ' "$T/usb/VVVVVV-P31DEV/SOURCE.txt" | sed 's/^/                    /'
    echo
    echo "Stripped ELFs in this archive (sha256):"
    (cd "$T" && sha256sum usb/*/*.elf)
    echo
    echo "Full source identity (upstream commits, patch hashes): usb/*/SOURCE.txt"
    echo "Source: https://github.com/MarvinParanoid/VVVVVV-PS2"
} > "$T/BUILD-INFO.txt"
fi

# unstripped ELFs for symbolising crash reports (kept locally, not in the zip)
mkdir -p "$SYM/$EXP_ID"
if ((!CI)); then
    mkdir -p "$SYM/H1-RC2"
    cp "$REL"/symbols/*.elf "$SYM/H1-RC2/"
fi
cp "$REPO/out/vvvvvv-ps2/VVVVVV.elf" "$REPO/out/vvvvvv-ps2/VVVVVV-nomusic.elf" "$REPO/build/ps2-tests-opt/fs_smoke.elf" "$SYM/$EXP_ID/"

# no game data, BIOS files, secrets or caches may be in here
python3 -I "$REPO/scripts/check-package.py" tree "$T"

(cd "$T" && find . -type f ! -name SHA256SUMS -print0 | LC_ALL=C sort -z | xargs -0 sha256sum > SHA256SUMS)

# deterministic zip: sorted entries, fixed timestamps, fixed modes
python3 -I - "$T" "$T.zip" <<'EOF'
import os, sys, zipfile
root, out = sys.argv[1], sys.argv[2]
base = os.path.basename(root)
entries = []
for d, dirs, files in os.walk(root):
    for n in files:
        p = os.path.join(d, n)
        entries.append((base + "/" + os.path.relpath(p, root).replace(os.sep, "/"), p))
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    for arc, p in sorted(entries, key=lambda e: e[0].encode()):
        info = zipfile.ZipInfo(arc, (1980, 1, 1, 0, 0, 0))
        info.compress_type = zipfile.ZIP_DEFLATED
        info.external_attr = (0o755 if arc.endswith(".py") else 0o644) << 16
        with open(p, "rb") as f:
            z.writestr(info, f.read(), compresslevel=9)
print(f"{out}: {len(entries)} files, {os.path.getsize(out) / 1048576:.1f} MiB")
EOF
if ((CI)); then
    python3 -I "$REPO/scripts/check-package.py" zip "$T.zip" --build "$EXP_ID" --commit "$COMMIT"
else
    python3 -I "$REPO/scripts/check-package.py" zip "$T.zip"
fi
(cd "$PUB" && sha256sum "$(basename "$T.zip")" > "$(basename "$T.zip").sha256")
sha256sum "$T.zip"
echo "public tester release: $T.zip (asset-free; testers add their own data.zip)"
echo "symbols (keep, do not publish): $SYM"
