# Installing and running on a PS2

How to put the game on a USB stick, start it on a PlayStation 2, and report what happens. To build the ELFs yourself, see [building.md](building.md). What has been tested, and the limitations: [known-issues.md](known-issues.md).

## What you need

- A PS2 that runs homebrew: Free McBoot (FMCB) or PS2BBL, with [wLaunchELF](https://github.com/ps2homebrew/wLaunchELF) or another ELF launcher that can open `mass:/`. Tested so far: one PS2 Slim SCPH-7500x with FMCB.
- A USB stick, **FAT32 with an MBR partition table**, about 200 MB free. Smaller and older sticks are safest on the PS2's USB 1.1 ports.
- A TV and cable that show **NTSC 60 Hz**: the game always outputs 640×448 at 60 Hz, also on PAL consoles. RGB SCART or component video are safest.
- `data.zip` from your own copy of VVVVVV (see [Game data](#game-data)).
- A PC with Python 3.8 or newer, to prepare the game folder.

## Game data

The PS2 build needs VVVVVV's `data.zip`, which contains the graphics, sounds and music. It is **not** part of this repository or of any package made from it. Either:
- copy `data.zip` from your purchased copy of VVVVVV, from the game's install folder (Steam, GOG, itch.io and similar); or
- for personal use, take it from the free [VVVVVV: Make and Play Edition](https://thelettervsixtim.es/makeandplay/), as the VVVVVV source license describes.

You never unpack or edit it by hand. The packaging tools:
- check that it contains the 49 files the PS2 build loads;
- make a PS2 copy, `data-ps2.zip`, in which the 61.7 MB music file is stored uncompressed so it can be streamed;
- only read your original.

Only the full game's `data.zip` has been tested so far.

**Do not share** `data.zip`, `data-ps2.zip` or a USB folder that contains them.

## Preparing the game folder

### From a release archive

**No GitHub Release has been published yet.** Release candidates are built with `scripts/make-public-test-release.sh --release` ([releasing.md](releasing.md)). The archive `VVVVVV-PS2-<name>.zip` contains **no game data**.

1. Unzip it on your PC. Optional: `sha256sum -c SHA256SUMS` inside the unzipped folder.
2. Add your game data:

   ```sh
   python3 tools/prepare-ps2-usb.py /path/to/data.zip usb/VVVVVV      # Windows: py tools\prepare-ps2-usb.py ...
   ```

   It must end with `ready: usb/VVVVVV`.
3. Copy `usb/VVVVVV` to the stick (below).

The archive's `README.txt` ([docs/release/README.txt](release/README.txt)) contains the same steps.

### From a source build

After [building](building.md):

```sh
scripts/make-usb-package.sh /path/to/your/data.zip
#   builds everything if needed, then -> build/usb-package/VVVVVV/
#   it must end with "checksums OK" and no "FAT32 problem" lines
```

### What the folder contains

- `VVVVVV.elf`: the game;
- `VVVVVV-nomusic.elf`: the same game without music, as a fallback;
- `fs_smoke.elf`: USB self-test, run it first;
- `data-ps2.zip`: made from your `data.zip`;
- `lang/`, `fonts/`: open-source files from the VVVVVV repository;
- `README-PS2.txt`: a short on-stick guide;
- `SOURCE.txt`: build ID and exact source versions;
- `MANIFEST.sha256`: checksums of everything;
- `optional/bench.txt` and `optional/overlay.txt`.

## Copying to the stick

1. Copy the whole `VVVVVV` folder to the **root** of the stick, so that it contains `/VVVVVV/VVVVVV.elf`.
2. Check the copy, then eject safely:

   ```sh
   cd /run/media/$USER/<STICK>/VVVVVV && sha256sum -c --quiet MANIFEST.sha256 && echo OK
   ```

The game writes its saves (`savedata/`) and its log (`vvvvvv_ps2.log`) into this folder, so the stick must stay writable. Several game folders with different names can sit side by side; each keeps its own saves.

## Launching with FMCB and wLaunchELF

**Before power-on:** DualShock 2 in port 1, the stick in a USB port (the tested setup used a front port).

1. Boot into wLaunchELF (from the FMCB menu).
2. **Storage test first:** FileBrowser → `mass:/` → `VVVVVV/` → `fs_smoke.elf`. It should end with **ALL OK**. On the tested console it ends with PROBLEMS, listing only `opendir(".")`: that one is a known issue ([known-issues.md](known-issues.md#technical-debt)). Any other failed check matters. Reset.
3. **The game:** start `mass:/VVVVVV/VVVVVV.elf`. A white start-up list appears (its first line shows the build ID), then the game.
4. On the very first start the game asks for the language. Only English is included: press ✕. The choice is saved.
5. If audio misbehaves, try `VVVVVV-nomusic.elf`.

### Controls

| Button | Action |
|---|---|
| Left stick / D-pad | move |
| ✕ | flip / confirm |
| ○ | back / pause menu |
| △ | map |
| □ | interact |
| R1 | restart |
| **L3+R3** (press both sticks) | diagnostics overlay: off → compact → performance page → diagnostics page |

### Optional files

Copy these from `optional/` next to `VVVVVV.elf` to enable them:
- `overlay.txt`: start with the compact overlay on;
- `bench.txt`: run a ~4-minute hands-off benchmark that writes `bench_results.txt`. Delete it afterwards to play normally.

## If something fails

| What you see | What to do |
|---|---|
| `mass:/` is empty or the folder is missing | The stick must be FAT32 with an MBR partition table and plugged in before power-on. Try the other port and another, smaller stick |
| `fs_smoke.elf` lists a failed check other than `opendir(".")` | Photograph the screen; the game will probably not run from this stick |
| The start-up list stops at a step | Photograph it: the last line shows where it hangs |
| A **FATAL ERROR** or **CPU EXCEPTION** screen | Photograph it whole. Keep `vvvvvv_ps2.log` from the stick |
| Black screen, but other software shows a picture | The TV or cable does not accept NTSC 60 Hz |
| Saves are empty or lost | Check that the stick is not full or write-protected; report it |
| An old build hangs after "SDL started" | Builds before `H3-FIRST-HARDWARE-PLAYABLE` (`H1-RC2`, `H2-TEST1`) hang on a console; use a current build |

## Reporting hardware test results

Please open an issue with the **Hardware test report** template. Include:
- console model (sticker on the bottom) and region;
- launcher;
- USB stick and port;
- TV and cable;
- the build ID (first line of the start-up list, or `SOURCE.txt`);
- photos of any error screen;
- the files the game writes into its folder on the stick: `fs_smoke.log`, `vvvvvv_ps2.log`, `bench_results.txt` (if you ran it);
- if saves misbehave: the names and sizes of the files in `savedata/saves/`. Send the save files themselves only if a developer asks and you agree.

Failures are just as useful as successes. **Do not attach any game data.** See [CONTRIBUTING.md](../CONTRIBUTING.md).

**Crash addresses.** An exception screen shows `EPC` and `RA` addresses. Developers can turn them into function names with the unstripped ELF of the same build:

```sh
scripts/ps2-profile.py <unstripped VVVVVV.elf> --addr <EPC> <RA>
```
