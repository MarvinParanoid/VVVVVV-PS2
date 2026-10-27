# Building from source

How to build the PS2 ELFs, make a USB folder and run the game in PCSX2. To change the patches, see [CONTRIBUTING.md](../CONTRIBUTING.md).

## Requirements

**Host system.** Linux x86-64. Builds have been tested on Arch Linux. Other distributions should work but are untested; macOS and Windows are not supported.

**Tools:** `bash`, `git`, `curl`, `patch`, `python3` (3.12+ for the bubblewrap backend; tested with 3.14), coreutils (`sha256sum`), and **either**:
- [bubblewrap](https://github.com/containers/bubblewrap) (`bwrap`): tested. The toolchain image is unpacked rootless into `~/.cache/vvvvvv-ps2/`;
- or Docker: used by CI ([releasing.md](releasing.md#continuous-integration)), where it has built successfully; not tested on a developer machine.

**Disk and network.** About 2 GB of disk and a network connection for the first build: ~1.2 GB for the cached toolchain image and VVVVVV clone, ~0.7 GB in the repository's `build/` and `out/`. The build downloads:
- the [ps2dev toolchain image](https://github.com/ps2dev/ps2dev/pkgs/container/ps2dev) (pinned by digest);
- SDL 2.32.10 (pinned by SHA-256);
- VVVVVV (pinned commit).

**For running in PCSX2** (optional):
- **your own PS2 BIOS dump** (none is included or downloaded);
- Pillow (`python3-pil`) for screenshots;
- `/dev/uinput` access for the scripted virtual pad.

PCSX2 v2.8.2 itself is downloaded and checked by SHA-256.

## Build the game

```sh
git clone https://github.com/MarvinParanoid/VVVVVV-PS2.git
cd VVVVVV-PS2
scripts/build-sdl2-ps2.sh              # SDL 2.32.10 + patches/sdl2  -> build/sdl2-ps2-opt/prefix
scripts/build-vvvvvv-ps2.sh --refetch  # VVVVVV + patches/vvvvvv     -> out/vvvvvv-ps2/
```

**Output** in `out/vvvvvv-ps2/`:
- `VVVVVV.elf` and `VVVVVV-nomusic.elf`, a fallback build without music;
- the open-source `lang/` and `fonts/` folders;
- `SOURCE.txt`, which lists the exact upstream revisions and patch hashes.

Set `PS2_BUILD_ID=<name>` to stamp your own build ID; the default is `dev`. `SOURCE.txt` also records the SHA-256 of both ELFs. `scripts/build-ps2-tests.sh` writes `build/ps2-tests-opt/BUILD-STAMP.txt` for `fs_smoke.elf`; release packaging checks both records.

## Reproducibility

What has been shown, on the build host (bubblewrap backend):
- a clean worktree of tag `H3-FIRST-HARDWARE-PLAYABLE` (`b1e087c`) rebuilds the console-tested `P3.4.3-SAVEPAD` ELFs bit for bit (`VVVVVV.elf` `66f0568e…`);
- `H4-RC1` built from clean checkouts of `784581b` and `a435397` gives identical ELFs (`VVVVVV.elf` `3fac5284…`).

What has **not** been shown: identity across machines and between the bubblewrap and Docker backends. That comparison is pending ([releasing.md](releasing.md#continuous-integration)).

The toolchain image is pinned by digest, SDL by SHA-256 and VVVVVV by commit. The host build tools inside the image (`apk add cmake make …`) are not pinned, so they can drift; `BUILD-INFO.txt` of every package lists their versions.

**Reproducing a CI build locally.** From a clean checkout of the same commit:

```sh
id=ci-$(git rev-parse --short=12 HEAD)
scripts/build-sdl2-ps2.sh
PS2_BUILD_ID=$id scripts/build-vvvvvv-ps2.sh --refetch
scripts/build-ps2-tests.sh
EXP_ID=$id scripts/make-public-test-release.sh --ci "$id"   # -> build/public/VVVVVV-PS2-$id.zip
scripts/build-host-tests.sh && scripts/run-host-tests.sh     # the host job
```

Then compare the ELF hashes in `out/vvvvvv-ps2/SOURCE.txt` with the "Unstripped ELFs" lines of the run's `BUILD-INFO.txt`.

**How the patches are applied:**
- `scripts/fetch-vvvvvv.sh` exports VVVVVV at the pinned commit into `build/vvvvvv-src/` and applies `patches/vvvvvv/`.
- `scripts/build-sdl2-ps2.sh` does the same for SDL.
- All build products stay in `build/` and `out/`, which git ignores.

## Continuous integration

Every push to `main` and every pull request is built by GitHub Actions with these same scripts. What runs, what it uploads and how release candidates are made: [releasing.md](releasing.md).

## Tests

- **Host:** `scripts/build-host-tests.sh && scripts/run-host-tests.sh`. Renderer checks against the host SDL, plus `fs_semantics`, which emulates ps2sdk's USB error codes against the real PhysicsFS.
- **PS2 test programs:** `scripts/build-ps2-tests.sh` builds them into `build/ps2-tests-opt/`:
  - `fs_smoke` (USB self-test);
  - renderer checks;
  - the file-system probes `stat_probe`, `write_probe`, `physfs_probe`, `io_bisect`, `sdl_bisect` that found the console bugs ([architecture.md](architecture.md#files-saves-and-the-ps2sdk-seams)).

## Make a USB folder

```sh
scripts/make-usb-package.sh /path/to/your/data.zip   # -> build/usb-package/VVVVVV/
```

See [installing.md](installing.md) for the game data, copying to the stick and launching.

## Running in PCSX2

```sh
PCSX2_BIOS=/path/to/your/bios.bin VVVVVV_DATA=/path/to/data-ps2.zip RUN_SECONDS=120 PCSX2_GUI=1 \
    scripts/run-vvvvvv-pcsx2.sh
```

- To make `data-ps2.zip` without making a full package, run `scripts/repack-data-zip.py data.zip data-ps2.zip`.
- Logs and screenshots go to `logs/pcsx2/`.
- Other options (benchmarks, scripted pad input, soak runs) are documented at the top of each `scripts/run-*.sh`. Benchmark comparison with golden frames is described in [CONTRIBUTING.md](../CONTRIBUTING.md).

## Repository layout

```
patches/vvvvvv/   PS2 changes to VVVVVV (applied to the pinned upstream commit)
patches/sdl2/     PS2 changes to SDL 2.32.10
scripts/          build, packaging, PCSX2 runners, benchmark and profiling tools
tests/ps2/        standalone PS2 test programs (renderer checks, fs_smoke storage test)
docker/           Dockerfile for the pinned ps2dev build image
docs/             user and developer documentation; archive/ holds the phase reports;
                  release/, tester/ and ps2-usb-readme.txt are texts copied into packages
LICENSES/         license texts (see LICENSE.md)
```

The documents refer to `logs/`, `release/` and `build/` folders. Those are local outputs of the scripts and are not part of the repository.
