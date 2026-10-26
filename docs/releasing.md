# Release packaging

How an end-user release-candidate archive (`VVVVVV-PS2-<name>.zip`, e.g. `H4-RC1`) is built and checked, locally and in CI. Building it does **not** publish it. Publishing is a separate, manual decision; the release gates are in [h4-first-public-release-plan.md](h4-first-public-release-plan.md) §11, and the open licence items are in [license-compliance-matrix.md](license-compliance-matrix.md).

## What the archive contains

```
VVVVVV-PS2-<name>/
  README.txt                 end-user guide (docs/release/README.txt)
  BUILD-INFO.txt             build ID, commit, toolchain, ELF checksums, hardware-validation note
  SHA256SUMS                 every other file
  LICENSE-VVVVVV.md          VVVVVV Source Code License
  THIRD-PARTY-NOTICES.txt    notices of everything linked into the ELFs, incl. open items
  LICENSES/                  LGPL-2.0, LGPL-2.1, pthread-embedded COPYING, FatFs, newlib, zlib, MIT
  tools/                     prepare-ps2-usb.py, repack-data-zip.py
  usb/VVVVVV/                VVVVVV.elf, VVVVVV-nomusic.elf, fs_smoke.elf (stripped),
                             SOURCE.txt, README-PS2.txt, lang/, fonts/,
                             optional/overlay.txt, optional/bench.txt
```

There is no game data, no frozen or experimental build, no prototype, no test switch (`crashtest.txt`, `iodiag.txt` and the like), no log and no benchmark output. `scripts/check-package.py zip --release` enforces this with an allowlist. Users add their own `data.zip` with `tools/prepare-ps2-usb.py` (see the archive's `README.txt`).

The unstripped ELFs and `SOURCE.txt` go to `build/public/VVVVVV-PS2-<name>-symbols/<name>/`, for symbolising crash reports. They are not part of the archive.

## Rules the script enforces

`scripts/make-public-test-release.sh --release <name> [--build-root DIR]` stops unless all of these hold:

- `DIR/out/vvvvvv-ps2/SOURCE.txt` says `build <name>`. The ELFs were built with `PS2_BUILD_ID=<name>`, so an older build is never picked up.
- `SOURCE.txt` has no `WARNING` (stale tree) and no `-dirty` (uncommitted changes at build time).
- The revision in `SOURCE.txt` is the commit `DIR` has checked out now, and `DIR` has no uncommitted change to tracked files.
- `VVVVVV.elf` and `VVVVVV-nomusic.elf` have exactly the SHA-256 that `build-vvvvvv-ps2.sh` recorded in `SOURCE.txt` (`elf <sha256>  <file>` lines), and both contain the build ID `<name>`. A replaced or rebuilt ELF stops the script.
- `DIR/build/ps2-tests-opt/fs_smoke.elf` has the SHA-256 recorded in `BUILD-STAMP.txt` there (written by `build-ps2-tests.sh`), and that stamp names the same clean commit.

`DIR` defaults to the repository itself and must be inside it, because the build container only sees the repository. `BUILD-INFO.txt` also names the commit of the packaging scripts. If those had uncommitted changes, it says so ("NOT for publication").

**What this guarantees, and what it does not.** The archive contains the files that the build scripts produced and recorded in `DIR` at one clean commit, named by `BUILD-INFO.txt`. Nothing was swapped between building and packaging, and no older build was reused.

The check cannot detect someone deliberately editing both a file and its record. It also does not prove that the commit's source produced the ELF. That proof is a rebuild from the commit with a matching checksum: a clean checkout, or the CI run of the same commit.

For the H4 candidates, the plan requires such a rebuild comparison. `THIRD-PARTY-NOTICES.txt` separates the licence notices that are included (part A) from compliance items that are still open (part B).

## Reproducing locally

From a clean commit, either in the repository itself or in a separate worktree. The worktree lets you keep unrelated uncommitted files in the main tree:

```sh
git worktree add --detach build/rc/<name>-src <commit>
cd build/rc/<name>-src
scripts/build-sdl2-ps2.sh
PS2_BUILD_ID=<name> scripts/build-vvvvvv-ps2.sh --refetch
scripts/build-ps2-tests.sh
cd -
scripts/make-public-test-release.sh --release <name> --build-root build/rc/<name>-src
```

Output:
- `build/public/VVVVVV-PS2-<name>.zip` and its `.sha256`;
- the unpacked tree `build/public/VVVVVV-PS2-<name>/`;
- the symbols folder.

The zip is deterministic: sorted entries, fixed timestamps and modes. Two runs from the same build give the same SHA-256.

Checks after packaging:
1. `python3 -I scripts/check-package.py zip build/public/VVVVVV-PS2-<name>.zip --build <name> --commit <sha> --release`. The script runs this itself.
2. Follow the archive's `README.txt` on a PC: unzip, `sha256sum -c SHA256SUMS`, `tools/prepare-ps2-usb.py <data.zip> usb/VVVVVV` (must print `ready:`).
3. Copy `usb/VVVVVV` to a FAT32 USB stick. Then `sha256sum -c MANIFEST.sha256` inside the copied folder, after remounting the stick.
4. PCSX2 boot of the prepared folder: `scripts/run-package-pcsx2.sh <folder> VVVVVV.elf <tag> 45`. The log must reach `MILESTONE first frame` with no `WARN`/`ERROR` line.
5. On a console: the checklist in [h4-first-public-release-plan.md](h4-first-public-release-plan.md) §10.1.

## In CI

`.github/workflows/ci.yml`, job `ps2-build`:
- **Push of a tag `H<n>-RC<m>`, or a manual run with a release name:** builds with `PS2_BUILD_ID=<tag or name>` and runs `--release`.
- **Any other push or pull request:** builds the CI test archive (`--ci`, build ID `ci-<sha12>`) as before.

In both cases the zip, the metadata and the unstripped ELFs are uploaded as **workflow artifacts** (30 days). The workflow has no step that creates a GitHub release, and its token is read-only (`permissions: contents: read`).

A CI release-candidate build is not "hardware-tested" until a console session with that exact zip is recorded.

What CI cannot check: anything that needs `data.zip` (gameplay, music, golden frames), a BIOS (PCSX2 runs), or a console (USB/FatFs behaviour, timing, controllers).
