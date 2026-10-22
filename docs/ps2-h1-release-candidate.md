# H1.1: Hardware-test release candidate VVVVVV-PS2-H1-RC2

**Date:** 2026-10-22

**Status:** **passed validation in PCSX2; frozen.** Ready for the first hardware test. (`H1-RC1` was frozen first and rejected; see §2 #7.)

**Target:** PS2 Slim SCPH-7500x via FMCB → wLaunchELF → USB.

> **No physical PS2 has run this build.**
> - Every result below was measured in PCSX2 v2.8.2 (BIOS SCPH-75001 v2.20, NTSC), marked [M], or read from code, marked [C].
> - Real-hardware compatibility is **not** claimed.
> - Development is frozen until hardware results arrive.

## 1. Source revision and build identity

There is no VCS in this workspace. The build is identified by its pinned inputs and the resulting checksums:

| Item | Value |
|---|---|
| Release | `release/VVVVVV-PS2-H1-RC2/` (read-only; verified by its `SHA256SUMS`) |
| Build ID | `H1-RC2`, compiled in (`PS2_BUILD_ID`). Shown on the boot console, the fatal and exception screens, the overlay status line and the first log line |
| VVVVVV | `2.4-updates` @ `79395fa47d35e1de5332e6b4a342249c46ac6fb9` + submodules (see `SOURCE.txt`) |
| VVVVVV patches | `patches/vvvvvv/0001`–`0005` (sha256 prefixes in `SOURCE.txt`: 0001 `f0779dca`, 0002 `00105d83`, 0003 `16271a48`, 0004 `c7082da7`, 0005 `5562e41d`) |
| SDL | 2.32.10 (tarball sha256 `5f5993c5…`) + `patches/sdl2/0001`–`0007` (0001 `32e8779f`, 0002 `c389bb78`, 0003 `8c8efdf4`, 0004 `a3143e6a`, 0005 `5bc054ac`, 0006 `8913b6e1`, 0007 `c5a29653`) |
| Toolchain | ps2dev Docker image pinned by digest (`scripts/ps2-env.sh`) |

Rebuild: `scripts/build-sdl2-ps2.sh && PS2_BUILD_ID=H1-RC2 scripts/build-vvvvvv-ps2.sh --refetch`. This reproduces `symbols/VVVVVV.elf` and `symbols/VVVVVV-nomusic.elf` bit for bit (§3).

## 2. Changes made during the audit

All changes are PS2-only. None touches rendering, music or game logic.

| # | Finding (evidence) | Change |
|---|---|---|
| 1 | **Log race between threads** [C]: since H1, the music worker's `vlog_*` messages go through `PS2_Log`, but appends to the shared log buffer were not serialised. Two threads appending at once could corrupt the buffer, and the trim `memmove` could then run with wrong lengths | Appends, trims, flushes and deferral are serialised with a kernel semaphore, created before any other thread exists |
| 2 | **Exception screen could be overdrawn** [C]: a fault in a non-main thread (music worker, audio, log writer) showed the screen from that thread while the main loop kept rendering over it | A "halted" flag set by the exception and fatal screens; the main loop parks at its next iteration. A second fault while reporting is parked in place, keeping the first report |
| 3 | **No build identity on screens** | `PS2_BUILD_ID` (`H1-RC2`) is compiled in and shown on every diagnostic screen, the overlay and the log. `SOURCE.txt` now also lists the SDL patches |
| 4 | **Read-only or full storage gave a cryptic fatal error** [M]: "Could not get OS directory: i/o error" | Two PS2 lines explain it: the folder must be writable because `savedata/` is created next to the ELF; is the stick write-protected, full or not FAT32? |
| 5 | **Packaging could not use a frozen build** | `scripts/freeze-release.sh <ID>` creates a read-only release folder: `bin/` (stripped), `symbols/` (unstripped), `files/`, `SOURCE.txt`, `SHA256SUMS`, `BUILD.md`, `FIRST-BOOT.md`, the packaging scripts. `make-usb-package.sh --from-release` (or the copy inside the release) packages it **without building**, after verifying `SHA256SUMS` |
| 6 | **PCSX2 test tooling** | `scripts/run-package-pcsx2.sh` runs an ELF from a package folder exactly as from the stick. Pad scenarios `save-quick` and `continue` added |
| 7 | **RC1 rejected** [M]: validation of the frozen `VVVVVV-PS2-H1-RC1` failed at packaging. `cp` kept the release's read-only file modes, so appending to the package's `SOURCE.txt` failed ("Permission denied") and no package was made. The binaries were fine: they rebuilt bit for bit, and the release was unchanged | `make-usb-package.sh` copies from a release with `--no-preserve=mode`. Rebuilt with build ID `H1-RC2` and frozen again. `release/VVVVVV-PS2-H1-RC1/` is left untouched and must not be used |
| 8 | **Docs** | Save location and save procedure corrected (§4); release commands; build ID |

**Checked, no change needed** [C, plus M where noted]:

| Topic | Finding |
|---|---|
| Unaligned access | All binary parsing in our code (zip directory, blob header table, Vorbis ID header, `fs_smoke`) uses `memcpy` or byte reads |
| Cache coherency | gsKit's `gsKit_TexManager_bind` calls `SyncDCache` before every texture DMA (disassembly). This includes the window framebuffer that patch 0006 places in texture memory. libdebug writes its DMA data through the uncached segment. `PS2_Snapshot` flushes the cache before DMA |
| DMA buffer alignment | GS textures `memalign(128)`; libdebug's DMA buffers 16-byte aligned in the final link (checked addresses) |
| Threads and priorities | Main 64; music worker 50 (time-sliced, ≤5 ms bursts [M]); log writer 50 (blocks in file RPC); SDL audio thread above both. Profiler alarm only with `bench.txt` |
| Stacks | SDL threads 128 KiB (hint); log writer 16 KiB static (open/write/close only); exception report runs on the faulting thread's stack (small) |
| Memory | Peak `malloc_in_use` 16.87 MiB in gameplay, ≥ 6.4 MiB free [M] |
| USB initialisation | SDL's `main()` loads usbd/bdm/bdmfs_fatfs/usbmass_bd and waits for the boot device. The result is shown on screen ("boot device ready" / "NOT ready") |
| Video mode | Forced NTSC 640×448i by SDL's gsKit backend (unchanged; PAL out of scope) |
| SPU2 / audio | audsrv via SDL's `ps2` audio driver. Failure is reported on the boot console ("SDL started: … audio FAILED") and in the log; behaviour after such a failure is untested |
| Diagnostics when disabled | Overlay off by default (`overlay.txt` or L3+R3). Benchmark only with `bench.txt`; the profiler starts only then. The disabled cost is one controller poll per frame and a timestamp per logic tick; H1 timing equals P3 within noise [M] |
| Failed log writes | `open()` failure is ignored; a write-protected folder ends at the fatal screen without a crash [M] |

## 3. Test results [M]

All tests ran **on copies of the USB package made from the frozen release** (`scripts/run-package-pcsx2.sh`), i.e. on the exact stripped binaries that go on the stick. `host:` stands in for `mass:`.

| Test | Result |
|---|---|
| Packaging with the release's own `make-usb-package.sh` | OK: 49 required assets present, `checksums OK`, 29 entries, 72.2 MiB |
| `fs_smoke`, good package | **ALL OK** |
| `fs_smoke`, one byte of `data-ps2.zip` flipped | PROBLEMS: 1 failed (CRC check), named on screen |
| `fs_smoke`, `data-ps2.zip` deleted | PROBLEMS: 3 failed, with the "make it with make-usb-package.sh" hint |
| Game without `data-ps2.zip` | FATAL screen: last stage "assets archive", hint to get `data.zip`; no crash |
| Game in a read-only folder | FATAL with the writable-folder hint (§2 #4), seen on the serial console; no crash. No screenshot or log, since nothing can be written |
| Boot console | Header "experimental test build H1-RC2"; every stage through "starting the renderer" |
| Build ID | Log header `=== VVVVVV PS2, build H1-RC2 ===` present in all 10 game runs |
| Exception (`crashtest.txt`, trap) | Screen: build H1-RC2, thread 1, trap code 13, EPC/RA `001fb118`, SP `01ffe5d0`, Cause `00000034`, last stage; same line on SIO and in the log. Symbolised in §5 |
| Overlay toggle (L3+R3 via virtual pad) | on → off → on logged; off by default |
| Save | `savedata/saves/qsave.vvv` (2504 B) plus `settings.vvv` and `unlock.vvv` written |
| Load (continue) | `qsave.vvv` read; resumes in room (113,105) |
| Gameplay, 300 s scripted (10 rooms) | 28/28 periodic reports `underruns=0`; loop period mean 34.0–34.2 ms, max 38.0 ms; no errors |
| Golden frames (pass 0) vs H1 and P3, music build | **identical 10/10** |
| Golden frames vs P3, no-music build | **identical 10/10** |
| Hardware preset, `bench_results.txt` | written; overlay showed BENCH DONE; numbers below |
| Clean rebuild (SDL + game `--refetch`, fs_smoke) | **bit-identical** to `symbols/VVVVVV.elf`, `symbols/VVVVVV-nomusic.elf`, `symbols/fs_smoke.elf` |
| Release folder after all tests | `sha256sum -c SHA256SUMS` passes: unchanged |

**Performance compared with H1 and P3** (same scenes and settings; ms; full speed is 29.4 ticks/s):

Hardware preset (`bench_results.txt`; ticks/s, slowest 1 s window, ms):

| scene | H1 tps / min | RC2 tps / min | H1 p99 | RC2 p99 | render H1 → RC2 | underruns |
|---|---|---|---|---|---|---|
| intro_dialogue | 29.4 / 29.3 | 29.4 / 29.3 | 34.7 | 34.5 | 10.78 → 10.78 | 0 |
| room_ss1 | 29.2 / 29.1 | 29.2 / 29.2 | 36.8 | 36.3 | 14.84 → 14.78 | 0 |
| tower | 25.0 / 25.0 | 25.0 / 24.9 | 46.1 | 45.4 | 23.61 → 23.72 | 0 |
| tower_climb | 28.9 / 28.3 | 29.0 / 28.4 | 40.6 | 40.5 | 21.24 → 21.31 | 0 |
| panic_room | 25.1 / 24.8 | 25.1 / 24.7 | 46.1 | 45.8 | 24.20 → 24.09 | 0 |
| super_gravitron | 29.2 / 29.1 | 29.3 / 29.0 | 37.2 | 37.0 | 20.76 → 20.86 | 0 |

The full 10-scene run with P3 settings (`ps2-bench-compare.py` against H1) gives the same ticks/s in every scene, and mean period and render time within ±0.1 ms. Peak memory 17.2 MiB. The only outlier is one 51.8 ms frame in `tower` (H1: 46.4 ms) with an unchanged p99, i.e. emulator noise. **No regression.** P3 → H1 was already equal within noise (H1 report).

## 4. Save/load: verified behaviour

**Where the data goes** [C]:
- The PS2 build writes **only to the folder the ELF runs from**:
  - PhysFS write directory `<base>savedata/`, created at start-up;
  - saves in `<base>savedata/saves/`: `settings.vvv`, `unlock.vvv`, `qsave.vvv` (quicksave), `tsave.vvv` (teleporter save);
  - custom levels in `<base>savedata/levels/`.
- `<base>` is `getcwd()`, which ps2sdk derives from `argv[0]`. From wLaunchELF on a stick it is `mass:/VVVVVV/` (or `mass0:/…`, depending on the loader); in PCSX2 it is `host:/`.
- **There is no memory-card (`mc0:`) support.** Nothing is read from or written to the memory card. The FMCB card is only the boot loader.

**Tested in PCSX2** [M]. This is the closest equivalent: the package folder on `host:`, the same relative layout as on the stick.
1. **Save:** new game, walk, then △ (map) → → → (SAVE tab) → ✕. `savedata/saves/qsave.vvv` is written (room 113,105, summary "Space Station, 0:33").
2. **Load:** relaunch, then title ✕ → play ✕ → continue ✕. The game resumes in room (113,105); a new game would start in (104,110).
3. **Read-only folder:** fatal screen with the explanation from §2 #4; no crash.

**Not testable here:**
- FAT32 write behaviour on a real stick (open/write/close without explicit sync);
- power loss during a save;
- speed of the save write on USB 1.1.

## 5. Crash symbolisation

1. **Read the screen.** The exception screen shows:
   - build ID and thread;
   - exception name and code;
   - EPC, BadVAddr, RA, SP, Cause, Status;
   - last start-up stage and last error.

   The same line goes to the EE serial console, and to the log if the log can still be written.
2. **Check the build.** It must read `H1-RC2`. Otherwise the addresses do not belong to this release.
3. **Symbolise:**

   ```sh
   scripts/ps2-profile.py release/VVVVVV-PS2-H1-RC2/symbols/VVVVVV.elf --addr <EPC> <RA>
   ```

   (or `symbols/VVVVVV-nomusic.elf` / `symbols/fs_smoke.elf` for those ELFs). The stripped ELF on the stick has the same code addresses as its `symbols/` twin, because `strip` removes only symbol and debug sections.

   Verified [M] with a deliberate trap: EPC `001fb118` → `PS2_LoopEnd + 0x988`. The disassembly of `symbols/VVVVVV.elf` at that address is `teq zero,zero`, the test trap in `PS2Platform.cpp`.

**Limits:**
- PCSX2 raises traps but **not** address errors for unaligned accesses, so that class could not be exercised.
- On hardware, a fault inside the kernel or in an interrupt handler, or a hang without a fault, shows no exception screen. Then the boot console's last line or the log is the clue.
- The log is written about every 10 s, so the newest lines before a hard crash may be missing; the screens do not depend on it.

## 6. Hardware-specific risks (cannot be tested in PCSX2)

1. **USB:** stick and driver compatibility; whether the device is named `mass:` or `mass0:`; read speed and latency on USB 1.1; behaviour if the stick is pulled out.
2. **Video:** always NTSC 640×448i at 60 Hz on a PAL console. Composite on a PAL-only TV may lose colour; CRT overscan at the edges.
3. **CPU:**
   - real EE speed, caches and FPU, measured by `bench_results.txt` against §3;
   - golden-frame CRCs vs PCSX2;
   - unaligned-access faults that PCSX2 does not raise.
4. **Audio:** SPU2/audsrv output quality, clicks at loops and track switches, latency.
5. **Storage writes:** FAT32 semantics, flush behaviour, write-protect.
6. **Timing:** `SDL_Delay(1)` granularity on the real kernel, which the music worker's time slicing relies on.

## 7. Release candidate checksums

`release/VVVVVV-PS2-H1-RC2/SHA256SUMS` (excerpt):

```
2331d351540f9a7647f0736fdd8993cae802d92c51fc412de8fc03e0521f6f25  ./bin/VVVVVV.elf
f6c41c1129061eb09f30e25132e8c563979e95d0b12d2bde006ccc060edf3c40  ./bin/VVVVVV-nomusic.elf
f900140be6ff73cc2fe6faad3fd7bbb5736cd5f6bfb7d4cad04547dba7c03ed2  ./bin/fs_smoke.elf
7f24c75ec0d6939c4c3272b10dcb2dc53946ebb247b13de2733db02e21b1ced4  ./symbols/VVVVVV.elf
cfeec245a8e59db652d245dc311cb51406d5e67f5bb0e20b5ad1cdb9c7f2614c  ./symbols/VVVVVV-nomusic.elf
4a5bcb14f1cfbcbe6bf5de4a5dd065f1806eda129760a1288342b727e26800aa  ./symbols/fs_smoke.elf
(27 lines in total; the remaining lines cover files/, SOURCE.txt, the scripts and docs)
```

## 8. USB preparation commands

```sh
# on the PC, from the repository (the release is not rebuilt)
scripts/make-usb-package.sh ~/path/to/data.zip --from-release release/VVVVVV-PS2-H1-RC2
#   -> build/usb-package/VVVVVV/   (must end with "checksums OK")

# copy to the stick (FAT32, MBR); replace /run/media/$USER/STICK with your mount point
cp -r build/usb-package/VVVVVV /run/media/$USER/STICK/
(cd /run/media/$USER/STICK/VVVVVV && sha256sum -c --quiet MANIFEST.sha256 && echo stick OK)
sync    # then eject the stick from the file manager / udisksctl power-off
```

## 9. Minimal first-boot procedure

Full checklist: [ps2-hardware-first-boot.md](ps2-hardware-first-boot.md), also in the release as `FIRST-BOOT.md`.

1. FMCB card in slot 1, pad in port 1, stick in a front port; boot wLaunchELF.
2. `mass:/VVVVVV/fs_smoke.elf` → must end with **ALL OK**. Photograph the screen and reset.
3. `mass:/VVVVVV/VVVVVV.elf`:
   - the boot list shows **H1-RC2**;
   - the title screen appears; ✕ → menu music;
   - L3+R3 → overlay (in a room expect TPS ≈ 29.4, UNDERRUN 0);
   - new game; save via △ → SAVE; reset; continue.
4. Copy `optional/bench.txt` next to the ELF and launch. Leave it about 4 min until **BENCH DONE**, then reset and delete `bench.txt`.

## 10. What to bring back

**Files**, from `VVVVVV/` on the stick, copied on a PC after the session:
- `fs_smoke.log`;
- `vvvvvv_ps2.log`;
- `bench_results.txt`, `bench_*.csv`, `prof_*.txt`, `golden_*.ppm` (benchmark);
- the whole `savedata/` folder.

**Photographs:**
- the `fs_smoke` result screen;
- the boot list, if it stops or shows red lines;
- any FATAL ERROR or CPU EXCEPTION screen, whole and legible;
- the overlay in a normal room and in the Tower;
- the TV picture edges (overscan).

**Notes:**
- console model (sticker on the bottom, e.g. SCPH-75004);
- TV and cable;
- wLaunchELF and FMCB versions;
- USB stick model and size;
- time from launch to title;
- what was heard: music, gaps, clicks;
- what you did just before any failure.
