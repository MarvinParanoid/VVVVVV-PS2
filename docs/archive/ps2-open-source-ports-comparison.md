# Open-source PS2 ports: what VVVVVV-PS2 can learn

**Date:** 2026-10-23

**Status:** research only; no code was changed.

**Why:** everything in VVVVVV-PS2 has been measured in PCSX2 only. Two of the ports below run on real consoles. Their notes are the cheapest source of "PCSX2 hides this" knowledge before our first hardware session.

**Evidence labels:**
- **[V] verified** in source, ours or theirs, at the revisions below.
- **[D] documented** in that project's docs or rules files; not re-checked in code.
- **[HW]** the source says it was observed on a real console.
- **[A] assumption.**

## 1. Repositories examined

| Repo | Revision | What it is | Hardware status |
|---|---|---|---|
| [counter185/CSE2-ps2](https://github.com/counter185/CSE2-ps2) | `4acb4d2654` (2025-12-20) | Cave Story (CSE2) on SDL2 with the gsKit **hardware** renderer. About 600 changed lines, many of them hacks | Runs on a console (commit "fix fonts on real hardware") |
| [glampert/quake-ps2](https://github.com/glampert/quake-ps2) | `b6f6aa843e` (2026-10-22) | QuakeSpasm with its own C++20 PS2 backend (own DMA/VIF, no gsKit, no SDL). The backend comes from his Quake II port | **Playable on a console**. Facts measured with Quake II on hardware are marked *(Q2)* in `.claude/rules/*.md` |
| [glampert/doom3-bfg-ps2](https://github.com/glampert/doom3-bfg-ps2) | `24ddb8b093` (2026-10-22) | Doom 3 BFG. Work in progress: dev console and 2D UI only | Emulator only; hardware acceptance is deferred to milestone 2D-8 |
| [Arawn-Davies/pcsx2oom](https://github.com/Arawn-Davies/pcsx2oom) | `0105d27ccc` (2022-11-27) | Old ps2sdl DOOM on SDL 1.2 and SjPCM, refreshed to build on a newer SDK | Unknown |

The user's original list said "Quake II". The most useful repo is the Quake (1) port: it carries the Quake II backend forward and records the Quake II hardware findings. The real knowledge base is `quake-ps2/.claude/rules/{ps2-platform,audio,gs-renderer,performance,save-games,testing-pcsx2}.md`, not the README.

## 2. Findings by area

### 2.1 SDL2 and software rendering (CSE2-ps2)

CSE2-ps2 is the only other SDL2 port. It took the opposite route to ours: it uses SDL's gsKit renderer directly and removed or rewrote every render-target use (textbox, room name, value view, inventory backdrop). Problems they hit, mapped to us:

| CSE2 problem | Their fix | VVVVVV-PS2 |
|---|---|---|
| "SDL2 has incorrect texture colours, fixed in SDL3" [D] | Custom SDL build with the fix backported | Already fixed: patch 0001 scales vertex colour and alpha to the GS range (0x80 = 1.0) [V] |
| No render targets in the gsKit renderer, and not enough VRAM for them [D] | Rewrote the drawing code | We use SDL's software renderer, with the GS only scaling the final frame [V] |
| 640×480 too slow; game speed tied to frame rate [D] | 320×240 | Same choice; 320×240 window stretched by the GS [V] |
| Sprites off by one texel [D] | Sheets resized to multiples of 16 | Not affected: the only GS texture is the full-frame 320×240 framebuffer [V] |
| **Fonts garbled on real hardware [V, HW]** | Each glyph gets its own texture of at least 16×16, and the **whole texture is zero-filled before the glyph is uploaded** (commit `4acb4d2`) | See below |
| Audio crackles at every rate except 22,050 Hz [D] | 22,050 Hz, 512 samples | We output 44,100 Hz. See §2.3 |
| `SDL_LockAudioDevice` replaced by `std::mutex` [V] | Reason not given | Not applicable [A] |
| Progressive mode "makes the screen constantly scroll" [D] | Interlaced | We use interlaced 640×448 [V] |

**Uninitialised VRAM [V, HW].** The garbled fonts were fixed by uploading zeros over the parts of each texture that the glyph does not cover. In other words, texels that were never written contain garbage on a console, while PCSX2 shows them as zero. For us:
- the window framebuffer texture is fully re-uploaded every frame;
- it is sampled with `GS_FILTER_NEAREST`, because SDL takes the default `SDL_HINT_RENDER_SCALE_QUALITY` [V];
- so nothing reads outside the 320×240 region today.

The risk returns if the filter becomes linear: with `GS_CMODE_REPEAT`, a bilinear tap at the edge reads padding texels that were never written. **Rule: never let the GS sample a texel we did not write.**

### 2.2 GS and VRAM (Quake, Doom 3)

Neither glampert port uses gsKit. Points that carry over to us:

- **D-cache coherency [D, HW (Q2)].** `dma_channel_send_chain` writes back only the tag buffer, not data reached through REF tags. On a console, paletted textures vanished until `SyncDCache` was added before the CLUT upload. PCSX2 does not model the EE cache.
  - gsKit's upload path flushes itself [A].
  - Patch 0006 puts the window framebuffer directly in the texture's memory, and the CPU writes it every frame. **If a console shows stale or torn frames, check this first.**
- **GS cost is invisible in PCSX2 [D, HW (Q2)].**
  - GsWait was 5.4 ms on a console, against 0.05 ms in PCSX2.
  - EE work was 1.56× slower on hardware, because of the cache.
  - Our P3 margins (14.6–17.4 ms render in normal rooms, against a 34 ms tick) leave room for a 1.56× slowdown. The two worst scenes (24.0 ms) would come out at about 37 ms, just over the tick [A].
- **Bounded GS waits [V].** Both ports wrap every GS/DMA wait in a limit: about 1 s counted with the COP0 Count register, which works without timers or interrupts. A timeout dumps D1_CHCR, VIF1_STAT, GIF_STAT and CSR, then shows a fatal screen. gsKit's own waits are unbounded [A], so a GS hang gives us a black screen today.
- **Hang recovery [D, HW (Q2)].** A hardware hang once ended on a green screen. Before drawing a panic screen they now reset PATH1, PATH2 and PATH3.
- **VRAM allocator.** Quake uses a first-fit heap in 256-byte blocks with LRU eviction that never evicts a texture bound this frame. Doom 3 uses whole 8 KiB pages with generation-checked frees. This only matters to us if G1 (the GS renderer) is revived; see [ps2-gs-renderer-feasibility.md](ps2-gs-renderer-feasibility.md).
- **Large S/T coordinates lose precision on a real GS [D, HW].** This is relevant to G1 only.

### 2.3 Sound

**The audsrv stale-ring buzz [D, verified against the audsrv description in their rules].** audsrv's IOP thread takes one feed every tick whether or not the EE queued anything. When the queue runs dry it **replays the stale ring (about 106 ms) in a loop**, so a stall is heard as a buzz, not as silence. The Quake II port had this bug. Quake now uses:
- a feeder thread one priority above main;
- a `SetTimerAlarm` every 5 ms that wakes it through a semaphore;
- a queue kept 4 feeds deep, padded with silence when no mixed audio is ready.

Other audsrv facts [D]:
- It does not start playing until the first `audsrv_play_audio()`.
- Its EE library is single-threaded: one static buffer and one RPC client, so only one thread may call it.
- Its upsamplers are sample-and-hold, so low rates alias.

Where we stand [V]:
- SDL's PS2 audio driver forces 512 samples × 2 buffers. At our 44,100 Hz that is about 23 ms of EE-side buffering, and the SDL audio thread blocks in `audsrv_wait_audio`.
- The SDL audio thread runs at SDL priority 15/16, and `PS2_PostInit` drops the game thread to 64, so audio preempts the game.
- The music worker is above the game thread and yields every ~2 ms (P2b).
- **Gap:** our underrun counters measure the **music ring**, not audsrv. If the SDL audio thread misses a feed (FAudio mixing slower than real time, or blocked behind something), the console will buzz while every counter reads zero. PCSX2 would not show it.
- **Gap:** CSE2 reported crackle at every rate except 22,050 Hz. That is consistent with feed misses at 512-sample periods: 11.6 ms at 44.1 kHz, against 23.2 ms at 22.05 kHz [A].

**Music formats.** Quake streams **SPU2 ADPCM** pre-encoded on the host (22,050 Hz, about 1/7 of WAV size) and decodes it on the EE. That cost is 164 µs per frame, against roughly 10% of the EE for our `stb_vorbis` (P2b). The reader runs at main's priority, and main yields to it with `RotateThreadReadyQueue`. This is a fallback for us if hardware shows that Vorbis costs too much; it would mean a host-side transcode in `repack-data-zip.py`.

**Fade around stalls [D].** Before a memory-card access, Quake fades the music out over 500 ms and back in over 1 s, instead of letting it stutter.

**pcsx2oom [V]** paces its mixer from vblank, sizing each buffer to one field: 800 samples NTSC, 960 PAL at 48 kHz. The buffer size depends on the region.

### 2.4 USB and saving (Quake)

- **The IOP serves one file request at a time [D, HW (Q2)].** While one thread has a `read()` out, every other file call waits for it, about 300 ms on a console USB drive. PCSX2's `host:` hides this.
  - Our music worker reads through PhysicsFS on its own thread.
  - Any main-thread file call during gameplay (saves, `bench.txt` polling, the overlay?) can stall a frame behind it.
  - The log writer is on its own thread, but it also takes the IOP for every flush.
  - Expect hitches on save points, not underruns [A].
- **Writes reach the drive only on `close()` [D].**
  - FatFs updates the size in the directory entry only on sync or close.
  - `bdmfs_fatfs` sync returns EIO, and `fsync()` is ENOSYS.
  - Our log writer does open, truncate, write the whole buffer, then close on every flush [V], which is correct.
  - **Write cost [D, HW (Q2)]:** about 23 ms per open/append/close cycle on a console.
- **A successful `close()` on USB or HDD returns the iomanX slot number, not 0 [D].** `host:` returns 0, which hid this in PCSX2. We never test `close() == 0` [V], so we are safe.
- **IOP and boot [D].**
  - Their boot reuses the modules the launcher left behind, and resets the IOP only on a conflict or failure. Ours always resets (SDL's `reset_IOP()` + `init_drivers()`) [V].
  - **If an HDD is present, its drivers must load, or the console hangs at boot [HW].**
  - USB readiness: poll every 100 ms for up to 10 s, probing `mass0:` to `mass9:`.
  - fileXio's read buffer is raised from 16 to 64 KB. Together with 32 KB FAT clusters this cut a Quake II level load from 58.6 s to 40.4 s on a console. Their model: about 5 ms per command, about 900 KB/s.
  - newlib stdio buffers only 1 KB, so they make their file handles unbuffered.
- **Save safety [D].**
  - A/B slot files, each with a sequence number and a 96-byte header that carries CRCs.
  - The newest valid slot wins.
  - No rename, because FILEIO and mc have none.
  - The same scheme works on USB, HDD and memory card.
  - VVVVVV writes its save XML in place through PhysicsFS [A, needs check]. A power cut mid-write can leave a truncated save.
- **Memory card [D].** If we ever add it:
  - use the ROM modules SIO2MAN, PADMAN, MCMAN and MCSERV, because ps2sdk's own mcman will not link against the ROM SIO2MAN;
  - `mcInit` spins forever if MCSERV is missing;
  - at most 3 open handles; no rename; no `O_TRUNC`.
- **`rom0:FILEIO` bug [D]:** `remove()` also runs `mkdir()`. `sbv_patch_fileio()` fixes it.

### 2.5 PAL and NTSC

| Port | How the mode is chosen |
|---|---|
| Quake [D] | Auto from the ROM name: a 5th character of `'E'` means PAL. Overridden by a `ps2_video_mode` cvar (auto/ntsc/pal) read from the config before the GS starts; changes apply after a restart. 640×448 NTSC or 640×512 PAL, interlaced FIELD. Forcing NTSC on a PAL console works on hardware |
| Doom 3 [V] | `video::ConsoleStandard()` with the same ROM check, but boot uses a `host:presentation.cfg` file. The UI is a virtual 640×480 canvas scaled to 448 or 512 lines |
| pcsx2oom [V] | The same ROM check (`GetRomName`, byte 4 == `'E'`), overridden by an ELF name ending in `PAL` or `NTSC`. The audio buffer also follows the mode |
| CSE2 [V] | Hard-coded `SDL_HINT_PS2_GS_MODE "PAL"` (a custom SDL build; stock SDL 2.32 hard-codes NTSC) |
| **VVVVVV-PS2** [V] | Always NTSC 640×448i ([installing.md](../installing.md)) |

**Adoptable approach:**
1. Default to auto, using the ROMVER byte 4 check.
2. Allow an override file next to the ELF, read at boot before the GS starts. pcsx2oom's ELF-name suffix needs no file at all.
3. Change 640×448 to 640×512 in patch 0001 and let the GS stretch 320×240 into it. VVVVVV's logic runs at a fixed 34 ms step, not per field, so game speed does not change [A].

The cost is about 240 KB more VRAM per buffer [D] and a second golden-frame set.

### 2.6 Diagnostics

Our tooling compares well:
- Doom 3 has no crash handler and no on-screen fatal error yet [V].
- Quake's handler is in debug builds only [D].

What they have that we lack:

| Tool | Port | Value for our first hardware session |
|---|---|---|
| **Exception handler switches `$sp` to a private stack** | Quake [D] | **High.** Ours copies the frame and rewrites EPC to `exception_report` [V], the same design, **but keeps the faulting `$sp`**. A stack overflow or smashed `$sp` faults again inside the report, hits the `entered++` guard and parks, giving a hang with no screen. Fix: a static 16 KB stack and `frame->sp = top` |
| **Null-dereference is untested in PCSX2** | Quake/Doom 3 [D] | PCSX2 logs `TLB Miss` and **skips** a null load or store, with no exception. Our `teq` test proves the trap path only; the TLB/AdEL path (codes 2–5) is unproven until hardware |
| Bounded GS/DMA waits with a register dump | both [V] | **High.** Turns a black-screen hang into a named fatal error. We would need wrappers around our own sync points, or a watchdog thread that checks a frame heartbeat [A] |
| Reset GIF paths before the panic screen | Quake [D, HW] | Medium. Our boot console and fatal screen may draw nothing after a GS hang |
| Boot report of IOP modules (what the launcher left, what was reset and why, free IOP memory) | Quake [D] | Medium. Our boot console lists steps but not modules or IOP memory |
| GS VRAM readback to a file | Doom 3 [V], ours [V] | We already have `shot_NN_*.ppm` via `ps2_screenshot`. Doom 3 says ps2sdk's `ps2_screenshot_file` returns 0 on both success and failure and has unbounded waits [D]; our use of `ps2_screenshot` checks `!= 1` [V] |
| Per-file open/read/close timings during load (`io_log`) | Quake [D] | Low to medium; `fs_smoke` covers read speed |
| Frame log CSV with host analysis scripts | Quake [D] | We have the bench and overlay; comparable |
| PCSX2 runner: requires a RESULT marker, fails on `TLB Miss` and other crash patterns, watchdog | Doom 3 [V] | Medium for regression. PCSX2 can silently survive faults that would kill a console, so grepping its log for `TLB Miss` catches real bugs |
| Command file polled over `host:` | Doom 3 [V] | Low |
| Tagged heap ledger and OOM dump of every tag | Doom 3 [V] | Low; our memory is flat, with 6.4 MiB free |
| `make_usb_image.py`: FAT32 image for PCSX2 USB with `HostFs=false` | Quake [D] | **High and cheap.** It exercises the real `mass:` boot path (bdm, FatFs, close semantics) in the emulator instead of `host:` |

### 2.7 Smaller items

- **Pad [D, HW].**
  - Old DualShock 2 sticks rest more than 0.15 off centre, so Quake raised the dead zone to 0.25.
  - A pad port left open by the launcher keeps PADMAN DMAing into a stale buffer. Close the ports at boot.
  - Our IOP reset probably covers the second point [A].
- **`sysconf()` returns −1 [D, HW].** dlmalloc then has no page size and the console shows a black screen. They pin it to 4096. We use newlib's malloc, not dlmalloc [A].
- **FPU [D].** No Inf/NaN, rounds toward zero, `double` is soft-float. They build with `-fsingle-precision-constant -Wdouble-promotion`. P3 removed our known hot doubles; `-Wdouble-promotion` would find the rest.
- **PCSX2 limits [D].**
  - PCSX2 cannot start an ELF whose path is 152 characters or longer.
  - A `TLB Miss` in sifrpc `_request_end` under HostFs is a PCSX2 flake, not a game bug.
- **SDK trap [D].** `TimerUSec2BusClock` overflows u32 for durations of 30 s or more.

## 3. Recommendations

Ordered by value for the first hardware session versus cost. Nothing here changes game logic.

| # | Change | Why | Cost |
|---|---|---|---|
| 1 | Exception handler: set `$sp` to a private stack | Stack faults currently hang with no screen | Tiny (VVVVVV patch 0003) |
| 2 | Instrument audsrv in SDL `SDL_ps2audio.c`: record the minimum `audsrv_queued()` depth (present in our ps2sdk, `audsrv.h:284`) and count feed misses (time between `PlayDevice` calls greater than the buffer length). Show both in the overlay and bench | The stale-ring buzz is otherwise invisible to our counters | Small (new SDL patch) |
| 3 | PCSX2 USB-image boot test (`HostFs=false`), plus a `TLB Miss` grep in our PCSX2 runs | Exercises the real `mass:` path and catches faults PCSX2 skips | Small (scripts only) |
| 4 | Frame-heartbeat watchdog thread: no present for over 2 s → dump GS/DMA registers to the log and show the fatal screen after resetting the GIF paths | GS hang → diagnosable instead of black | Medium |
| 5 | Add hardware-plan checks: stale or torn frames (D-cache), buzz versus silence on stalls, frame hitches at save points, HDD-equipped consoles | Known hardware-only failure modes | Docs only |
| 6 | PAL mode: ROMVER auto-detect plus an override, 640×512 | PAL consoles and TVs; the user base outside the US and Japan | Medium (patch 0001, goldens) |
| 7 | Atomic save writes (A/B or write-then-swap) | Power cut mid-save | Medium; needs a check of the VVVVVV save path first |
| 8 | Fallback only: 22,050 Hz output and ADPCM music. Possibly bypass SDL audio and feed audsrv by queue depth, as SM64 does (§4.2) | Only if hardware shows crackle or Vorbis cost too high | Medium to large |

Per the current freeze ([ps2-h1-release-candidate.md](ps2-h1-release-candidate.md), RC2), items 1–4 would go into a separate diagnostics build, not into RC2. Item 5 can be added to [ps2-hardware-test-plan.md](ps2-hardware-test-plan.md) now.

## 4. Second batch (2026-10-23)

### 4.1 Repositories examined

| Repo | Revision | What it is | Value for us |
|---|---|---|---|
| `DanielLMcGuire/chrome-dino-cpp` | — | **Does not exist.** GitHub returns 404, the user has no public repositories, and a search for "chrome dino ps2" finds nothing. The link was probably invented by the source that suggested it | — |
| [fgsfdsfgs/sm64-port](https://github.com/fgsfdsfgs/sm64-port), branch `ps2` | `5b693af` (2026-01-22) | Super Mario 64 on gsKit with its own triangle emitters. No SDL. Memory card saves | **Audio model**, frame pacing |
| [ButterscotchRunner/Butterscotch](https://github.com/ButterscotchRunner/Butterscotch) | `46b572b` (2026-10-23) | GameMaker: Studio runner in C. Its PS2 target is a 2D sprite engine on gsKit with offline-converted paletted atlases | **Closest 2D analogue**; GS render targets on gsKit |
| [headshot2017/supertux-ps2](https://github.com/headshot2017/supertux-ps2) | `5c6ddb1` (2022-04-04) | SuperTux Milestone 1 on SDL 1.2 software surfaces | Low |
| [h4570/tyra](https://github.com/h4570/tyra) | `b4be27a` (2026-10-19) | 3D engine: own packets, VU1 | Low |
| [Wellinator/tyracraft](https://github.com/Wellinator/tyracraft) | `7641cbe` (2024-08-16) | Minecraft clone on Tyra | Low |
| [2AGamesOfficial/HOEPS2](https://github.com/2AGamesOfficial/HOEPS2) | `9bce84b` (2026-05-17) | Arena shooter on a patched Tyra | Low to medium |

None of them reports fps or test results from a real console. The only hardware-specific facts found:
- SM64's "glitch bars on console" fix (3e6be8e);
- Butterscotch's "rom0: has compatibility issues between fat/slim PS2" (CMakeLists.txt:798);
- TyraCraft's "disable FOG for color issue on real console" (CHANGELOG.md:1176).

### 4.2 Sound: two models that avoid the stale-ring buzz

- **SM64 [V]** (`src/pc/audio/audio_ps2.c`, `pc_main.c:83-99`).
  - There is no audio thread. Once per 30 Hz frame, the main loop produces about 1,088 frames and pushes them with `audsrv_play_audio`.
  - Rate control works on queue depth: it produces 528 instead of 544 samples per half-frame when `audsrv_queued()` is at least 1,100 frames, and drops the chunk above 6,000.
  - The result is 35–190 ms always queued in audsrv, instead of SDL's 512 × 2.
  - Output is 32 kHz. The author's audsrv fork (`fgsfdsfgs/ps2-audsrv`) only added 32 kHz upsamplers and `audsrv_queued()`/`audsrv_available()`. It was dropped in 2021 once stock audsrv gained both, so **stock audsrv is enough**.
- **Butterscotch [V].**
  - Everything is pre-converted offline to IMA ADPCM at 22,050 Hz. Up to 4 music streams use 4 KB reads.
  - Mixing is mono into an int32 accumulator.
  - Also no thread: it pushes 512-frame chunks while `audsrv_available()` allows. A long hitch can therefore underrun [A].
  - A comment says PCSX2 crashes when the audio system is destroyed, so they skip destroying it.
- **Tyra [V]** streams raw WAV at 22,050 Hz through `audsrv_on_fillbuf` and a semaphore. Sound effects are SPU2 ADPCM through `audsrv_ch_play_adpcm`. HOEPS2 adds a channel pool that steals the oldest voice.

What this means for us:
- Sample rates: CSE2, Butterscotch and Tyra all output at 22,050 Hz and SM64 at 32,000 Hz. **We are the only port at 44,100 Hz.** This does not prove 44.1 kHz fails, but nobody else's experience supports it [A].
- Our threaded model is more robust to main-loop hitches than SM64 or Butterscotch. The weak point is the shallow SDL buffer, not the threading. Recommendation #2 measures it. If hardware shows a buzz, the SM64-style depth target is the fix.

### 4.3 Graphics

**Butterscotch's GS render targets on gsKit [V]** are directly relevant to the deferred GS renderer (G1, [ps2-gs-renderer-feasibility.md](ps2-gs-renderer-feasibility.md)). G1 found that SDL's gsKit renderer ignores `SetRenderTarget`. Butterscotch shows that render-to-texture on gsKit is practical. How it works:
- A surface is a page-aligned CT16 region in a VRAM chunk pool, pinned against eviction.
- To switch: `gsKit_queue_exec`, set `ScreenBuffer[active]`, Width = TBW × 64 and PSM, then `gsKit_setactive`.
- No nesting. A surface that does not fit becomes a "phantom": FBMSK = all ones, so its draws are dropped.

The gsKit pitfalls they document:
- PRIM.ABE is tied to TEX0.TCC, so "blend off" is emulated with `ALPHA(0,2,2,2,0x80)`.
- The default AREF of 0x80 hides opaque pixels; reset it to 0.
- `gsKit_sync_flip` resets FBMSK, so re-apply it every frame.
- Reusing a VRAM chunk within a frame needs a TEXFLUSH (they get one by uploading with `GS_CLUT_NONE`).
- Clearing to alpha 0 needs ATE and FBA off.

Their other design points:
- art is paletted T4/T8, with the CLUTs uploaded once;
- VRAM is split into 32 KB chunks with LRU eviction by atlas;
- a second-level cache of decompressed atlases sits in EE RAM, uploaded by zero-copy DMA;
- an overlay shows VRAM and EE residency and a **thrashing indicator**.

None of this changes G1's verdict, which waits for hardware data. It is the reference design if G1 resumes.

**Frame pacing [V].**
- SM64 waits on two vblank semaphores per frame and flips only finished frames. Earlier, a vsync callback could display a half-drawn buffer: the "glitch bars on console" fix.
- Installing the vsync handler at init made the game run fast; it is now installed lazily.
- Our present path is SDL's `gsKit_sync_flip` with vsync [V]. If hardware shows bars or tearing, compare with SM64 3e6be8e.

**Tyra [V]:**
- it has no render targets, no LRU (it evicts everything when VRAM is full) and no NTSC/PAL choice;
- it sends one DMA per sprite;
- it is not a model for us.

### 4.4 Storage, IOP and saves

- **IRX source.** Butterscotch embeds every IRX instead of loading from `rom0:` because of fat/slim differences. Tyra does the same. **We already do this:** SDL's `init_drivers()` uses `ps2_drivers`, whose modules are linked-in `*_irx` blobs [V].
- **Drivers loaded.** SM64 (Jan 2026) loads only the boot device's drivers (`init_only_boot_ps2_filesystem_driver()`). Quake (§2.4) warns that skipping the HDD drivers on an HDD-equipped console hung its boot. The two approaches conflict, and Quake's warning is backed by hardware.
- **Memory card saves, if we add them.**
  - SM64, SuperTux and Butterscotch all write a directory with `icon.sys` (Shift-JIS title, colours and lights) plus an icon. Without them the browser shows "Corrupted Data" (Butterscotch comment).
  - SM64 tries `mcInit(MC_TYPE_XMC)` first and falls back to `MC_TYPE_MC`, then picks the card that already holds a save.
  - **None of the three writes atomically**; Quake's A/B scheme (§2.4) is the one to copy.
- **USB robustness [V].**
  - HOEPS2 retries failed asset opens and PNG decodes up to 3 times.
  - Tyra polls `stat("mass:/")` up to 50 times after the BDM modules load.
  - HOEPS2 replaced stdio with POSIX `read` in its WAV streamer to stop heap fragmentation. **Not applicable to us:** PhysicsFS uses `physfs_platform_posix.c` (open/read) [V], and our soak tests show flat memory.

### 4.5 Diagnostics and build

- **Butterscotch [V]:**
  - places the VM context in the 16 KB scratchpad (0x70000000);
  - **fails CI if the interpreter hot loop exceeds 16 KB** (I-cache size);
  - builds with `-fsingle-precision-constant -Werror=double-promotion`;
  - can write gprof output to `host:` or `mass:`.
  - The I-cache check is cheap to copy for our hottest blitters (patch 0004) if hardware shows the 1.56× cache penalty Quake measured [A].
- **Tyra and TyraCraft [V]** have a trap screen (`scr_printf`), a synchronous log file and a malloc-tracking wrapper. Nothing beyond what we have.
- **SuperTux [V]:** the only PS2 change of note is removing `SDL_Delay` calls. Data is found by probing `mass:/`, `cdfs:/data` and `hdd:/` in turn.

### 4.6 Changes to the recommendations

- #2 now uses `audsrv_queued()` (§3).
- #8 includes the SM64 feeding model as the fallback.
- New, lower priority:
  - an I-cache size check on the hot blitters;
  - `-Wdouble-promotion` as a warning in our PS2 builds;
  - for a future memory card save: `icon.sys`, XMC → MC fallback, A/B writes.

## 5. glampert's other repositories (2026-10-23)

Of [glampert](https://github.com/glampert)'s 44 public repositories, these concern the PS2:
- `quake-ps2` and `doom3-bfg-ps2`, covered in §2;
- the six below.

The rest (debug-draw, compression, game reverse-engineering, Rust and OpenGL demos) do not bear on the port.

| Repo | Revision | What it is | Value for us |
|---|---|---|---|
| [quake2-ps2](https://github.com/glampert/quake2-ps2) | `f5fb890` (2026-10-22) | Quake II on libdraw/packet2 plus VU1. **The port that actually ran on a console.** The "(Q2)" facts in quake-ps2's rules come from here | **High**: a dated record of hardware-only failures (§5.1) |
| [ps2-homebrew](https://github.com/glampert/ps2-homebrew) | `602441a` | 2015 demos (rotating cubes, a dungeon game) on his own C++ framework | Low |
| [vu-checker](https://github.com/glampert/vu-checker), [vclpp](https://github.com/glampert/vclpp), fork of [openvcl](https://github.com/glampert/openvcl) | 2026-10 | VU1 microprogram tooling: a checker for openvcl/dvp-as silent miscompiles, a C-like preprocessor for VCL, and openvcl fixes merged upstream (PRs #7, #8) | Only if G1 ever uses VU1; our G1 PoC drives the GIF directly |
| fork of [ps2docs](https://github.com/glampert/ps2docs) | 1 commit ahead | Sony's EE, GS, VU and SPU2 manuals converted to Markdown | A searchable reference. The texts are Sony's; do not copy them into this repository |
| forks of ps2gl, ps2stuff | 0 commits ahead | Unmodified | None |

### 5.1 quake2-ps2: the hardware session record

**Hardware [D]:**
- a **PAL fat PS2** with wLaunchELF 4.43a;
- an HDD attached (about 90 OPL partitions);
- a 15.8 GB FAT32 USB stick;
- a worn DualShock 2.

There are no slim-console results; our target is an SCPH-7500x slim.

The failures in the order they appeared [V: commits checked]:

| Date, commit | Symptom on the console | Cause and fix | Check for VVVVVV-PS2 |
|---|---|---|---|
| 10-06 `d688718` | Game data not found on USB | Fixed path `mass:/baseq2`. Fix: derive the folder from `argv[0]` (after the last colon), probe `mass0:`–`mass9:`, wait up to 10 s. **The error screen lists argv, the folders searched and the mounted volumes**, "because a console has no log to read" Already covered: the fatal screen shows the last errors, including "put data-ps2.zip … next to VVVVVV.elf in <path>" [V, patch 0003] |
| 10-06 `260396b` | A slow USB level load (over 78 s) looked like a hang | Log file, per-file load trace with a heartbeat, loading screen with progress. Timeouts later raised to 150 s | Our boot console shows steps but no progress inside the asset load. Time the first USB boot (§2.4: about 900 KB/s, about 5 ms per command) |
| 10-06 `0e010ec` | **Exception handler showed nothing on a real fault** | At exception level, printf and SIF RPC fail. Fix: copy registers, rewrite **EPC and `$sp`**, report from thread context on a private stack; retry without the stack walk if the walk faults | **Same as recommendation #1 (§3)**: our handler rewrites EPC but not `$sp`. Now confirmed by a hardware failure, not just a design note |
| 10-07/08 `0099cda` | First pipeline hang: the log held only the error line | The register dump went to stdout | Every fatal-path print must reach our log file; check the exception report and the GS paths |
| 10-09 `16d59d9` | Log stopped after its header | `close()` returns the slot number (§2.4) | We never test `close()==0` [V] |
| 10-09/10 `ff74dad` | GS hang | VU1 double-buffer overrun that PCSX2 never shows. Found by decoding the DMA chain trail and dumping VU1 memory | Not applicable (no VU1) |
| 10-10 `31e634f` | Only untextured fills drew | Missing `SyncDCache` before CLUT DMA (§2.2) | gsKit flushes before its texture DMA [V, H1.1 audit] |
| 10-10 `a1d110f` | Walls "swam" | GS precision loss on large S/T values | Not applicable: our UVs stay within one 320×240 texture [A] |
| 10-11 `d9f31d0`, `d0b53b4` | 56 fps on NTSC, 6.8% of frames dropped | A command-buffer overflow stalled the EE about 8.6 ms on hardware (about 2 ms in PCSX2). Fixed by continuing in the other half, then sending the world pass early. Result: 59.9 fps, GsWait 5.3 → 3.4 ms | Not applicable while the software renderer is used: the GS gets one stretched sprite per frame. It matters for G1, where a draw-heavy queue would stall like this |
| 10-11 `aba41f3` | Music stuttered during saves, config writes and loads | The audsrv stale-ring loop. Fix: play silence until the whole ring has been rewritten, `audsrv_stop_audio`, then `audsrv_set_format` again on resume | Our SDL audio thread keeps feeding during main-thread stalls. A stall *of the audio thread itself* is the risk; recommendation #2 measures it |
| 10-11 | Black screen at boot, never diagnosed | Seen with an HDD attached and the HDD probe skipped | Note for testers with an HDD |
| 10-12 `eb7e4f5` | Boot took 29 s (53.6 s with a reset and a second search) | IOP reset 175 ms; **the HDD probe and PFS mount took about 7.5 s**. Fix: reuse the launcher's modules; its usbd registers as "usbd". USB boot with the HDD attached: **80 ms** | SDL always resets the IOP and loads HDD drivers [V]. Time our boot on a console with an HDD; expect several seconds |

**Other hardware facts not in §2 [D]:**
- One USB keyboard `read()` per frame while music streamed caused **259 stalls of about 314 ms** in 171 s. This confirms §2.4 with numbers. Our main thread does no per-frame file I/O [A]; keep it that way, overlay and bench included.
- An IOP reset leaves the same window as a launcher's open pad port: PADMAN can DMA into EE memory until it is stopped. SDL resets the IOP first thing in `main()` [V], which keeps the window short.
- `bdmfs_fatfs` `write` returns 0 instead of an error when FatFs fails. Our log writer counts `write() == len` as success [V], so a 0 is counted as a failure.
- Load times on a console:
  - USB, 8 KB clusters: 58.6 s;
  - USB, 32 KB clusters and a 64 KB fileXio buffer: 40.4 s;
  - HDD: 15.8 s.
  - macOS formats 16 GB sticks with 8 KB clusters.
  - Logging about 1,040 lines during a load cost about 24 s of 76 s.
- Memory card:
  - the BIOS browser draws transparent icon texels black;
  - a save self-test failed two steps on console boots only (not investigated).
- A PAL console forced to NTSC on a PAL-only TV over composite may lose colour. RGB SCART and component are fine. This matches [installing.md](../installing.md).
- Dead zone: a ramp up from zero, not a hard cutoff. A pad poll with no fresh data returns length 0; Q2 drops the analog input for that frame.

### 5.2 Changes to the recommendations

1. **#1 is promoted to required before any further hardware build.** A real fault produced no report until `$sp` was switched too; this was observed on hardware.
2. **For the first hardware session** (docs only; add to [ps2-hardware-test-plan.md](ps2-hardware-test-plan.md)):
   - time boot to title screen, noting HDD or no HDD;
   - record the stick's cluster size;
   - note any audio buzz during saves.
3. **Fatal-path logging is already covered.** The exception report goes through `PS2_Log` and `PS2_LogFlushNow` [V, patch 0003], so Q2's "only the error line reached the log" failure should not recur. It can only be confirmed by a real fault, after #1.
