# Technical overview

This is a short tour of the engineering work in the port. Numbers come from the
phase reports in this folder, which hold the full data and methods.

> **Every number below was measured in PCSX2 v2.8.2** (software GS renderer,
> BIOS SCPH-75001 v2.20). PCSX2 does not model the EE caches or memory
> latency, so real hardware may well be slower. Nothing here has been
> measured on a physical PS2 yet.

## Starting point

**Constraints.** The PS2 has:
- a 294.912 MHz MIPS R5900 (the EE) with a single-precision FPU only, so `double` maths runs in software;
- 32 MiB of main RAM;
- a GS that draws textured sprites quickly but has no general-purpose render targets reachable from SDL.

**What existed.** SDL 2.32.10 has a PS2 port (gsKit renderer, audsrv audio, libpad input). VVVVVV's SDL2 branch (`2.4-updates`) builds against SDL2, PhysicsFS and FAudio. VVVVVV draws everything through render targets, which the gsKit renderer does not implement, so the game uses SDL's software renderer.

**Approach.** Everything is kept as patches on pinned upstream sources:
- 7 for SDL;
- 5 for VVVVVV.

All changes are guarded by `__PS2__` or CMake `if(PS2)`, and game logic is not changed.

## 1. SDL2 adaptation (P1)

[Report](ps2-p1-report.md)

**GS-assisted scaling.** The window is a 320×240 surface. The GS stretches it to the 640×448 NTSC frame in hardware, instead of the CPU scaling every pixel:
- final copy: 148.7 → 4.8 ms;
- present: 2.6 → 1.4 ms.

**32-bit texture framebuffer.** XBGR8888 is the GS's native order, so the upload is a straight copy rather than a 16-bit pack.

**Colour fix.** On the stock path the displayed frame was about 1.6× too bright, because the GS MODULATE blend uses 0x80 as 1.0.
- Vertex colours and texture alpha are now scaled to the GS range.
- VRAM readback matches the window surface in 286,720 of 286,720 pixels.

**gsKit queue guard.** gsKit has no bounds check outside debug builds.
- The patch checks space before every command and flushes early.
- Queue memory went from 4.47 to 0.16 MB.
- 8,960 draws per frame run on a 64 KB queue with no drops.

## 2. Software rendering optimisations (P1c, P3)

[P1c report](ps2-p1c-render-profile.md), [P3 report](ps2-p3-report.md)

**Profiling setup.**
- A 1 kHz EE timer-alarm sampling profiler (`scripts/ps2-profile.py` symbolises the output).
- A deterministic in-game scene benchmark with ten scenes, including the worst cases (Tower, Panic Room).

**Correctness check.** "Golden frame" CRCs: every optimisation must leave the rendered pixels bit-identical, and they were checked against the unoptimised build.

**The pixel-format bottleneck.**
- VVVVVV loads its PNGs as **ABGR8888** textures but renders into **ARGB8888** targets, and the window is XBGR8888.
- Every tile, sprite and glyph therefore went through a channel-swapping blitter. SDL's generic generated loops took 20–39% of all CPU samples.
- On PS2, the render targets now use the textures' own layout (ABGR8888).
- New same-format 8888 fast paths cover modulated, blended, scaled and opaque copies. SDL's generated blitter table has no ABGR8888 destination at all, so without them these blits would have used the slowest path.

**Soft-float divisions.** SDL 2.32 does four `double` divisions per draw call (an angle check and the viewport size). The EE has no double FPU, so this took up to 13% in tile-heavy frames. Now they are a bitwise `angle == 0` test and a cached viewport.

**Redundant work.**
- Dead full-screen clears are dropped.
- The window framebuffer lives directly in the GS texture's memory, which removes a 300 KB copy per frame.
- Opaque scroll copies skip blending.

**Rendering rate.** One render per logic tick by default (`over30mode` off on PS2). The 30+ FPS option stays available in the menu.

**Result** (P2b configuration → P3):

| | Before | After |
|---|---|---|
| Render time, normal rooms | 33–40 ms | 14.6–17.4 ms |
| Render time, worst scene | 55.5 ms | 24.0 ms |
| Scenes at full game speed (≥ 29.0 of 29.4 ticks/s) | 2 of 10 | 8 of 10 |
| Worst-case game speed (Tower, Panic Room) | 11.6 ticks/s | 25.1 ticks/s |
| Pixel output | — | bit-identical in all 10 scenes |

**Why ticks per second.** VVVVVV's logic runs on a fixed 34 ms step (29.4 ticks/s), and a late frame drops the backlog instead of catching up. A slow frame therefore slows the *game* down, so the reports use logic ticks per second as the headline metric, not FPS.

## 3. Vorbis music streaming (P2b)

[Report](ps2-p2b-report.md)

**The problem.** Upstream loads the whole 61.7 MB `vvvvvvmusic.vvv` into RAM, which cannot fit in 32 MiB.

**Repacked archive.** `scripts/repack-data-zip.py` builds a `data-ps2.zip` from your `data.zip` with the music blob stored uncompressed, so it can be seeked. The blob stays byte-identical.

**Streaming worker.** One worker thread streams the current track:
- PhysicsFS seek;
- `stb_vorbis` decoding;
- a lock-free 1-second ring.

The audio callback only copies out of the ring.

**Memory.** Only one decoder (about 213 KB) and bounded buffers are live at a time. Peak memory with music is 17.16 MiB, against 16.42 MiB without.

**Scheduling.** The main finding of this phase. The EE kernel scheduler is strictly priority-based.
- A low-priority worker starved completely during gameplay: the first track took 12.8 s to open.
- The worker now runs above the game thread but yields after about 2 ms of work.
- Yield points inside `stb_vorbis` come from its I/O and trig callbacks, without modifying the library.
- The longest uninterrupted worker slice is 3–5 ms.

**Results.** Zero underruns in every run since the scheduling fix, including 18-minute gameplay and title-screen soaks. Music costs about 10% of the EE.

## 4. Memory

| Point | malloc in use |
|---|---|
| After `SDL_Init` | 0.05 MiB |
| First frame (all graphics, fonts, sounds, render targets) | ~17.1 MiB |
| Peak in gameplay with music (P3) | 16.87 MiB, ≥ 6.4 MiB free |

The 32 MiB budget also covers the ~7.7 MB ELF and the kernel. Memory stays flat across room changes and long soaks. Only English text and Latin fonts are packaged: the CJK font sheets would exceed the GS texture size limit and cost about 33 MB at 32 bpp.

## 5. Diagnostic infrastructure (H1)

[Readiness report](ps2-h1-hardware-readiness.md), [release-candidate report](ps2-h1-release-candidate.md)

The diagnostics are built for a first test on a console with no debugger attached:

| Tool | What it does |
|---|---|
| **Boot console** | A text screen lists each start-up step (IOP reset, USB drivers, boot device, assets, SDL, renderer) before any graphics exist. A hang shows the last step reached |
| **Fatal-error screen** | Any fatal exit shows the last stage, the last error and a hint, e.g. "is the USB stick write-protected?" |
| **CPU exception screen** | An EE exception handler saves the register frame and redirects to a report function. That function draws the registers and the build ID, and logs them. `scripts/ps2-profile.py --addr` turns the addresses into function names using the unstripped ELF |
| **Overlay** (L3+R3) | Ticks/s, frame and render time, music state, underruns and free memory, updated once a second |
| **Hardware benchmark** | Put `bench.txt` next to the ELF to play six fixed scenes hands-off. It writes `bench_results.txt` and golden-frame CRCs, so console numbers compare directly with PCSX2 |
| **`fs_smoke.elf`** | A standalone USB storage self-test: device readiness, read speed, random-seek latency, CRC32s of the packaged files, and directory, write and delete checks |
| **Log** | A background writer thread flushes every 10 s on real storage, so logging never blocks the render or audio threads |

**Reproducible releases.**
- Builds are bit-for-bit reproducible from a clean clone: the toolchain image is pinned by digest, and the SDL and VVVVVV sources by hash and commit.
- Release candidates are frozen read-only, with both stripped and unstripped ELFs and SHA-256 sums.
