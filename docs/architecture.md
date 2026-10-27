# Architecture

How the port works and why it is built this way. Measurements are labelled by where they were taken: **PCSX2** (v2.8.2, software GS renderer) or **console** (PS2 Slim SCPH-7500x, the only console tested; see [known-issues.md](known-issues.md#compatibility)). The full phase reports with methods and raw data are in [archive/](archive/README.md).

## Starting point

**The PS2:**
- a 294.912 MHz MIPS R5900 (the EE) with a single-precision FPU only, so `double` maths runs in software;
- 32 MiB of main RAM;
- a GS that draws textured sprites quickly but offers SDL no general render targets.

**What existed.** SDL 2.32.10 has a PS2 port (gsKit renderer, audsrv audio, libpad input). VVVVVV's SDL2 branch (`2.4-updates`) builds against SDL2, PhysicsFS and FAudio. VVVVVV draws everything through render targets, which the gsKit renderer does not implement, so the game uses SDL's software renderer.

**Approach.** Nothing is forked. Every change is a patch on a pinned upstream source:
- `patches/sdl2/` (10 patches);
- `patches/vvvvvv/` (5 patches, including the PhysicsFS platform patch).

PS2 code is guarded by `__PS2__` / `__ps2sdk__` or CMake `if(PS2)`. Game logic is unchanged.

## Rendering

**GS-assisted scaling.** The window is a 320×240 surface. The GS stretches it to the 640×448 NTSC frame:
- final copy: 148.7 → 4.8 ms;
- present: 2.6 → 1.4 ms (PCSX2).

The framebuffer is a 32-bit XBGR8888 texture, the GS's native order, so the upload is a plain copy. Vertex colour and texture alpha are scaled to the GS range, where 0x80 means 1.0. The stock path showed frames about 1.6× too bright.

**gsKit queue guard.** gsKit does not bounds-check outside debug builds. The patch checks space before each command and flushes early.

**Software renderer optimisations** (P1c, P3):
- **Bottleneck:** VVVVVV's textures are ABGR8888 but its render targets were ARGB8888, so every blit swapped channels in SDL's generic loops, 20–39% of all CPU samples. Render targets now use the textures' layout.
- **Same-format 8888 fast paths:** for modulated, blended, scaled and opaque copies.
- **Soft-float removed:** four soft-float `double` divisions per draw call are gone.
- **Less work per frame:** dead full-screen clears are dropped, and the window framebuffer lives in the GS texture's memory.
- **Correctness:** every change is checked with golden-frame CRCs and stays bit-identical.

**Speed.** VVVVVV's logic runs on a fixed 34 ms step (29.4 ticks/s), and a late frame slows the game rather than skipping. So the measure is logic ticks per second, not FPS. `over30mode` is off by default on PS2.

| | PCSX2 | Console (H4-RC1, normal rooms) |
|---|---|---|
| Render time | 14.6–17.4 ms in normal rooms; 24.0 ms in the worst scene | ~17.5 ms; work 17–18 ms per 34 ms tick |
| Game speed | full speed in 8 of 10 benchmark scenes; Tower and Panic Room ~25 of 29.4 | 29.5 ticks/s (10 s minimum 29.0); Tower and Panic Room not measured |

**Start-up picture.** Two black frames are presented as soon as the renderer exists. Without them, the GS showed leftover VRAM, the previous program's menu picture, during the ~4 s resource load. This was only visible on the console.

## Music and audio

**Problem.** Upstream loads the 61.7 MB `vvvvvvmusic.vvv` into RAM.

**Solution:**
- **Archive:** `scripts/repack-data-zip.py` writes `data-ps2.zip` with that blob stored uncompressed, so it can be seeked.
- **Worker:** one worker thread streams the current track (PhysicsFS seek, `stb_vorbis`, a lock-free 1-second ring). The audio callback only copies out of the ring.
- **Memory:** one decoder at a time, about 213 KB.

**Scheduling.** The EE scheduler is strictly priority-based. A low-priority worker starved during gameplay (12.8 s to open the first track). The worker therefore runs above the game thread and yields after about 2 ms of work. The yield points are `stb_vorbis`'s I/O callbacks; the library itself is unchanged.

**Results:**
- **PCSX2:** 0 underruns, including 18-minute soaks.
- **Console:** 0 underruns and 0 late audsrv submissions in sessions s5–s7; decoding costs about 171 ms per second; the audsrv queue never fell below 74 ms.
- **Known limit:** music starts about 0.5 s after a track change.

## Memory

| Point | malloc in use |
|---|---|
| After `SDL_Init` | 0.05 MiB (PCSX2) |
| First frame (graphics, fonts, sounds, render targets) | ~17.1 MiB (PCSX2); 16.5 MiB (console, s5 log) |
| Peak in gameplay with music | 16.87 MiB (PCSX2); 6.3 MiB of heap free on the console (s6 overlay) |

Only English text and the Latin fonts are packaged. The CJK font sheets would exceed the GS texture size limit and cost about 33 MB at 32 bpp.

## Files, saves and the ps2sdk seams

The game reads `data-ps2.zip`, `lang/` and `fonts/` through PhysicsFS. It writes `savedata/` and `vvvvvv_ps2.log` next to the ELF on the boot device (`mass:` = USB). PhysicsFS uses its POSIX platform layer on top of ps2sdk's libc (newlib + libcglue, fileXio RPC, and the IOP FAT driver `bdmfs_fatfs`).

The first console sessions found five problems that PCSX2 cannot show. PCSX2's `host:` device returns host errors, not the FAT driver's. All five are fixed:

| Symptom on the console | Cause | Fix |
|---|---|---|
| Hang at the first missing file (start-up) | the FAT driver returns FatFs result codes; "no file" (`FR_NO_FILE` = 4) arrives as errno 4 = `EINTR`, and PhysicsFS retried `open()` forever | no EINTR retry on PS2 (`patches/vvvvvv/0001`) |
| Every save 0 bytes | `fsync()` always fails (`ENOSYS`); `PHYSFS_close` then returned **without closing**, and FatFs writes the size only at close | no `fsync` in PhysicsFS's flush on PS2 (`0001`) |
| `lang/en` not found; a false ERROR at start-up | `stat()` of a missing path sets errno 1 (libcglue's `_stat` overwrites the fileXio helper's `ENOENT`), and PhysicsFS stops its search-path walk on any error except "not found" | a failed stat is "not found" on PS2 (`0001`) |
| `WARN mkdir … errno 8` at every start | `mkdir` of an existing directory returns `FR_EXIST` (8), not `EEXIST` | `PS2Mkdir.h`: after a failed `mkdir`, an existing **directory** counts as success (`0003`) |
| Left stick dead | SDL never switched the DualShock 2 to analog mode | `padSetMainMode(DUALSHOCK, LOCK)` once per connection (`patches/sdl2/0010`) |

Traced in ps2sdk at `329e2bb`:
- **open, getstat, mkdir, remove, rename:** `bdmfs_fatfs` returns `-FRESULT` for all of them;
- **read and write:** on a FatFs error they return **0 bytes**, not an error;
- **libcglue:** `fsync` is always `ENOSYS`.

On `host:` (PCSX2) the codes differ. The fixes therefore do not depend on specific values; they check the path or never retry.

`tests/ps2/fs_semantics` (host test, in CI) builds the real PhysicsFS as the PS2 platform with these codes emulated. It fails on both earlier versions: the original PhysicsFS hangs, and the H3 version misses `lang/en`. The PS2 probes `stat_probe`, `write_probe`, `physfs_probe` and `io_bisect` reproduce the cases on a console.

**Saves on the console.** Saves were written, survived a full power-off and were restored by Continue (session s7). Untested:
- a full or write-protected stick;
- power loss during a save.

The save is truncated before writing, and a FAT error shows up only as a short write. The game logs a warning and recreates a broken file at the next start.

## Input

DualShock 2 through SDL's PS2 joystick driver, with all 8 pad slots opened through the multitap driver.

| Input | Status |
|---|---|
| D-pad, buttons | console |
| Left stick | console, with patch 0010 |
| Unplug/replug | **untested** (the code re-arms analog mode on reconnect) |
| Third-party pads, multitap use | **untested** |

## Diagnostics

Built for a console with no debugger:

| Tool | What it does |
|---|---|
| Boot console | text screen with each start-up step before graphics exist; a hang shows the last step |
| Fatal-error screen | last stage, last error and a hint |
| CPU exception screen | registers and build ID on screen and in the log; the report runs on its own 16 KB stack, and a nested fault parks the thread instead of spinning |
| Overlay (**L3+R3**) | OFF → COMPACT (`TPS … PCM … HEAP …`) → PERF (ticks/s, frame, render, music, heap) → DIAG (Flip Mode paths, storage log, audsrv queue, late submissions, estimated dry time, last error) |
| Benchmark | `bench.txt` next to the ELF: fixed scenes hands-off, `bench_results.txt` and golden-frame CRCs |
| `fs_smoke.elf` | standalone USB self-test: device, read speed, seek latency, CRCs, write/delete |
| Log | `vvvvvv_ps2.log`; a writer thread flushes every 10 s on USB. It is rewritten at every start |

**Symbolising a crash.** Use the unstripped ELF of the same build: the release's `-symbols` folder, or `out/vvvvvv-ps2/`.

```sh
scripts/ps2-profile.py <unstripped VVVVVV.elf> --addr <EPC> <RA>
```

**Developer-only switches**, never shipped in packages; `check-package.py` rejects them next to the ELF. These files next to the ELF change behaviour:

| File | Effect |
|---|---|
| `crashtest.txt` | deliberate exception after ~20 s (`badsp`, `nested`) |
| `audiostall.txt` | holds the audio lock to test the audsrv counters |
| `iodiag.txt` | per-asset file tracing, synchronous log |
| `music_soak.txt` | scripted music soak |
| `storage_real.txt` | treat `host:` as real storage |
| `snapshots.txt` | periodic snapshots |

The overlay, its exact metrics and costs, and the exception-stack details are documented in [archive/ps2-hardware-diagnostics.md](archive/ps2-hardware-diagnostics.md) (written before the console tests).

## Reproducible builds

- The toolchain image is pinned by digest, SDL by SHA-256, VVVVVV by commit, and patches by content.
- Builds from a clean checkout are bit-identical on the build host. Verified for H3 and H4-RC1, see [building.md](building.md#reproducibility).
- Across machines and the Docker backend: [releasing.md](releasing.md#continuous-integration).
