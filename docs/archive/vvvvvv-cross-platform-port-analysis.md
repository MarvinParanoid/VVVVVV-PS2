# VVVVVV ports on constrained hardware: what P2b (PS2 music) should do

**Date:** 2026-10-22

**Status:** research only. No PS2 port code or dependencies were changed. Two research tools were added: `tests/ps2/music_bench`, plus `scripts/run-music-bench.sh` and `scripts/repack-data-zip.py`.

**Detail report:** [dreamcast-port-analysis.md](dreamcast-port-analysis.md).

**Evidence labels:**
- **[V] verified** in source at the listed revision.
- **[M] measured** by us. Measurements come from PCSX2 v2.8.2 with BIOS SCPH-75001 and are not hardware-validated.
- **[A] assumption / estimate.**

## 1. Executive summary

**None of the existing ports decodes Vorbis at runtime for music.**
- **Dreamcast** pre-decodes to CD-DA disc tracks.
- **PSP** pre-converts to ATRAC3 and decodes on the Media Engine. Its author tried runtime Ogg (Tremor) and removed it after six days.

Both rely on hardware that the PS2 does not have in a usable form. CD-DA is impossible from USB or DVD; there is no EE-side hardware audio decoder.

**Our own measurements on the EE are favourable to runtime Vorbis** [M]:
- stb_vorbis decodes VVVVVV's 44.1 kHz stereo, ~198 kbps tracks at **~88–90 ms of CPU per second of audio (~9% of the EE)**;
- the worst 50 ms chunk costs **7.4 ms**;
- a decoder uses **213 KB** of heap.

**What is not viable is streaming from the original `data.zip`.** The music blob is a deflated zip entry, and reaching a track start costs **2.6–7.6 s** [M]. The same seek in a stored entry costs **0.07 ms** [M].

**Recommendation: hybrid.**
- Runtime streaming of the **original, unmodified Vorbis bytes**, one track and one decoder at a time.
- The music blob is read from an **uncompressed (stored)** container, produced by a one-time, lossless, user-side repack of the user's own `data.zip` (`scripts/repack-data-zip.py`).
- **No transcoding.** Preconverted SPU2 ADPCM is not justified now: 63–125 MB of storage, audible quality risk, and an IOP streaming driver that does not work as shipped. It stays the fallback if real hardware disproves the CPU measurement.

## 2. Repository inventory

| Repository | Revision examined | Status | Contents |
|---|---|---|---|
| **PSP:** https://github.com/dntrnk/VVVVVV-PSP | `196deb758407fb5c1d974998cf21c2815d5c233d` (2026-10-15) | Active (162 port commits, 2026-08-28 → 2026-10-15) | Full fork of VVVVVV **2.4.4** (`ea811e15`). SDL removed; renderer is glib2d over sceGu [V] |
| **PSP music tool:** https://github.com/dntrnk/VVVVVV-MUSIC-TO-PSP | `a8eedd1802291b532d82381cad7b30ed6a5cceb3` (2026-10-11) | 5 web uploads; **not buildable as committed** (`dr_libs/dr_wav.h` is an empty file) | One Windows C file, `main.c` (MIT): blob → Ogg → WAV → Sony `at3conv.exe` [V] |
| **Dreamcast:** https://github.com/gusarba/VVVVVVDC | `47222b05f763b6b31e960035fb9e1c5d5fe52890` (2020-12-24) | Dormant since 2020 | Fork of pre-2.3 upstream `4511ea1`; SDL 1.2; CD-DA music [V] |
| **Libretro:** https://github.com/libretro/libretro-vvvvvv | — | **Does not exist publicly.** API and web return 404 (also for `libretro/VVVVVV` and `libretro/vvvvvv-libretro`); GitHub search returns 0 results; no core-info entry [V] | Not analysed. No claims are made about a libretro port |
| **VVVVVV-CE:** https://github.com/v6cord/VVVVVV-CE | `8cb1253ba6d2e15c8035465a730e80a8c8c37904` | **Archived** (last push 2020-11-17); base is pre-2.3 (upstream merges up to `5ce2c04`, 2020-07-06) | SDL_mixer, old surface renderer, Lua [V] |
| **Official:** https://github.com/TerryCavanagh/VVVVVV | `2.4-updates` `79395fa` (our base); master `40e1ec3` | Active | Baseline. Our patches: `patches/vvvvvv/0001–0004`, `patches/sdl2/0001–0002` |

## 3. Technical comparison

| | **PS2 (ours, P2a)** | **PSP** | **Dreamcast** | **VVVVVV-CE** |
|---|---|---|---|---|
| Upstream base | 2.4-updates (SDL2) | 2.4.4 | pre-2.3 (`4511ea1`) | pre-2.3 |
| Main RAM | 32 MB | 32 MB (PSP-1000) / 64 MB; built with `MEMSIZE=1`, the extended-RAM flag for PSP-2000+ | 16 MB | desktop / Switch |
| Music format at runtime | none yet (disabled) | **ATRAC3, 64 kbps**, pre-converted | **CD-DA**, pre-decoded | Ogg via SDL_mixer |
| Decoder | stb_vorbis (FAudio integration) | Media Engine (`sceAtrac*`) | drive hardware | libvorbis (SDL_mixer) |
| Streamed or RAM | — | whole `.at3` file `malloc`'d per track | streamed from disc | **whole blob in RAM** |
| Loop handling | upstream: loop points or whole track | whole-track rewind | CD-DA repeat count | `Mix_FadeInMusicPos` loops |
| SFX | 28 WAV → EE RAM PCM → FAudio → audsrv | WAV → RAM PCM → 8 hardware channels | AICA sound RAM (`snd_sfx`) | `Mix_Chunk` |
| Renderer | SDL2 software + GS-scaled texture framebuffer (patched) | sceGu quads, CLUT textures, no render targets | SDL 1.2, RGB565, colour key | 2.2-era surface pipeline |
| data.zip | mounted by PhysFS (zip) | mounted by PhysFS (zip) | extracted onto the disc, mounted as a directory | read fully into RAM on Switch/Android |
| Saves | files next to ELF (`host:`/`mass:`) | files next to EBOOT | VMU (zlib, fixed 1.5 KB) | SD card |
| FPS evidence | PCSX2 instrumentation | none | README claim only | none |

## 4. PSP music implementation [V]

Permalink prefix **P/** = `https://github.com/dntrnk/VVVVVV-PSP/blob/196deb758407fb5c1d974998cf21c2815d5c233d/desktop_version/`

**Format.**
- Tracks are hard-coded as `music/<N><name>.at3`, loaded from a loose folder next to `EBOOT.PBP` with `sceIo`, not through PhysFS (P/`src/Music.cpp#L106`, P/`src/FileSystemUtils.cpp#L1355`).
- The blob is not read at runtime.

**Whole file in RAM.** `LoadAt3` does `sceIoOpen`, then `malloc(fileSize)`, reads the whole file, then `sceAtracSetDataAndGetID` (P/`src/pspaalibat3.c#L185-L198`). The tool encodes at 64 kbps.
- [A] At 64 kbps a track is 0.5–3 MB, so one track in RAM is affordable.
- [A] At our measured Vorbis sizes (up to 8.8 MB per track), loading a whole track would not be affordable.

**Threads.**
- 8 playback threads (`aalibplayN`, priority 0x18, 32 KB stack), one per hardware channel (P/`src/pspaalib.c#L192`).
- Double-buffered 1024-frame `sceAudioOutputPanned` (P/`src/pspaalib.c#L146`).

**Loop and switching.**
- Whole-track loop via `sceAtracResetPlayPosition(…, 0)`.
- Track switch: fade out, then `AalibStop` + wait + unload, then load + play the next track (P/`src/Music.cpp#L29-L70`). There is one music voice and no crossfade.

**Abandoned runtime Ogg.**
- `a20a878f` (2026-09-13) added Tremor (`libvorbisidec.a`) with LOOPSTART/LOOPLENGTH parsing.
- `c59a836a` reduced its RAM use.
- `3471d20b` (2026-09-19) "Use .at3 instead of .ogg" replaced it; Tremor was deleted afterwards.
- The reason is not stated. [A] CPU or RAM on a 333 MHz Allegrex with the Media Engine available.

**Reusable leftovers.**
- `FILESYSTEM_loadBinaryBlobResource` (P/`src/FileSystemUtils.cpp#L1010`, unused in HEAD) reads only the 128-entry header table and computes one track's offset. That is the same index walk our design needs.
- The tool's blob walk is `main.c#L198-L280`.

**Custom-level music.** It requires converting the level's blob with the Windows tool; [A] it fails for zipped levels.

**Licensing.**
- Port code is under the VVVVVV licence.
- glib2d is CC BY-SA 3.0. pspaalib has no licence header, so **do not copy it**.
- The tool is MIT, but it depends on Sony's non-free `at3conv.exe`.

## 5. Dreamcast memory and rendering

The full analysis is in [dreamcast-port-analysis.md](dreamcast-port-analysis.md). In short:
- **Music:** pre-decoded CD-DA tracks, zero RAM, zero CPU. The README says blob Ogg streaming "was too slow and choppy"; no such code survives in the history.
- **SFX:** in AICA sound RAM.
- **Images:** RGB565 without alpha. The cost is the loss of translucency and of pixel-perfect collision.
- **data.zip:** extracted onto the disc.
- **RAM measurements:** none recorded.

## 6. Libretro audio architecture

**Not available.** `libretro/libretro-vvvvvv` and the obvious variants return HTTP 404 from the GitHub API and web, and repository search returns no results (checked 2026-10-22). No findings are reported; nothing is inferred from memory.

## 7. Our measurements (the evidence the decision rests on)

`tests/ps2/music_bench`, run with `scripts/run-music-bench.sh`; log at `logs/pcsx2/music_bench/music_bench.log`.

### 7.1 Music blob [M]

Parsed from the user's Make and Play `data.zip`; durations come from the last Ogg granule position.

| | Value |
|---|---|
| Tracks | 16, all 44.1 kHz stereo Vorbis |
| Total duration | 41.5 min |
| Total size | 61.6 MB |
| Average bitrate | 198 kbps (range 170–243) |
| Largest track | `10popularpotpourri` 8.8 MB, 6:09 |
| Shortest track | `5intermission` 7.6 s |
| Loop tags | none (`LOOPSTART`/`LOOPLENGTH` absent), so every track loops whole |

### 7.2 Container cost on the EE (PhysicsFS 3.2 zip archiver) [M]

| Operation | Deflated entry (stock data.zip) | Stored entry (repacked) |
|---|---|---|
| Fresh handle → seek to track 0 (offset 7.7 KB) | 3.2 ms | 0.07 ms |
| Seek to track 5 (19.5 MB) | **2,577 ms** | 0.07 ms |
| Seek to track 10 (31.7 MB) | **4,212 ms** | 0.07 ms |
| Seek to track 15 (57.8 MB) | **7,621 ms** | 0.07 ms |
| Seek back 2 MB (a loop restart) | same as above: re-inflates from 0 | 0.07 ms |
| Sequential read | 6.97 MB/s (inflate-bound) | 187 MB/s (`host:`, not representative of USB) |

### 7.3 Vorbis decode on the EE (stb_vorbis, VVVVVV's build flags, track in RAM: CPU only) [M]

| Track | Open | Decoder heap | CPU per audio second | Worst 50 ms chunk |
|---|---|---|---|---|
| 0levelcomplete (192 kbps) | 115 ms | 213 KB | 87 ms (8.7%) | 6.9 ms |
| 5intermission (243 kbps) | 115 ms | 213 KB | 88 ms (8.8%) | 7.1 ms |
| 10popularpotpourri (191 kbps) | 114 ms | 213 KB | 88 ms (8.8%) | 7.0 ms |
| predestinedfatefinallevel (238 kbps) | 115 ms | 213 KB | 90 ms (9.0%) | 7.4 ms |

**Cross-check [A]:** a published libvorbis figure for the PS2 EE, 30 s of q0 44.1 kHz stereo, is about 11–12% of 294.9 MHz (vorbis-dev, Feb 2003, https://lists.xiph.org/pipermail/vorbis-dev/2003-February/007057.html). The same order of magnitude. PCSX2 has no EE cache model, so hardware may be slower.

### 7.4 Storage for alternatives (computed from 41.5 min)

| Format | Size | Stream rate |
|---|---|---|
| Original Vorbis | 61.6 MB | ~25 KB/s |
| SPU2 ADPCM 44.1 kHz stereo | 125 MB | 50 KB/s |
| SPU2 ADPCM 22.05 kHz stereo | 63 MB | 25 KB/s |
| PCM16 44.1 kHz stereo | 439 MB | 176 KB/s |

## 8. The three PS2 strategies against evidence

| | 1. Runtime Vorbis streaming | 2. Preconverted SPU2 ADPCM | 3. Hybrid (lossless repack + runtime Vorbis) |
|---|---|---|---|
| EE CPU | ~9% average, 7.4 ms worst chunk [M] | ~0% (SPU2/IOP) [A] | same as 1 [M] |
| EE RAM | decoder 213 KB [M] + decode buffers (~35–140 KB) + PhysFS read buffer [A] | ~0; IOP 16 KB+ and SPU2 ~64 KB stream buffers [A] | same as 1 |
| Storage | 61.6 MB (unchanged) | 63–125 MB extra files | 61.6 MB (container rewritten; +2 MB vs deflated) |
| Quality | original | 4-bit ADPCM hiss on chiptune; 22 kHz is dull [A] | original (bit-identical audio data) |
| From stock data.zip | **No**: 2.6–7.6 s per track start or loop [M] | needs an offline tool anyway | needs a one-time repack (seconds on a PC) |
| Engine changes | `MusicTrack` streaming (lazy open, one decoder) | new music backend (SPU2 voice, volume, fades), IOP driver | as 1 |
| PS2 SDK support | exists (stb_vorbis, PhysFS, FAudio, audsrv) [V] | audsrv ADPCM is load-whole only (≤ ~2 MB SPU2); ps2snd streamer is broken as shipped (header/seek bug, core-0 IRQ, legacy ioman) [V per research] | exists |
| Toolchain | none | encoder (ps2sdk `adpenc`/`ps2adpcm`, AFL 2.0) + interleave | `scripts/repack-data-zip.py` (stdlib Python) |
| Biggest risk | hardware CPU/IO latency vs ~100 ms buffering | driver work, quality, storage | as 1, plus a user step |

**Decision.** Strategy 1 alone is blocked by the deflated container. Strategy 2 costs the most engineering and quality for a CPU saving we have not been shown to need. **Strategy 3 keeps the measured 9% CPU cost and removes the container problem with a lossless, scripted step.**

## 9. Reusable techniques, ranked by value / effort

| # | Technique | Source | Value | Effort | Portable? |
|---|---|---|---|---|---|
| 1 | Read only the blob header table; compute track offsets; open one track at a time | PSP `FILESYSTEM_loadBinaryBlobResource`, PSP tool `main.c`, our bench | High | Low | Yes |
| 2 | One-time asset preprocessing by the user, scripted | DC (PyOgg → CD-DA), PSP (→ AT3) | High | Low | Yes (ours is a lossless repack) |
| 3 | Single music voice; fade out → stop/unload → load/play; free the previous decoder | PSP `Music.cpp#L29-L70`, DC | High | Low | Yes (upstream state machine already does this) |
| 4 | Whole-track loops (no loop tags in the stock tracks) | PSP, DC, our parse | Medium (simplifies seeking: loop = seek to the first audio page) | Low | Yes |
| 5 | SFX in sound-chip RAM as native ADPCM | DC (`snd_sfx`, `rescueA.wav` ADPCM) | Medium (frees ~2.3 MB EE RAM and FAudio mixing) | Medium–High (audsrv ADPCM voices + volume/pan) | PS2 analogue exists (audsrv ADPCM); later phase |
| 6 | Pre-allocate long-lived textures at startup; CLUT4/CLUT8 textures | PSP `GraphicsResources::preserve` | Medium (RAM/fragmentation) | Medium | GS supports PSMT4/8, but our textures are software surfaces; later |
| 7 | Mount a small zip from memory with `PHYSFS_mountMemory` | CE | Low | Low | Yes, small archives only |
| 8 | Colour keys / RGB565 instead of alpha | DC | Low (breaks translucency and pixel-perfect collision) | — | **Do not copy** |
| 9 | Load whole data.zip or blob into RAM | CE | Negative | — | **Do not copy** |

## 10. Licensing and redistribution

**No code from these repositories is needed for the recommended plan.** The index walk and streaming fit in our own patch to `Music.cpp`.

| Source | Licence | Notes |
|---|---|---|
| VVVVVV forks | VVVVVV Source Code License v1.0 | Same as ours. Code may be studied and adapted non-commercially with attribution |
| PSP music tool | MIT | Depends on Sony's `at3conv.exe`, which is not redistributable |
| glib2d | CC BY-SA 3.0 | Share-alike: avoid mixing it in |
| pspaalib | no licence header | **Do not copy** |
| DC SDL 1.2 | LGPL 2.1 | |
| DreamHAL mem* | public domain | |
| ps2sdk tools (`adpenc`, `ps2adpcm`) | AFL 2.0 | Relevant only if we go ADPCM |

**Assets.**
- Upstream `License exceptions.md` lists per-console permissions to distribute images *with* data.zip for **Dreamcast** and **PSP** (and Wii), non-commercial only. **There is no PS2 entry.**
- A redistributable PS2 build must be `-DMAKEANDPLAY=ON`, with the Make and Play data, under the general exception. Alternatively, ask Terry Cavanagh for a PS2 exception.
- The repack script only **re-containers the user's own file**; nothing proprietary is committed. This matches how the DC and PSP ports also require the user to run a conversion step.
- The open GPL/LGPL question about `ps2_drivers` (from the feasibility audit) is unchanged.

## 11. Recommended P2b architecture

1. **Asset step (user, once):** `scripts/repack-data-zip.py data.zip data.zip.new`. It is lossless; the music blob is stored and every other entry is unchanged. The music blob was verified byte-identical after repacking.
   - **Runtime check:** time the first seek. If the blob behaves like a deflated entry (a seek into it takes more than ~50 ms), log a clear message and **keep music disabled** rather than stall the game for seconds.
2. **Index:**
   - Read the 7,680-byte header table of `vvvvvvmusic.vvv` through PhysFS and compute `{offset, size}` per track. No track data is read.
   - Loose `music/*.ogg` keeps working with offset 0.
   - `mmmmmm.vvv`, if present, uses the same index walk.
3. **Track object:** upstream `MusicTrack` gains a streaming mode (`VVV_MUSIC_STREAMING`, enabled for PS2, off elsewhere):
   - On `Play()`: `PHYSFS_openRead`, `PHYSFS_setBuffer` (16–32 KB), `PHYSFS_seek(offset)`, then `PHYSFSRWOPS_makeRWops`, then `stb_vorbis_open_file_section(rw, close_on_free, …, size)`. Music.cpp already maps stb_vorbis' `FILE` to `SDL_RWops`, so no new decoder or engine is needed.
   - Only one track is open; the previous decoder is closed after `Halt()` destroys the voice.
   - **Bounded memory:** ~213 KB decoder + decode buffers + read buffer, about **0.3–0.5 MB total, independent of blob size**.
4. **Scheduling:** decoding stays in FAudio's buffer callbacks on the SDL audio thread (priority 15–16, above the game's 64). It never runs on the render thread. On PS2:
   - Raise per-buffer duration from 50 ms to ~100–200 ms so I/O latency on USB cannot starve the voice.
   - Measure underruns via `FAudioSourceVoice_GetState` (`BuffersQueued == 0` before submit).
5. **Track-start cost (115 ms open) [M]:** start with the open in `Play()`, measured.
   - **If it shows as a visible hitch:** cache open decoders for recently played tracks (213 KB each; e.g. LRU of 4 = ~0.85 MB), so only the first start of a track pays. Or move the open to the audio thread during the fade-out silence.
6. **Instrumentation:** music memory, decode time per buffer, max decode time, underruns, track-open time. Logged by the existing `PS2Platform` reporter, together with frame times with music on and off.

## 12. Concrete next steps (P2b implementation)

| Step | Work | Exit criterion |
|---|---|---|
| 1 | `0005-music-streaming.patch`: index walk + streaming `MusicTrack` + counters; CMake `PS2_MUSIC=ON` with streaming, keeping `VVV_PS2_NO_MUSIC` as the off switch | Builds reproducibly; host build unaffected (streaming off by default) |
| 2 | `run-vvvvvv-pcsx2.sh` stages the repacked data.zip; runtime check refuses a deflated blob with a clear log line | Music plays in PCSX2; with the stock zip the game still runs, silently, and logs why |
| 3 | 15-minute PCSX2 session across areas (ship, Space Station, Lab, Warp Zone, Tower, Gravitron) | 0 underruns; music RAM bounded at < 0.6 MB steady; frame p95 regression ≤ ~2 ms vs P2a |
| 4 | Track switching, fades, whole-track loops, SFX overlap, pause/menu, save/load | All behave as on desktop; no leaks across ≥ 30 track changes |
| 5 | Measure the 115 ms open; add an LRU decoder cache only if needed | No frame > 50 ms caused by a music start |
| 6 | Report `docs/ps2-p2b-report.md` with on/off comparisons; hardware-only checklist (USB read latency vs buffer depth, real EE decode cost) | — |

**Fallbacks if hardware disproves the PCSX2 CPU numbers:**
1. Re-encode to lower-bitrate or 22 kHz Vorbis with the same streaming code (hybrid, lossy).
2. Then SPU2 ADPCM streaming, which needs a fixed or new IOP streamer.

## 13. Recommendation

**Use the hybrid:**
- runtime streaming of the **original Vorbis** with stb_vorbis inside the existing FAudio music path;
- from an **uncompressed blob entry**, produced by a lossless, scripted, one-time repack of the user's own `data.zip`.

**Do not stream from the stock deflated archive.** It is measured at 2.6–7.6 s per track start or loop.

**Do not adopt preconverted ADPCM now.** The measured ~9% EE decode cost does not justify 63–125 MB of extra storage, the quality risk, and a custom IOP streaming driver. Keep it as the documented fallback if real hardware disagrees.
