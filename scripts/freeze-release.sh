#!/usr/bin/env bash
# Freeze a hardware-test release candidate from the current build outputs.
#
#   PS2_BUILD_ID=H1-RC1 scripts/build-vvvvvv-ps2.sh --refetch     # build first, with the ID
#   scripts/freeze-release.sh H1-RC1                               # -> release/VVVVVV-PS2-H1-RC1/
#
# Copies the ELFs exactly as built (symbols/, for crash symbolisation) and
# stripped (bin/, what goes on the stick), the open-source game files (lang,
# fonts), source identity, checksums and instructions. No game assets: the
# user's data.zip is added only when a USB package is made from the release
# (scripts/make-usb-package.sh <data.zip> --from-release <dir>).
# Refuses to touch an existing release folder: a frozen RC is never rebuilt.
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ID="${1:?usage: freeze-release.sh <build id>}"
REL="$REPO/release/VVVVVV-PS2-$ID"
[[ ! -e "$REL" ]] || { echo "$REL exists; a frozen release is not modified" >&2; exit 2; }
grep -q "^build $ID\$" "$REPO/out/vvvvvv-ps2/SOURCE.txt" || {
    echo "out/vvvvvv-ps2 was not built with PS2_BUILD_ID=$ID" >&2; exit 2; }
[[ -f "$REPO/build/ps2-tests-opt/fs_smoke.elf" ]] || { echo "build fs_smoke first (scripts/make-usb-package.sh does)" >&2; exit 2; }

mkdir -p "$REL/bin" "$REL/symbols" "$REL/files"
cp "$REPO/out/vvvvvv-ps2/VVVVVV.elf" "$REPO/out/vvvvvv-ps2/VVVVVV-nomusic.elf" "$REPO/build/ps2-tests-opt/fs_smoke.elf" "$REL/symbols/"
rel="$(realpath --relative-to="$REPO" "$REL")"
"$REPO/scripts/ps2-env.sh" /bin/sh -euc "
    for e in VVVVVV.elf VVVVVV-nomusic.elf fs_smoke.elf; do
        mips64r5900el-ps2-elf-strip -o $rel/bin/\$e $rel/symbols/\$e
    done"
cp -r "$REPO/out/vvvvvv-ps2/lang" "$REPO/out/vvvvvv-ps2/fonts" "$REL/files/"
cp "$REPO/out/vvvvvv-ps2/SOURCE.txt" "$REL/SOURCE.txt"
cp "$REPO/scripts/make-usb-package.sh" "$REPO/scripts/repack-data-zip.py" "$REL/"
cp "$REPO/docs/ps2-hardware-first-boot.md" "$REL/FIRST-BOOT.md"
cp "$REPO/docs/ps2-usb-readme.txt" "$REL/README-PS2.txt"
cat > "$REL/BUILD.md" <<EOT
# VVVVVV-PS2-$ID

Frozen $(date -u +%Y-%m-%d) from the sources listed in SOURCE.txt.

Make the USB folder from your own data.zip (nothing is built):

    ./make-usb-package.sh /path/to/data.zip            # -> ./usb-package/VVVVVV/
    # or from the repository:
    scripts/make-usb-package.sh /path/to/data.zip --from-release release/VVVVVV-PS2-$ID

Rebuild bit-identical binaries from the repository (checks against symbols/):

    scripts/build-sdl2-ps2.sh
    PS2_BUILD_ID=$ID scripts/build-vvvvvv-ps2.sh --refetch
    sha256sum out/vvvvvv-ps2/VVVVVV.elf out/vvvvvv-ps2/VVVVVV-nomusic.elf

Symbolise a crash address from the exception screen:

    scripts/ps2-profile.py release/VVVVVV-PS2-$ID/symbols/VVVVVV.elf --addr <EPC> <RA>

See FIRST-BOOT.md for the test procedure and docs/ps2-h1-release-candidate.md
in the repository for the validation results.
EOT
(cd "$REL" && find . -type f ! -name SHA256SUMS -print0 | sort -z | xargs -0 sha256sum > SHA256SUMS)
chmod -R a-w "$REL"
echo "frozen: $REL"
sed -n '/bin\/\|symbols\//p' "$REL/SHA256SUMS"
