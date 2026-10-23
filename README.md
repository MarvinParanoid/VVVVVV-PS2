# VVVVVV for PlayStation 2 (unofficial port)

An experimental, unofficial port of Terry Cavanagh's
[VVVVVV](https://github.com/TerryCavanagh/VVVVVV) to the Sony PlayStation 2,
built on SDL2 and the open-source [ps2dev](https://github.com/ps2dev) toolchain.

> [!IMPORTANT]
> **Real PS2 hardware compatibility has not yet been confirmed.** This port has only been tested in the PCSX2 emulator; it has not yet run on a real PlayStation 2.
> Everything listed as working below was observed in PCSX2 v2.8.2. Real hardware may behave differently, and may be slower. If you have a console, a [hardware test report](#reporting-hardware-test-results) is the most useful contribution right now.

> [!NOTE]
> **VVVVVV is a commercial game.** This repository contains no game graphics, sounds or music, and never will. You need your own `data.zip` ([Game data](#game-data)). If you enjoy the game, please buy it at [thelettervsixtim.es](https://thelettervsixtim.es).

## Status

**Current milestone:** first hardware tests. The tester release (`H2-TEST1`) contains two builds:
- the frozen release candidate **`H1-RC2`**, the baseline;
- the experimental build **`P3.2-T1`**, which adds the Flip Mode speed fix.

Both are validated only in PCSX2. [Testing on a real PS2](#testing-on-a-real-ps2-no-development-tools-needed) explains how to help.

| Area | PCSX2 v2.8.2 | Real PS2 |
|---|---|---|
| Boot from a USB stick folder, title screen | ✅ tested | ❓ untested |
| Gameplay with DualShock 2 | ✅ tested (emulated pad) | ❓ untested |
| Streamed music + sound effects | ✅ tested, 0 underruns | ❓ untested |
| Save / continue (to the USB stick) | ✅ tested | ❓ untested |
| Full game speed in normal rooms (29.4 ticks/s) | ✅ measured | ❓ unmeasured |
| Tower / Panic Room worst case | ⚠️ ~25 of 29.4 ticks/s (85%) | ❓ unmeasured |
| Flip Mode (upside-down option) | ✅ full speed in most scenes in `P3.2-T1`; slow in `H1-RC2` ([P3.1](docs/ps2-p3.1-software-renderer-edge-cases.md)) | ❓ unmeasured |
| Error screens, overlay, benchmark, `fs_smoke` | ✅ tested | ❓ untested |
| NTSC video output on a PAL console | — | ❓ untested |

PCSX2 does not model the PS2's CPU caches or memory latency. Its timings are probably **optimistic**, so do not read the numbers above as hardware performance.

## Features

What works in PCSX2:
- The full game in English: main game, time trials, Secret Lab and Super Gravitron, rendered by SDL2's software renderer and scaled to the screen by the GS.
- All music, streamed from the USB stick, with sound effects.
- DualShock 2 controls with PlayStation button glyphs in the menus.
- Saves (`settings.vvv`, `unlock.vvv`, quicksave and teleporter saves) in a `savedata/` folder next to the game on the stick.
- **Hardware-test diagnostics:**
  - a start-up progress screen;
  - fatal-error and CPU-exception screens with a build ID;
  - an on-screen performance overlay (L3+R3);
  - a hands-off benchmark;
  - `fs_smoke.elf`, a USB storage self-test.

## Known limitations

- **Not tested on real hardware** (see above).
- **NTSC only:** always 640×448 at 60 Hz, also on PAL consoles. Use a TV and cable that accept 60 Hz (RGB SCART, component). Over composite on a PAL-only TV the picture may be black and white or roll. There is no PAL mode yet.
- **No memory-card saves.** Saves go to the USB stick only, so the game folder must be writable.
- **USB mass storage only.** HDD, network and disc launches have not been tried.
- **English only.** The CJK font sheets do not fit the GS texture limit.
- **Slowdown in the worst scenes.** The Tower and Panic Room run at about 85% speed in PCSX2 when the camera chases the player. Normal tower climbing is close to full speed.
- **Music starts about 0.5 s** after a track change.
- `over30mode` (interpolated 30+ FPS) is off by default. It can be enabled in the options but costs speed.

## Building

### Requirements

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

### Build the game

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

### Game data

The PS2 build needs VVVVVV's `data.zip`, which contains the graphics, sounds and music. It is **not** part of this repository. Either:
- copy `data.zip` from your purchased copy of VVVVVV, from the game's install folder (Steam, GOG, itch.io and similar); or
- for personal use, take it from the free [VVVVVV: Make and Play Edition](https://thelettervsixtim.es/makeandplay/), as the VVVVVV source license describes.

You never unpack or edit it by hand. The packaging script:
- checks that it contains the 49 files the PS2 build loads;
- makes a PS2 copy, `data-ps2.zip`, in which the 61.7 MB music file is stored uncompressed so it can be streamed;
- only reads your original.

Only one `data.zip` has been tested so far.

**Do not share** `data.zip`, `data-ps2.zip` or a USB package that contains them.

## Running on a PlayStation 2

### Prepare the USB stick

```sh
scripts/make-usb-package.sh /path/to/your/data.zip
#   builds everything if needed, then -> build/usb-package/VVVVVV/
#   it must end with "checksums OK" and no "FAT32 problem" lines
```

1. Use a **FAT32** stick with an **MBR** partition table. Smaller and older sticks are safest on the PS2's USB 1.1 ports.
2. Copy the whole `build/usb-package/VVVVVV/` folder to the **root** of the stick, so you have `/VVVVVV/VVVVVV.elf`.
3. Check the copy, then eject safely:

   ```sh
   cd /run/media/$USER/<STICK>/VVVVVV && sha256sum -c --quiet MANIFEST.sha256 && echo OK
   ```

The folder contains:
- `VVVVVV.elf`, `VVVVVV-nomusic.elf` and `fs_smoke.elf`;
- `data-ps2.zip`, `lang/` and `fonts/`;
- `README-PS2.txt`, a short on-stick guide;
- `optional/bench.txt` and `optional/overlay.txt`.

### Launch with FMCB and wLaunchELF

You need a way to run homebrew, e.g. a memory card with Free McBoot (FMCB) and
[wLaunchELF](https://github.com/ps2homebrew/wLaunchELF).

**Before power-on:** FMCB card in slot 1, DualShock 2 in port 1, the stick in a **front** USB port.

1. Boot into wLaunchELF.
2. **Storage test first:** FileBrowser → `mass:/` → `VVVVVV/` → `fs_smoke.elf`. It should end with **ALL OK**. Photograph the screen, then reset.
3. **The game:** start `mass:/VVVVVV/VVVVVV.elf`. A white start-up list appears. Its first line shows the build ID. Then the title screen follows.
4. If audio misbehaves, try `VVVVVV-nomusic.elf`.

**Controls:**

| Button | Action |
|---|---|
| Left stick / D-pad | move |
| ✕ | flip / confirm |
| ○ | back / pause menu |
| △ | map |
| □ | interact |
| R1 | restart |
| **L3+R3** (press both sticks) | performance overlay |

**Diagnostic files.** Copy these next to the ELF to enable them:
- `optional/overlay.txt`: start with the overlay on;
- `optional/bench.txt`: run a ~4-minute hands-off benchmark that writes `bench_results.txt`. Delete it afterwards to play normally.

**If something fails:**
- A **FATAL ERROR** or **CPU EXCEPTION** screen stays up. Photograph it whole.
- A hang on the start-up list shows the last step reached.

The full printable session checklist is [docs/ps2-hardware-first-boot.md](docs/ps2-hardware-first-boot.md).

### Running in PCSX2

```sh
PCSX2_BIOS=/path/to/your/bios.bin VVVVVV_DATA=/path/to/data-ps2.zip RUN_SECONDS=120 PCSX2_GUI=1 \
    scripts/run-vvvvvv-pcsx2.sh
```

To make `data-ps2.zip` without making a full package, run `scripts/repack-data-zip.py data.zip data-ps2.zip`. Logs and screenshots go to `logs/pcsx2/`. Other options are documented at the top of each `scripts/run-*.sh`.

## Testing on a real PS2 (no development tools needed)

The tester release is a zip **without any game data**. You add the data from your own copy of VVVVVV on your PC, with a small Python script.

**You need:**
- a PS2 that runs homebrew (FMCB, PS2BBL or similar, with wLaunchELF);
- a FAT32 USB stick with an MBR partition table, about 200 MB free;
- a PC with Python 3.8+ (Windows, Linux or macOS);
- `data.zip` from your own copy of VVVVVV.

**Steps:**
1. Download `VVVVVV-PS2-<release>.zip` from [Releases](https://github.com/MarvinParanoid/VVVVVV-PS2/releases) once a tester release is published, and unzip it.
2. Run `python3 tools/prepare-ps2-usb.py /path/to/data.zip usb/VVVVVV usb/VVVVVV-P31DEV` (on Windows: `py tools\prepare-ps2-usb.py ...`).
3. Copy the folders inside `usb/` to the root of the stick.
4. Follow `TESTING.txt`. The first session takes about 15 minutes.

The archive's `README.txt` has the details; the developer's version of the plan is [docs/ps2-hardware-test-plan.md](docs/ps2-hardware-test-plan.md).

## Reporting hardware test results

Please open an issue with the **Hardware test report** template; `TESTING.txt` lists what to collect. Include:
- console model;
- launcher;
- USB stick;
- TV and cable;
- the build ID;
- photos of any error screen;
- the files the game writes into its folder on the stick: `fs_smoke.log`, `vvvvvv_ps2.log`, `bench_results.txt`.

Failures are just as useful as successes. **Do not attach any game data.** See [CONTRIBUTING.md](CONTRIBUTING.md).

**Crash addresses.** An exception screen shows `EPC` and `RA` addresses. Developers can turn them into function names with the unstripped ELF of the same build:

```sh
scripts/ps2-profile.py out/vvvvvv-ps2/VVVVVV.elf --addr <EPC> <RA>
```

## How it works

[docs/technical-overview.md](docs/technical-overview.md) summarises the interesting parts:
- the SDL2 PS2 backend fixes (GS scaling, colour range, gsKit queue guard);
- the software-rendering work, which took normal rooms from 33–40 ms to about 15–17 ms per frame in PCSX2. Most of the gain came from removing per-pixel channel swapping between ABGR and ARGB, while keeping output bit-identical;
- Vorbis music streaming in 32 MB of RAM, and the EE scheduler problem it uncovered;
- the on-console diagnostics.

**Detailed reports** for each phase, with methods and measurements:

| Phase | Report |
|---|---|
| Feasibility study | [ps2-feasibility.md](docs/ps2-feasibility.md) |
| P1: SDL2 environment and present path | [ps2-p1-report.md](docs/ps2-p1-report.md) |
| P1c: renderer profile | [ps2-p1c-render-profile.md](docs/ps2-p1c-render-profile.md) |
| P2a: first playable build | [ps2-p2a-report.md](docs/ps2-p2a-report.md) |
| Port comparison: other constrained ports and the music plan | [vvvvvv-cross-platform-port-analysis.md](docs/vvvvvv-cross-platform-port-analysis.md), [dreamcast-port-analysis.md](docs/dreamcast-port-analysis.md) |
| P2b: music streaming | [ps2-p2b-report.md](docs/ps2-p2b-report.md) |
| P3: rendering optimisation | [ps2-p3-report.md](docs/ps2-p3-report.md) |
| H1: hardware readiness | [ps2-h1-hardware-readiness.md](docs/ps2-h1-hardware-readiness.md) |
| H1.1: release candidate | [ps2-h1-release-candidate.md](docs/ps2-h1-release-candidate.md) |
| G1: GS renderer feasibility (deferred) | [ps2-gs-renderer-feasibility.md](docs/ps2-gs-renderer-feasibility.md) |
| P3.1: software renderer edge cases (Flip Mode) | [ps2-p3.1-software-renderer-edge-cases.md](docs/ps2-p3.1-software-renderer-edge-cases.md) |

These reports refer to `logs/` and `release/` folders. Those are local outputs of the scripts and are not part of the repository.

## Repository layout

```
patches/vvvvvv/   PS2 changes to VVVVVV (applied to the pinned upstream commit)
patches/sdl2/     PS2 changes to SDL 2.32.10
scripts/          build, packaging, PCSX2 runners, benchmark and profiling tools
tests/ps2/        standalone PS2 test programs (renderer checks, fs_smoke storage test)
docker/           Dockerfile for the pinned ps2dev build image
docs/             technical reports, hardware checklist, on-stick README
LICENSES/         license texts (see LICENSE.md)
```

## License

**Mixed: see [LICENSE.md](LICENSE.md).**

| Part | License |
|---|---|
| VVVVVV patches | VVVVVV Source Code License (non-commercial), like VVVVVV itself |
| SDL and PhysicsFS patches | zlib |
| Tooling and documentation | MIT |

Game assets are proprietary and not included.

## Credits

- **VVVVVV** by [Terry Cavanagh](https://distractionware.com), with music by Magnus Pålsson (SoulEye). This port is based on the VVVVVV source code, which is © Terry Cavanagh. It is an altered, unofficial version, not affiliated with or endorsed by the original authors.
- **[SDL](https://libsdl.org)** and its PS2 port by the SDL and ps2dev contributors.
- **[ps2dev](https://github.com/ps2dev)**: the toolchain, ps2sdk, gsKit and ps2sdk-ports.
- **VVVVVV's libraries:** [PhysicsFS](https://icculus.org/physfs/), [FAudio](https://github.com/FNA-XNA/FAudio), [stb_vorbis](https://github.com/nothings/stb), tinyxml2, lodepng, SheenBidi and c-hashmap.
- **Tools:** [PCSX2](https://pcsx2.net), the reference emulator for this work; [Play!](https://purei.org) for supplementary checks.
- **Prior art:** the VVVVVV ports to Dreamcast ([VVVVVVDC](https://github.com/gusarba/VVVVVVDC)) and other constrained platforms were studied for design ideas. No code was taken from them.

"PlayStation" and "PS2" are trademarks of Sony Interactive Entertainment. This project is not affiliated with Sony.
