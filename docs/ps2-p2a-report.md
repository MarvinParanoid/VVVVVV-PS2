# P2a Report: VVVVVV running on PS2 (PCSX2)

**Date:** 2026-10-22

**Base:**
- VVVVVV `2.4-updates` @ `79395fa` (SDL2).
- SDL 2.32.10 + `patches/sdl2` (0001 GS-scaled framebuffer / queue guard, 0002 SW blit fast paths).

**Related:**
- [ps2-feasibility.md](ps2-feasibility.md)
- [ps2-p1-report.md](ps2-p1-report.md)
- [ps2-p1c-render-profile.md](ps2-p1c-render-profile.md)

## 0. Status

| Milestone | Status | Evidence (PCSX2 v2.8.2, BIOS SCPH-75001 v2.20) |
|---|---|---|
| ELF builds reproducibly | ✅ | A clean re-export + patch + build gives a bit-identical ELF (`fb64a6cf…`) |
| First rendered frame | ✅ | 4.46 s after boot. Title screen captured from GS VRAM (`logs/pcsx2/VVVVVV/shot_00_boot.png`); the PlayStation ✕ glyph is shown |
| First playable room | ✅ | `gamestate=GAMEMODE` at 24.9 s, room (104,110) (ship) |
| Stable controller input | ✅ (emulated pad) | A human played ~4 min with PCSX2's DualShock 2 driven by keyboard: about 25 room transitions, map/menu opened and closed, no input loss reported in logs. Path: PCSX2 pad → libpad → SDL GameController → VVVVVV |
| Performance measured | ✅ | Gameplay render mean 18-23 ms, p95 19-38 ms (§4) |
| Real hardware | ⏸ | Not tested (§6) |

Music is disabled by design for now (§3). Sound effects and the FAudio → SDL audio path are initialised.

## 1. Build (reproducible)

```sh
scripts/build-vvvvvv-ps2.sh --refetch
```

What it does:
1. Exports pinned upstream, then applies `patches/vvvvvv/*`.
2. Builds SDL 2.32.10 + `patches/sdl2` if not built yet.
3. Builds the game in the pinned ps2dev image (Docker, or rootless bwrap).
4. Stages `out/vvvvvv-ps2/`.

**Output:** `out/vvvvvv-ps2/` contains:
- `VVVVVV.elf`;
- `lang/en/`;
- `fonts/` (Latin + button glyphs only);
- `SOURCE.txt` (upstream commit, submodules, patch hashes).

**No game assets are in the ELF or the repo.**

**ELF:** sha256 `fb64a6cf7c703982…`; loaded size 7.72 MB.

| Section | Size |
|---|---|
| text | 4.94 MB (incl. main-campaign room data) |
| data | 0.43 MB |
| bss | 2.34 MB (incl. VVVVVV's 1.92 MB static custom-level array) |

**Build options:**
- `-DPS2_MUSIC=ON` re-enables the music blob. It will not fit in RAM until music streams. *(Superseded in P2b: music now streams and `PS2_MUSIC=ON` is the default; see [ps2-p2b-report.md](ps2-p2b-report.md).)*
- `-DMAKEANDPLAY=ON` produces the Make and Play variant, which is the only one whose assets may be redistributed (§7).

## 2. Launch

**Assets.** The game needs `data.zip` next to the ELF. This run used the official Make and Play `data.zip`:
- 61.4 MB;
- sha256 `c7678095…ecc0ea` (matches the Haiku port recipe);
- cached at `~/.cache/vvvvvv-ps2/assets/data.zip`.

**PCSX2, automated** (boot, run N seconds, collect logs and GS snapshots):

```sh
PCSX2_BIOS=~/.cache/vvvvvv-ps2/bios/ps2-0220a-20060210.bin RUN_SECONDS=90 scripts/run-vvvvvv-pcsx2.sh
```

**PCSX2, interactive:** add `PCSX2_GUI=1 RUN_SECONDS=600`.

Default keyboard bindings for pad 1:

| Pad | Key |
|---|---|
| ✕ (start / confirm / flip) | K |
| ○ (back) | L |
| D-pad | arrows |
| left stick | WASD |
| Start | Enter |

**Run folder:** `build/vvvvvv-run/` holds the ELF, `lang/`, `fonts/` and a symlink to `data.zip`. PCSX2's host filesystem must be enabled; the script does this. Saves go to `build/vvvvvv-run/savedata/` and are kept between runs unless `RESET_SAVE=1`.

**Logs** go to `logs/pcsx2/VVVVVV/`:
- `vvvvvv_ps2.log`: instrumentation;
- `emulog.txt`: EE/IOP console, including the game's own log;
- `shot_*.png`: GS framebuffer snapshots.

**Real hardware (untested):**
1. Copy `out/vvvvvv-ps2/*` plus `data.zip` to a FAT32 USB stick, for example `mass:/APPS/VVVVVV/`.
2. Launch `VVVVVV.elf` with wLaunchELF / uLaunchELF.
3. `savedata/`, `vvvvvv_ps2.log` and snapshots are written next to the ELF.

## 3. What was changed

The patch series is `patches/vvvvvv/`. Every PS2 change sits behind `__PS2__`, `__ps2sdk__`, CMake `if(PS2)` or `VVV_PS2_NO_MUSIC`.

| Patch | Lines (+/−) | Content |
|---|---|---|
| `0001-physfs-ps2-platform` | +88 / −1 | PhysicsFS PS2 platform: posix file I/O via libcglue, base dir = cwd (set from argv0 by ps2sdk), pref dir = `<base>savedata/`, no CD-ROM enumeration; mutex-based atomics (R5900 has no LL/SC, toolchain has no `__sync_*`) |
| `0002-cmake-ps2-target` | +20 / −1 | `if(PS2)`: add `PS2Platform.cpp`, `.elf` suffix, `-ldebug`, `VVV_PS2_NO_MUSIC` unless `PS2_MUSIC`; skip RPATH flags (they broke the ps2sdk linker-script flags); FAudio `-Wno-error=incompatible-pointer-types` (newlib `int32_t` is `long`; same flag ps2sdk-ports uses) |
| `0003-ps2-platform-layer-and-hooks` | +383 / 0 | New `PS2Platform.{h,cpp}`: SDL hints, main-thread priority 64, instrumentation, GS snapshots. Hooks: 2 in `main.cpp` init, 2 around the loop, 2 around render+present; asset-load timing in `FILESYSTEM_loadFileToMemory`; `mkdir` wrapper stripping the trailing `/` (PCSX2's `host:` refused `"savedata/saves/"`, so saving silently failed); PlayStation glyphs (the PS2 pad reports VID 0) |
| `0004-ps2-disable-music-blob` | +9 / 0 | Skip loading `vvvvvvmusic.vvv`. It is 61.7 MB, held fully in RAM plus a copy per track, which cannot fit in 32 MB. `musicclass::play()` already bounds-checks, so gameplay is unaffected |

**Settings in `PS2_PreInit`** (from P1/P1c):
- software renderer;
- render batching;
- bit-exact blit fast paths;
- 320×240 XBGR8888 texture framebuffer scaled by the GS;
- 64 KB gsKit queues;
- dynamic vsync;
- 128 KB thread stacks;
- upstream switches `SteamDeck` (no keyboard: hides the editor) and `SteamTenfoot`.

**No game logic was changed. No GPU render targets were added.**

## 4. Measurements

All from PCSX2. PCSX2 has no EE cache or memory model, so times are probably optimistic.

### Frame time

`vvvvvv_ps2.log`, 300-loop windows.

| Scene | Render + present, mean | p95 | Max |
|---|---|---|---|
| Title screen | 13.7 ms | 14.4-15.2 | 16.6 |
| Gameplay, typical rooms (ship, Space Station) | 18.4-23.0 ms | 18.7-23.9 | 34-42 (room transitions) |
| Busier rooms | 24-28 ms | 33-38 | 46-59 |
| Map / pause menu (`gamestate=3`) | 37.0 ms | 44.0 | 50.9 |

- Game logic is ~0.2 ms per loop (work − render).
- `over30mode` is on (upstream default), so the loop renders uncapped with interpolation, at about 45-55 fps in gameplay in PCSX2. Logic still runs at its fixed 34 ms step.

### Memory

malloc in use.

| Point | In use | Headroom (of 32 MiB) |
|---|---|---|
| Boot | 0.03 MB | 24.6 MB |
| After `SDL_Init` | 0.05 MB | 24.6 MB |
| First frame (all graphics, fonts, sounds, 11 targets loaded) | 17.10 MB | 7.55 MB |
| In GAMEMODE, after 4 min of play | 17.22 MB | 7.43 MB |

Memory is stable during play; there is no growth across ~25 room changes.

The 24.6 MB starting headroom is after the 7.7 MB ELF image and the EE kernel area.

### Asset loading

60 file reads, 2.59 MB in total, 0.40 s summed (host: in PCSX2). Biggest reads:
- `rescue.wav`: 769 KB, 52 ms;
- `souleyeminijingle.wav`: 547 KB, 75 ms.

Boot to title screen is 4.5 s. Most of it is PNG decode and texture setup, not I/O.

## 5. Remaining issues (PCSX2)

| # | Issue | Severity | Next step |
|---|---|---|---|
| 1 | **Music off.** Needs a streaming loader (read one track from data.zip at a time) | Medium (feature) | P2b: stream the current track from PhysFS instead of `unPackBinary` |
| 2 | **Memory headroom 7.4 MB.** Untested in custom levels (level files up to 1.6 MB), the ending, and the editor (hidden) | Medium | Test Make and Play levels and the ending; trim duplicated sprite surfaces if needed |
| 3 | Sound effects initialised, but **audibility not verified** (logs only) | Low | Listen in PCSX2; add a callback counter to instrumentation |
| 4 | Map/menu screen 37 ms mean (44 ms p95) | Low | Profile; per-call overhead (P1c §5) |
| 5 | Log noise: `play() out-of-bounds!` on every music change; one `Could not stat file: permission denied` at boot (PhysFS probing the host root; PCSX2 denies access outside the ELF dir) | Low | Silence under `VVV_PS2_NO_MUSIC`; harmless otherwise |
| 6 | Snapshots (860 KB PPM every ~1,800 loops) and log flushes cost I/O, which will be slow on USB 1.1 | Low | Make snapshots opt-in for hardware runs |
| 7 | Input regression tests need a human. A virtual-gamepad automation via `/dev/uinput` (writable here) is designed, not built | Low | Build it for CI-style runs |

## 6. Blockers and risks for real-hardware testing (SCPH-7500x)

| # | Item | Risk |
|---|---|---|
| H1 | **Only `host:` was exercised.** `mass:` (USB/BDM) boot-device detection, `waitUntilDeviceIsReady`, path forms like `mass:/APPS/VVVVVV/` with PhysFS + the `mkdir` wrapper are untested | High (first thing to check) |
| H2 | **Real EE timing.** PCSX2 is likely optimistic for memory-bound blitting. Gameplay at 18-23 ms in PCSX2 leaves margin under 34 ms (logic step), but the map screen and busy rooms may exceed it | Medium |
| H3 | **Display.** NTSC 640×448 interlaced only (SDL 2.32.10 forces NTSC). A PAL SCPH-75004 on a PAL-only TV may not show 60 Hz; uneven 240→448 row scaling is visible on a CRT | Medium |
| H4 | **Saves on writable media only.** USB is fine; memory card (`mc0:`) not used yet; CD/DVD is read-only | Medium |
| H5 | **Exit path.** Quit from the menu → `SDL_Quit` → return to the browser is untested on hardware (completes in PCSX2 for the test ELFs) | Low |
| H6 | **USB 1.1 load time** for PNG/WAV (2.6 MB) plus the `data.zip` central directory; boot may take several seconds longer | Low |
| H7 | **Distribution.** This ELF is a personal-use build (full campaign). A shareable build must be `-DMAKEANDPLAY=ON` with the M&P data. The GPL-3.0 vs LGPL question for `ps2_drivers` (linked by SDL) is still open | Legal, not technical |

**Suggested hardware test order:**
1. Boot from USB.
2. Confirm `vvvvvv_ps2.log` appears on the stick.
3. Reach the title screen.
4. Start a new game and play 2-3 rooms.
5. Save at a checkpoint, power-cycle, and continue.
6. Copy the log back for frame times and memory.
