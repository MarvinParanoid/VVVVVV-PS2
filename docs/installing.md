# Installing and running on a PS2

How to put the game on a USB stick, start it on a PlayStation 2, and report what happens. To build the ELFs yourself, see [building.md](building.md).

## Compatibility

The real-PS2 column comes from one console: a PlayStation 2 Slim SCPH-7500x (EU), Free McBoot, one 8 GB USB stick (MBR, FAT32), an official DualShock 2 and component video. It applies to builds from tag `H3-FIRST-HARDWARE-PLAYABLE` (build `P3.4.3-SAVEPAD`) on. **Older builds, including `H1-RC2` and `H2-TEST1`, hang on a real PS2** at the first missing file. Other models and setups are untested.

| Area | PCSX2 v2.8.2 | Real PS2 (SCPH-7500x, 1 console) |
|---|---|---|
| Boot from a USB stick folder, title screen | ✅ tested | ✅ tested; first frame ~8 s after boot |
| Gameplay with DualShock 2 | ✅ tested (emulated pad) | ✅ D-pad, buttons and left analog stick |
| Streamed music + sound effects | ✅ tested, 0 underruns | ✅ 0 underruns, no late audio submissions |
| Save / continue (to the USB stick) | ✅ tested | ✅ saves written and loaded back on the next start |
| Full game speed in normal rooms (29.4 ticks/s) | ✅ measured | ✅ 29.5 ticks/s, 10 s minimum 29.0 |
| Tower / Panic Room worst case | ⚠️ ~25 of 29.4 ticks/s (85%) | ❓ unmeasured |
| Flip Mode (upside-down option) | ✅ full speed in most scenes in `P3.2-T1`; slow in `H1-RC2` ([P3.1](ps2-p3.1-software-renderer-edge-cases.md)) | ❓ unmeasured |
| Overlay, `fs_smoke`, exception screen | ✅ tested | ✅ overlay and `fs_smoke`; the exception screen appeared once (in a build before the fixes) |
| NTSC video output on a PAL console | — | ✅ over component video on one TV |
| Save after a full power-off, full or write-protected stick, controller unplugged and replugged | — | ❓ untested |

PCSX2 does not model the PS2's CPU caches or memory latency. On the tested console, normal rooms still run at full speed (34 ms per tick, of which about 18 ms is work). The heaviest scenes have not been measured there yet.

**What the game includes:**
- The full game in English: main game, time trials, Secret Lab and Super Gravitron. It is rendered by SDL2's software renderer and scaled to the screen by the GS.
- All music, streamed from the USB stick, with sound effects.
- DualShock 2 controls with PlayStation button glyphs in the menus.
- Saves (`settings.vvv`, `unlock.vvv`, quicksave and teleporter saves) in a `savedata/` folder next to the game on the stick.

## Known limitations

- **NTSC only:** always 640×448 at 60 Hz, also on PAL consoles. Use a TV and cable that accept 60 Hz (RGB SCART, component). Over composite on a PAL-only TV the picture may be black and white or roll. There is no PAL mode yet.
- **No memory-card saves.** Saves go to the USB stick only, so the game folder must be writable.
- **USB mass storage only.** HDD, network and disc launches have not been tried.
- **English only.** The CJK font sheets do not fit the GS texture limit.
- **Slowdown in the worst scenes.** The Tower and Panic Room run at about 85% speed in PCSX2 when the camera chases the player. Normal tower climbing is close to full speed.
- **Music starts about 0.5 s** after a track change.
- `over30mode` (interpolated 30+ FPS) is off by default. It can be enabled in the options but costs speed.

## Game data

The PS2 build needs VVVVVV's `data.zip`, which contains the graphics, sounds and music. It is **not** part of this repository or of any package made from it. Either:
- copy `data.zip` from your purchased copy of VVVVVV, from the game's install folder (Steam, GOG, itch.io and similar); or
- for personal use, take it from the free [VVVVVV: Make and Play Edition](https://thelettervsixtim.es/makeandplay/), as the VVVVVV source license describes.

You never unpack or edit it by hand. The packaging tools:
- check that it contains the 49 files the PS2 build loads;
- make a PS2 copy, `data-ps2.zip`, in which the 61.7 MB music file is stored uncompressed so it can be streamed;
- only read your original.

Only one `data.zip` has been tested so far.

**Do not share** `data.zip`, `data-ps2.zip` or a USB folder that contains them.

## Preparing the USB stick

There are two ways to get a game folder.

### From a tester release (no development tools needed)

**No tester release has been published yet.** When one is, it will be a zip on the [Releases](https://github.com/MarvinParanoid/VVVVVV-PS2/releases) page **without any game data**.

You will need:
- a PC with Python 3.8+ (Windows, Linux or macOS);
- `data.zip` from your own copy of VVVVVV.

**Steps:**
1. Unzip the release.
2. Run `python3 tools/prepare-ps2-usb.py /path/to/data.zip usb/VVVVVV usb/VVVVVV-P31DEV`. On Windows, use `py tools\prepare-ps2-usb.py ...`.
3. Copy the folders inside `usb/` to the root of the stick.
4. Follow `TESTING.txt` from the zip. The first session takes about 15 minutes.

The archive's `README.txt` and `TESTING.txt` come from [docs/tester/](tester/).

### From a source build

After [building](building.md):

```sh
scripts/make-usb-package.sh /path/to/your/data.zip
#   builds everything if needed, then -> build/usb-package/VVVVVV/
#   it must end with "checksums OK" and no "FAT32 problem" lines
```

The folder contains:
- `VVVVVV.elf`, `VVVVVV-nomusic.elf` and `fs_smoke.elf`;
- `data-ps2.zip`, `lang/` and `fonts/`;
- `README-PS2.txt`, a short on-stick guide;
- `optional/bench.txt` and `optional/overlay.txt`.

### Copying to the stick

1. Use a **FAT32** stick with an **MBR** partition table, with about 200 MB free. Smaller and older sticks are safest on the PS2's USB 1.1 ports.
2. Copy the whole game folder to the **root** of the stick, so you have `/VVVVVV/VVVVVV.elf`.
3. Check the copy, then eject safely:

   ```sh
   cd /run/media/$USER/<STICK>/VVVVVV && sha256sum -c --quiet MANIFEST.sha256 && echo OK
   ```

## Launching with FMCB and wLaunchELF

You need a way to run homebrew, e.g. a memory card with Free McBoot (FMCB) or PS2BBL, and [wLaunchELF](https://github.com/ps2homebrew/wLaunchELF).

**Before power-on:** FMCB card in slot 1, DualShock 2 in port 1, the stick in a **front** USB port.

1. Boot into wLaunchELF.
2. **Storage test first:** FileBrowser → `mass:/` → `VVVVVV/` → `fs_smoke.elf`. It should end with **ALL OK**. Photograph the screen, then reset.
3. **The game:** start `mass:/VVVVVV/VVVVVV.elf`. A white start-up list appears. Its first line shows the build ID. Then the title screen follows.
4. If audio misbehaves, try `VVVVVV-nomusic.elf`.

The full printable session checklist is [ps2-hardware-first-boot.md](ps2-hardware-first-boot.md). The developer's multi-session plan is [ps2-hardware-test-plan.md](ps2-hardware-test-plan.md).

### Controls

| Button | Action |
|---|---|
| Left stick / D-pad | move |
| ✕ | flip / confirm |
| ○ | back / pause menu |
| △ | map |
| □ | interact |
| R1 | restart |
| **L3+R3** (press both sticks) | diagnostics overlay |

### Diagnostic files

Copy these next to the ELF to enable them:
- `optional/overlay.txt`: start with the overlay on;
- `optional/bench.txt`: run a ~4-minute hands-off benchmark that writes `bench_results.txt`. Delete it afterwards to play normally.

What the overlay shows is described in [ps2-hardware-diagnostics.md](ps2-hardware-diagnostics.md) (for the newer overlay) and [ps2-h1-hardware-readiness.md](ps2-h1-hardware-readiness.md) (for the overlay in `H1-RC2`).

### If something fails

- A **FATAL ERROR** or **CPU EXCEPTION** screen stays up. Photograph it whole.
- A hang on the start-up list shows the last step reached.
- If `mass:/` is empty, check that the stick is FAT32 with MBR, plugged in before power-on, in a front port; try another, smaller stick.

## Reporting hardware test results

Please open an issue with the **Hardware test report** template. Include:
- console model;
- launcher;
- USB stick;
- TV and cable;
- the build ID;
- photos of any error screen;
- the files the game writes into its folder on the stick: `fs_smoke.log`, `vvvvvv_ps2.log`, `bench_results.txt`.

Failures are just as useful as successes. **Do not attach any game data.** See [CONTRIBUTING.md](../CONTRIBUTING.md).

**Crash addresses.** An exception screen shows `EPC` and `RA` addresses. Developers can turn them into function names with the unstripped ELF of the same build:

```sh
scripts/ps2-profile.py out/vvvvvv-ps2/VVVVVV.elf --addr <EPC> <RA>
```
