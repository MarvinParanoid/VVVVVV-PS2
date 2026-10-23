# Building from source

How to build the PS2 ELFs, make a USB folder and run the game in PCSX2. To change the patches, see [CONTRIBUTING.md](../CONTRIBUTING.md).

## Requirements

**Host system.** Linux x86-64. Builds have been tested on Arch Linux. Other distributions should work but are untested; macOS and Windows are not supported.

**Tools:** `bash`, `git`, `curl`, `patch`, `python3` (3.12+ for the bubblewrap backend; tested with 3.14), coreutils (`sha256sum`), and **either**:
- [bubblewrap](https://github.com/containers/bubblewrap) (`bwrap`): tested. The toolchain image is unpacked rootless into `~/.cache/vvvvvv-ps2/`;
- or Docker: supported by the scripts but **not yet tested**.

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

**Reproducibility.** Builds are bit-for-bit reproducible. A fresh clone built with an empty cache produced the same ELFs as release candidate `H1-RC2`. Set `PS2_BUILD_ID=<name>` to stamp your own build ID; the default is `dev`.

**How the patches are applied:**
- `scripts/fetch-vvvvvv.sh` exports VVVVVV at the pinned commit into `build/vvvvvv-src/` and applies `patches/vvvvvv/`.
- `scripts/build-sdl2-ps2.sh` does the same for SDL.
- All build products stay in `build/` and `out/`, which git ignores.

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
docs/             technical reports, hardware checklists, tester and on-stick READMEs
LICENSES/         license texts (see LICENSE.md)
```

The reports in `docs/` refer to `logs/`, `release/` and `build/` folders. Those are local outputs of the scripts and are not part of the repository.
