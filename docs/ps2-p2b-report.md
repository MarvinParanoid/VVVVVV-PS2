# P2b Report: Streamed Vorbis music on PS2 (PCSX2)

**Date:** 2026-10-22

**Base:**
- VVVVVV `2.4-updates` @ `79395fa`.
- SDL 2.32.10 + `patches/sdl2`.
- Same toolchain image as P2a.

**Related:**
- [ps2-p2a-report.md](ps2-p2a-report.md)
- [vvvvvv-cross-platform-port-analysis.md](vvvvvv-cross-platform-port-analysis.md) (hybrid music strategy)

All runtime results below are from **PCSX2 v2.8.2** (software GS renderer, BIOS SCPH-75001 v2.20). **Nothing in this report has been validated on a physical PS2** (§7).

## 0. Status

| Requirement | Status | Evidence |
|---|---|---|
| Original `data.zip` preserved; separate PS2 archive | ✅ | `scripts/repack-data-zip.py data.zip [data-ps2.zip]`. It refuses an output that resolves to the input. The source SHA-256 is unchanged after the run |
| Music blob byte-identical after repack | ✅ | SHA-256 of `vvvvvvmusic.vvv` is `74e3487d…27bb1` in both input and output. `testzip` passes. If either check fails, the tool deletes its output |
| Only metadata + one decoder + bounded buffers in RAM | ✅ | A 7.5 KB header table read at init gives 16 `{offset, size}` sources. In flight at any time: one `stb_vorbis` decoder (~213 KB), a 1 s float ring (345 KB), one pair of 17.6 KB voice buffers and a 32 KB PhysFS read buffer |
| Never load the whole blob or a whole track | ✅ | No full-entry read exists in the streaming path. Peak `malloc_in_use` with music is 17.16 MiB, vs 16.42 MiB without music (§3.3) |
| Decode and I/O off the render thread | ✅ | All of it runs in one worker thread. The audio callback only copies from the ring. The worker is time-sliced so it cannot hold the game thread off for long (§2.4) |
| Track switch, loop, pause/resume, SFX during music | ✅ (PCSX2) | 18-min gameplay soak plus 18-min title-screen soak (§3.2) |
| 115 ms decoder-init cost and loop gaps | ✅ measured | Track open: ~130 ms CPU, ~375 ms wall (time-sliced). Loops: 0 underruns, ring ≥ 0.93 s at every loop boundary, no reopen (§3.4) |
| Gameplay benchmark, music on vs off | ✅ (PCSX2) | Render mean 20.97 vs 18.68 ms (+2.3 ms). Details in §3.1 |
| Underruns / frame mean / P95 / max / peak memory | ✅ | **0 underruns** in every run since the scheduling fix. Numbers in §3 |
| Missing or incompatible archive handled | ✅ (host + PCSX2) | Clear log lines; the game continues without music (§3.5) |
| PS2 changes isolated; reproducible build | ✅ | The `VVV_MUSIC_STREAMING` / `PS2_MUSIC` CMake option. `--refetch` rebuild gives bit-identical ELFs (§1) |
| No ADPCM / GPU render targets / PAL / memory card | ✅ | None of these were touched |
| Real hardware | ⏸ | Not tested (§7) |

## 1. Build and run

```sh
scripts/build-vvvvvv-ps2.sh --refetch            # export upstream, apply patches/vvvvvv/*, build both variants
python3 scripts/repack-data-zip.py ~/.cache/vvvvvv-ps2/assets/data.zip   # -> data-ps2.zip next to it
PCSX2_BIOS=/path/bios.bin scripts/run-vvvvvv-pcsx2.sh                    # picks data-ps2.zip if present
```

**Build outputs** (`out/vvvvvv-ps2/`):

| ELF | CMake | sha256 |
|---|---|---|
| `VVVVVV.elf` | `-DPS2_MUSIC=ON` (default) | `de5e1251…32652a6` |
| `VVVVVV-nomusic.elf` | `-DPS2_MUSIC=OFF`, P2a behaviour | `f1c62a65…fa7e` |

A clean `--refetch` rebuild reproduced both hashes exactly. Streaming adds about 6 KB of `.text` and 8 KB of `.bss` (`size`: text 4,948,543 / bss 2,349,196 vs 4,941,987 / 2,341,260).

**Run-script switches added in P2b:**
- `PAD_SCENARIO=newgame-explore|idle`: a uinput virtual Xbox 360 pad (`scripts/virtual-pad.py`) starts a new game and walks and flips through rooms deterministically.
- `SOAK=1`: the music soak driver (§3.2).
- `LOG_TAG=name`: logs go to `logs/pcsx2/<name>/`.
- `PCSX2_OSD_FPS=1`: PCSX2's FPS/VPS overlay.
- `scripts/summarize-ps2-log.py` aggregates a run.

**Patch series** (`patches/vvvvvv/`):

| Patch | Content |
|---|---|
| 0001 | PhysicsFS PS2 platform (unchanged) |
| 0002 | CMake: `PS2_MUSIC` option (ON → `MUSIC_STREAMING`), `-ldebug` |
| 0003 | `PS2Platform.*` (instrumentation, soak driver); `FileSystemUtils.cpp` prefers `data-ps2.zip` |
| 0004-music-streaming | `Music.cpp` / `Music.h`. Replaces P2a's `0004-ps2-disable-music-blob` |

All streaming code sits under `#ifdef VVV_MUSIC_STREAMING`. With the option OFF, the host build compiles and behaves as upstream; this was checked by building both host variants.

## 2. Implementation

### 2.1 Archive

The music blob must be a **stored** (uncompressed) zip entry or a plain file. For a deflated entry, PhysFS must inflate from the start of the entry on every seek. In P2b research on the EE, reaching a track 57 MB into the blob took 2.6–7.6 s, against 0.07 ms when the entry is stored.

`repack-data-zip.py` rewrites only `vvvvvvmusic.vvv` / `mmmmmm.vvv` as `ZIP_STORED` and copies every other entry with its original compression. Output: 63.4 MB for data-ps2.zip vs the original 61.6 MB.

On PS2, `FileSystemUtils.cpp` mounts `data-ps2.zip` instead of `data.zip` when it exists and logs `Assets archive: …`.

### 2.2 Index (init)

- `musicclass::init` reads only the blob's 128-entry header table (7,680 B) and probes each track's 128-byte Vorbis identification header for channels and rate.
- Track numbering is identical to upstream: MMMMMM tracks first, then PPPPPP, extras, then loose `music/*.ogg`.
- Each track is stored as `{path, offset, size, channels, rate}`.
- `seekable()` reads the zip central directory and refuses a deflated entry (§3.5).

### 2.3 Streaming pipeline

```
game thread ── play()/pause()/halt() ──► request(source, loop) ─┐ (latest wins, generation number)
                                                                ▼
worker thread: PHYSFS_openRead + seek ─► stb_vorbis_open_file_section ─► decode 1024-frame chunks
               (one decoder; old one closed first)                        └► float ring, 1 s (345 KB)
                                                                                    │
SDL audio thread: FAudio OnBufferStart ─► consume(): memcpy out of ring ◄──────────┘
```

**Ring.**
- Monotonic `wpos`/`rpos` frame counters (atomics), so the ring is single-producer/single-consumer and lock-free.
- `consume()` returns silence while a request is still being opened. This is counted as `priming`, not as an underrun.

**Preroll.** A new track becomes audible only after 4,096 frames (93 ms) have been decoded. Without this, the first buffer after a switch underran (observed once: 1,181 frames).

**Loops.**
- A whole-track loop calls `stb_vorbis_seek_start` inside the worker and keeps writing into the same ring. Nothing is reopened.
- Tracks with `LOOPSTART`/`LOOPLENGTH` tags use `stb_vorbis_seek(loopbegin)` at the loop end.
- A non-looping track (the jingles) sets an EOF generation. `consume()` then returns a short buffer and the voice ends, as upstream.

**Halt/switch.** `Halt()` destroys the voice and posts `request(-1)`, so the decoder is freed immediately.

**Voice buffers.** All streamed tracks share one pair of 50 ms FAudio buffers, since only one music voice exists at a time. An earlier version allocated a pair per track: 16 × 35 KB = 564 KB.

### 2.4 Scheduling (the main P2b finding)

The EE kernel scheduler is strictly priority based, with no time slicing between priorities.

**Starvation at LOW priority.** The first design ran the worker at LOW priority (111), below the game thread (64), and boosted it when the ring ran low. In gameplay this starved the worker completely:
- Frames take 19–25 ms, longer than the 16.7 ms vsync period.
- With dynamic vsync, the SDL renderer then skips its `WaitSema`, so the game thread never sleeps.
- The first track open took **12.8 s**, and the second never finished within 50 s.
- Playback was silent; the user confirmed this by ear.

**Fix: worker above the game thread, but cooperative.**
- The worker runs at NORMAL priority (50), above the game thread.
- It works for at most ~2 ms, then calls `SDL_Delay(1)`.
- While opening a track, the budget is 1 ms of work then 2 ms of sleep, so the game keeps about 2/3 of the CPU.

**Yield points inside stb_vorbis.** Outside stb, the yield checks run between decode chunks. Inside stb they run from:
- the `SDL_RWops` read callback, every 64 reads (stb reads mostly one byte at a time);
- stb's `sin`/`cos`/`qsort` calls, through the existing `#define` remapping in `Music.cpp`. The third-party file is not modified.

Without the trig hooks, building the window and twiddle tables (soft-float double on the EE) held the CPU for **106 ms in one piece**. That produced 160–240 ms frames at every track change. The hooks are armed by an `in_stb` flag set only by the worker: a first attempt used `SDL_ThreadID()` comparison, which never matched on PS2.

**Result.** The longest uninterrupted worker stretch is now 3–5 ms, reported as `max slice` in the logs.

## 3. Measurements (PCSX2)

**Scenario.** `PAD_SCENARIO=newgame-explore`: a fresh save, new game, intro text skipped, then a deterministic left/right/flip walk through the first rooms around (113–115, 103–105). 300 s per run.

**Timer resolution.** On this target `SDL_GetPerformanceCounter` resolves to 1 ms, so open, decode and slice timings are quantised to 1 ms.

**How the statistics are aggregated.** FRAME statistics are 300-loop windows, gameplay windows only (`gamestate=0`). "P95" is the median and the worst of the per-window P95s, not a per-frame percentile.

### 3.1 Gameplay: music on vs off

| | Music OFF (`VVVVVV-nomusic.elf`) | Music ON (`VVVVVV.elf`) |
|---|---|---|
| Gameplay loops measured | 15,000 | 13,500 |
| Frame period mean | 18.94 ms | 21.29 ms |
| Render mean | 18.68 ms | 20.97 ms |
| Render P95 (median / worst window) | 20.30 / 35.90 ms | 26.07 / 35.89 ms |
| Frame period max | 61.30 ms | 101.62 ms |
| Peak `malloc_in_use` | 16.42 MiB | 17.16 MiB |
| Min heap headroom | 7.09 MiB | 6.34 MiB |
| Underruns | n/a | **0** |

**Cost of music.**
- Render mean rises by +2.3 ms per frame (+12%). This is the expected price of software Vorbis: about 9% of the EE at 198 kbps.
- The per-window frame maximum is 40–45 ms without music. With music it is 45–65 ms only in the window that contains a track open, and the same otherwise.
- Before the scheduling fixes (§2.4), the same windows peaked at 160–241 ms.

**Effective frame rate.** Mean presentation interval ~21 ms, about 47 FPS with dynamic vsync. Game logic still runs at its fixed 30 Hz.

### 3.2 Soak tests

**Driver.** The soak driver (`SOAK=1`, in `PS2Platform.cpp`) uses the public music API on a fixed schedule:
- `play(n)` for each of the 16 tracks, 20 s each, including the non-looping jingles 0 and 7;
- song 10 (6:09) for 420 s, so it crosses its loop boundary;
- song 1 for 240 s, then song 4;
- a 3 s `pause()`/`resume()` every minute;
- `playef()` with a different sound effect every 5 s.

| Run | Length | Opens | Underruns | Loops | Max slice | Notes |
|---|---|---|---|---|---|---|
| Gameplay soak (`p2b-soak`) | 18 min, 36,300 gameplay loops | 16 | **0** | 2 (song 1, whole-track) | 4 ms | Render mean 20.97 ms, P95 25.85 / 35.98 ms, max period 99 ms, peak 17.67 MiB. The game's own area music overrides most soak switches (it re-selects song 1 on room changes), so most tracks were covered by the title soak |
| Title soak (`p2b-soak-title`) | 18 min | 19 | **0** | 4 | 4 ms | All 16 tracks opened, including jingles 0/7 ending by EOF; song 10 crossed its 6:09 end and looped; 15 pause/resume cycles; an SFX every 5 s. Ring never below 40,878 frames. Render mean 15.21 ms vs 13.78 ms on the title screen with no track playing (+1.4 ms) |

Builds used:
- **Gameplay soak:** build `cb76b288…`. It differs from the final ELF only in two ways: each track has its own voice buffers (§2.3), and the soak driver calls `niceplay()`.
- **Music-on run (§3.1) and title soak:** build `49be7a27…`.
- **Final ELF:** differs from `49be7a27…` only by a larger log-line buffer (512 B). A 70 s smoke run on the final ELF confirmed: 0 underruns, 3 opens of ~368 ms, max slice 3 ms.
- **Music-off run:** used the P2b no-music ELF `fd8db693…`. The log-buffer change is its only difference from the final no-music ELF.

**Audible glitches.**
- Logs: no underrun was recorded in any run after the scheduling fix.
- By ear: the user confirmed that music plays in gameplay.
- A recording-based check (PCSX2 audio dump) was **not** done, so clicks inside correctly delivered buffers cannot be ruled out. This is not expected from the design, but it is not verified.

### 3.3 Memory

**Music build vs no-music build:** +0.74 MiB peak.

| Item | Size |
|---|---|
| Ring buffer | 352,800 B |
| Decoder heap (stb setup + one decoder) | ~213 KB (P2b research) |
| Worker thread stack | 128 KB (`SDL_HINT_THREAD_STACK_SIZE`) |
| Shared voice buffers | 2 × 17,640 B |
| PhysFS read buffer | 32 KB |
| FAudio music voice (float, 44.1 kHz) | small |
| Metadata | 16 × ~100 B |

### 3.4 Decoder init (the 115 ms) and loop boundaries

**Open cost.**
- `stb_vorbis_open` costs ~130 ms of EE CPU per track, mostly window/twiddle tables in soft-float double plus codebook setup. That is 0.34 ms on a desktop host.
- It now runs time-sliced, at 1 ms work / 2 ms sleep, so wall time is **~375 ms** (368–378 ms across 16+ opens).
- The game keeps running throughout. Affected frames rise from ~20 ms to at most 45–65 ms.

**Start latency.**
- A music change becomes audible ~0.47 s after `play()`: the open plus the 93 ms preroll.
- `play()` fades in over 3 s, and `niceplay()` first fades the old track out, so the delay sits inside an existing fade.

**Loop boundaries.**
- Loops never reopen the decoder. `stb_vorbis_seek_start` took ≤ 1 ms in every measurement.
- At every loop the ring held ≥ 40,878 frames (0.93 s), so the audio thread never saw a gap.
- The decoded stream continues in the same ring and voice. Loop behaviour is therefore sample-identical to upstream's in-memory path, which also calls `stb_vorbis_seek_start` at end of stream.
- None of the 16 PPPPPP tracks carry `LOOPSTART` tags. The tagged loop-region path (`stb_vorbis_seek`) is implemented but **not exercised with real data**.

**Ways to reduce the open cost further (not done):**
1. **Pre-open the next track.** Not possible in general: the game does not announce the next song.
2. **Reuse the setup data.** Cache stb's computed window/twiddle tables across opens; every PPPPPP track is 44.1 kHz with the same block sizes. This needs an stb change, so it was left out to keep third_party untouched.
3. **Change the format.** ADPCM/SPU2 (excluded from P2b) removes decoding entirely.

### 3.5 Missing / incompatible archives

| Case | Where tested | Result |
|---|---|---|
| Original deflated `data.zip` | host + PCSX2 (P2b smoke) | `Music: vvvvvvmusic.vvv is compressed (zip method 8) …`, `music is DISABLED`, and the exact `repack-data-zip.py` command. The game continues; SFX work |
| `data-ps2.zip` with no music blob | host | `Music: no blob, trying loose files...` then `no playable music found; continuing without music` |
| Blob replaced by 1 MB of random bytes | host | `indexed vvvvvvmusic.vvv: 0 entries …` then `no playable music found`. No crash |
| Track whose Vorbis header does not parse | code path only | The probe fails and the track is skipped at init. A later open failure logs `Vorbis open failed for source N (error E)` and increments `open_failures`. Not exercised with real data |

## 4. Problems found and fixed during P2b

1. **Virtual pad never reached the game.** PCSX2 numbers SDL devices in enumeration order, and the user's wireless keyboard dongle was SDL-0. The pad is now bound on SDL-0..3 when `PCSX2_VIRTUAL_PAD=1`.
2. **Worker starvation.** 12.8 s open, then silence (§2.4).
3. **First-buffer underrun after a track switch.** Fixed by the preroll (§2.3).
4. **106 ms non-preemptible stretch inside `stb_vorbis_open`.** Fixed with the trig/qsort yield points; the `SDL_ThreadID` gate replaced by the `in_stb` flag (§2.4).
5. **564 KB of per-track voice buffers.** Now shared (§2.3).
6. **Soak driver on the title screen did nothing.** `niceplay()` waits for `safeToProcessMusic`; the driver now uses `play()`.

## 5. Limitations

- **Emulator only.** PCSX2 HostFS reads complete with HLE timing, but real hardware reads come from USB mass storage, a network share or HDD, each with very different latency (§7).
- **CPU cost.** Music costs ~2.3 ms per frame (~12%) on an already CPU-bound software renderer. The game runs at ~47 FPS presented; logic stays at 30 Hz.
- **No fully gap-free track start.** There is ~0.47 s from `play()` to audible music, partly masked by the fade-in.
- **Loop-tag path untested** with real data (§3.4).
- **Timer granularity of 1 ms** limits how finely open, decode and slice times are measured.
- **No audio recording analysis.** "No glitches" means "no underruns logged, nothing heard", not a waveform comparison.
- **The soak cannot force tracks during gameplay**, because the game's area music takes over. Full track coverage comes from the title-screen soak, at lower render load.
- **`data-ps2.zip` is 1.8 MB larger** than `data.zip`.

## 6. Files

- `scripts/repack-data-zip.py`: lossless repack with SHA-256 verification.
- `scripts/summarize-ps2-log.py`: run summary (FRAME windows, MEM, MUSIC).
- `scripts/virtual-pad.py`, `scripts/pcsx2-common.sh`, `scripts/run-vvvvvv-pcsx2.sh`: automated runs, pad binding, OSD toggle.
- `scripts/build-vvvvvv-ps2.sh`: builds both variants with an explicit `-DPS2_MUSIC`.
- `patches/vvvvvv/0002`, `0003`, `0004-music-streaming.patch`.
- Logs (not committed):
  - `logs/pcsx2/p2b-on/` (music on);
  - `logs/pcsx2/p2b-off/` (music off);
  - `logs/pcsx2/p2b-soak/` (gameplay soak);
  - `logs/pcsx2/p2b-soak-title/` (title soak);
  - `logs/pcsx2/p2b-slice/` (scheduling diagnosis).

## 7. Remaining hardware-validation tasks (SCPH-7500x)

Not one of these has been run. Real-hardware compatibility is **not** claimed.

1. **Boot path.** There is no Free McBoot or other loader yet. The ELF and `data-ps2.zip` must be reachable from the same device (`mass:` USB, or `host:` via ps2link).
2. **Storage latency.**
   - Measure `PHYSFS_seek` + open and sustained reads from USB 1.1 mass storage. The required rate is ~25 KB/s of music data, plus the 32 KB buffer refills.
   - Check that blocking IOP RPC reads in the worker do not stall the game thread. They should not, since the worker sleeps in RPC, but this is unverified.
3. **Scheduler behaviour.** Confirm that `SDL_Delay(1)` (ps2sdk `nanosleep`) gives the expected ~1 ms sleep granularity on the real kernel, and that `max slice` stays ≤ 5 ms.
4. **Decode cost on the real EE.** PCSX2's EE timing is approximate. Re-measure `decode avg`, the open time and the render delta, especially soft-float double in `stb_vorbis_open`.
5. **Audio output.** Check the SPU2 path (SDL `ps2` audio driver, audsrv) for clicks at buffer boundaries, at loop points and during pause/resume, by ear and with a capture if possible.
6. **Long soak on hardware.** Run ≥ 30 min with `SOAK=1`, checking underruns, memory headroom (currently 6.34 MiB) and thermal or IOP stability.
7. **Archive handling on real filesystems.** FAT32 USB drives, file names, and the 63 MB file size.
