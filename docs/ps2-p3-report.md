# P3 Report: Rendering optimisation and real-hardware readiness

**Date:** 2026-10-22

**Base:** VVVVVV `2.4-updates` @ `79395fa`, SDL 2.32.10, PS2DEV image as before.

**Related:**
- [ps2-p1c-render-profile.md](ps2-p1c-render-profile.md)
- [ps2-p2a-report.md](ps2-p2a-report.md)
- [ps2-p2b-report.md](ps2-p2b-report.md)
- [ps2-hardware-first-boot.md](ps2-hardware-first-boot.md) (checklist and diagnostics)

**Evidence labels:**
- **[M]** Measured in **PCSX2 v2.8.2** (software GS renderer, BIOS SCPH-75001 v2.20 NTSC). PCSX2 has no EE cache model, so absolute times are probably optimistic.
- **[C]** Read from code.
- **[E]** Estimate.

**No part of this report has been validated on a physical PS2.**

## Summary

| | P2b configuration [M] | P3 [M] |
|---|---|---|
| Game speed: scenes at ≥ 29.0 ticks/s (full rate is 29.4) | 2 of 10 | **8 of 10** |
| Worst game speed (Tower / Panic Room, player falling behind the camera) | 11.6 ticks/s (39%) | **25.1 ticks/s (85%)** |
| Tower when climbing normally | 16.0 ticks/s | **29.0 ticks/s** |
| Render time, normal rooms | 33–40 ms | **14.6–17.4 ms** |
| Render time, worst scene | 55.5 ms | **24.0 ms** |
| Loop period p99, normal rooms | 39–53 ms | **36.2–36.7 ms** |
| Gameplay run from P2b (new game + walk, music on): render mean / max period | 20.97 ms / 102 ms | **10.15 ms / 41.9 ms** |
| Audio underruns (all runs) | 0 | **0** |
| Peak `malloc_in_use`, gameplay | 17.16 MiB | **16.87 MiB** |
| Pixel output | — | **bit-identical** in all 10 scenes (golden frames, window included) and in `sw_exact` on EE and host |

**Remaining bottlenecks:**
- per-pixel alpha blits;
- whole-frame copies;
- per-call overhead in tile-heavy frames (1,300–1,550 draws);
- Vorbis decoding, about 10% of the EE.

**GS-accelerated renderer:** not justified yet (§10).

**First physical test:** ready, with the risks listed in §8 and §10. A packaging script, a storage self-test ELF and a checklist are provided.

## 1. Methodology and baseline

### 1.1 Intended update rate

VVVVVV's game logic uses a fixed 34 ms timestep, 29.41 ticks/s [C: `Game::get_timestep`, 34 ms outside slowdown]. Rendering happens once per main-loop iteration.

**`over30mode`** (on by default upstream) renders extra interpolated frames between ticks.

**Backlog handling.** `deltaloop()` does `accumulator = fmod(accumulator, timestep)` after each tick [C: `main.cpp`]. A late frame therefore **does not catch up**: it drops the backlog, and **the game itself slows down**.

The headline metric is therefore **logic ticks per second**, with 29.4 meaning full speed. The FPS target follows from it: one rendered frame per tick, every frame within 34 ms. Loop period, render time and their P50/P95/P99/max are reported alongside.

### 1.2 Repeatable scenes: `src/PS2Bench.cpp`

The benchmark is enabled by a `bench.txt` next to the ELF, written by `BENCH="…" scripts/run-vvvvvv-pcsx2.sh`. It is PS2-only code.

**Driving the game.** The benchmark uses the game's own entry points, with no input:
- `script.startgamemode()`: new game, time trials, Secret Lab;
- `map.gotoroom()`;
- `game.mapmenuchange(MAPMODE)`;
- `game.setstate(9)`, which is what the Super Gravitron trigger does.

**Scenes:**

| Scene | How it is reached | Why it matters |
|---|---|---|
| title | boot | menu, scrolling tower background |
| intro_dialogue | new game; the intro text box waits for input | dialogue-heavy (~270 draws, text with outlines) |
| room_ss1 | Space Station 1 time trial | typical room + time-trial HUD |
| map | map menu from room_ss1 | ~650 draws |
| room_lab | Laboratory time trial | coloured rooms |
| warpzone | Warp Zone time trial | scrolling warp background redrawn in logic |
| tower | Tower trial, then the tower (109,109), invincible, idle | **worst case**: an idle player falls behind the auto-scrolling camera, so the game redraws the whole tower background *every tick* (`Logic.cpp` "buffer isn't big enough"). Also happens while falling or respawning |
| tower_climb | same, player kept at camera height | **typical** tower play: only new background rows are drawn (55 full redraws in 579 ticks) |
| panic_room | Final Level trial, then the Panic Room minitower (49,52), invincible | worst case, as for the tower |
| super_gravitron | Secret Lab, then (119,108) + state 9 | ~300 draws, large fills, 2× scaled text |

**Passes:**
- **Golden frames.** For each scene: `srand(1)`, set up, then exactly 90 logic ticks with logic frozen. The `gameTexture` and window contents are read back with CRC32 and a PPM.
  - Rendering uses its own fixed `rand()` sequence and restores the logic's RNG state, so the golden frames depend only on tick counts.
  - Verified: two runs of one build give identical CRCs.
- **Measurement.** Per scene: setup, 4 s settle (+5 s for time-trial countdowns), 20 s measured. Log file writes are deferred during measurement, and periodic GS snapshots are off.

**Per-frame records** (`bench_NN_scene.csv`):
- loop period;
- logic time and ticks;
- render time, split into:
  - VVVVVV draw code + SDL command queuing,
  - SDL software rasterisation (`SW_RunCommandQueue`),
  - GS upload (`UpdateTexture` + GIF build),
  - GS DMA/finish,
  - vsync wait;
- draw-command counts;
- music decode time.

The per-scene summary has mean, P50, P95, P99 and max, plus idle time per loop, ticks/s, and underruns in the scene.

**Profiler.** An EE timer alarm at 1,009 Hz records the interrupted PC for all threads; `scripts/ps2-profile.py` symbolises the samples. PCs outside `.text` count as "kernel/idle", i.e. waiting.

**Copy kinds.** With `kinds=1`, SDL counts copies by source/destination format, blend and modulation.

**PCSX2 speed.** Emulated time vs wall time per run: 338.4 s emulated in 347 s wall, including PCSX2 start-up. That is ~100%, so emulator slowdown does not distort the numbers [M].

**Configurations.** All runs use `settle=4 measure=20`.

| Run | ELF | Options | Purpose |
|---|---|---|---|
| A | VVVVVV.elf | `opts=0 over30=1` | **P2b configuration** (all P3 changes off, upstream default over30) |
| B | VVVVVV.elf | defaults (all P3 on, over30 off) | **P3, primary, music on** |
| C | VVVVVV.elf | all P3 on, over30 on | over30 option |
| D | VVVVVV-nomusic.elf | as B | **music-disabled control** |
| E | VVVVVV.elf | `opts=0 over30=0` | golden-frame reference for B |

The P3 changes are runtime switches (`ps2_opts`; SDL hint `SDL_PS2_P3_FASTPATHS`), so A/E and B/C use **the same ELF**.

### 1.3 Baseline (run A, P2b configuration, music on) [M]

| Scene | ticks/s | Period mean / p95 / p99 / max (ms) | Render mean / p95 / p99 / max (ms) | Logic per tick (ms) |
|---|---|---|---|---|
| title | 29.4 | 20.3 / 26.5 / 28.8 / 29.1 | 15.2 / 19.6 / 21.8 / 22.2 | 8.4 |
| intro_dialogue | 29.4 | 27.4 / 27.8 / 27.8 / 28.2 | 27.1 / 27.4 / 27.4 / 27.4 | 0.4 |
| room_ss1 | 28.9 | 33.8 / 37.6 / 39.3 / 40.8 | 33.4 / 37.3 / 39.0 / 40.5 | 0.3 |
| map | 24.7 | 40.6 / 43.7 / 46.0 / 46.7 | 40.3 / 43.5 / 45.8 / 46.5 | 0.2 |
| room_lab | 27.1 | 36.1 / 40.0 / 40.2 / 41.2 | 35.8 / 39.7 / 40.0 / 40.9 | 0.3 |
| warpzone | 21.8 | 45.9 / 50.9 / 53.4 / 54.5 | 35.0 / 38.9 / 40.5 / 43.5 | 10.9 |
| tower | 11.6 | 86.2 / 92.2 / 94.7 / 96.3 | 55.0 / 61.6 / 63.7 / 66.1 | 31.1 |
| tower_climb | 16.0 | 62.7 / 83.0 / 85.2 / 88.7 | 51.3 / 57.9 / 60.0 / 60.4 | 11.3 |
| panic_room | 11.7 | 85.8 / 92.6 / 95.4 / 101.4 | 55.5 / 62.8 / 63.7 / 65.5 | 30.3 |
| super_gravitron | 22.2 | 45.1 / 55.5 / 63.3 / 65.2 | 44.5 / 55.1 / 58.2 / 64.8 | 0.5 |

Music decoding took 104–124 ms of wall time per second (~10–12% of the EE) in every scene with music.

**What the P2b report's "20.97 ms render" covered.** It measured the new-game area with the virtual pad. These scenes are heavier: time-trial HUD, map, warp backgrounds and towers.

## 2. Bottlenecks found

All measured on the baseline. Profile shares are of all EE samples.

**1. Channel-order conversion on almost every blit.** This is the main cost.
- VVVVVV loads its PNGs as **ABGR8888** textures. Its render targets are **ARGB8888**. The window framebuffer is **XBGR8888**, the GS's native order, chosen in P1 so the upload needs no swizzle.
- So every tile, sprite and glyph goes through a converting blitter:
  - `SDL_Blit_ABGR8888_ARGB8888_Modulate_Blend`, SDL's generic generated loop: 20–39% of samples;
  - `BlitRGBtoBGRPixelAlpha`: up to 19%;
  - `Blit_3or4_to_3or4__inversed_rgb`: 4–20%.
- The P1c modulate-blend fast path (patch 0002) never applied, because it requires ARGB8888 on both sides.
- Copy-kind counts per frame, room_ss1: 265 modulated glyph/sprite blits ABGR→ARGB, 26 plain blended blits, 1 full-frame NONE copy ARGB→ARGB, and 1 full-frame ARGB→XBGR copy to the window.

**2. Software floating point on every draw call.** `__divdf3` took up to 13% in the towers.
- In SDL 2.32 `SDL_RenderCopyExF` tests `(int)(angle/360) == angle/360`, and `RenderGetViewportSize` divides the double viewport by the scale.
- That is four soft-double divisions per tile on the EE, which has no double FPU. VVVVVV draws every tile with `SDL_RenderCopyEx(angle 0)`.

**3. Redundant full-screen fills.** `SDL_FillRect4` took 6–30%. The window is cleared three times per frame (`Screen::RenderPresent`, the start of `deltaloop`, `draw_window_background`), each about 1 ms, and is then completely overwritten by the final copy.

**4. The extra framebuffer copy.**
- `SDL_UpdateWindowSurface` copies the 300 KB window surface into the GS texture's memory: 1.2–1.4 ms per frame, the "GS upload" column.
- The real DMA ("GS DMA/finish") was only ~0.15 ms.

**5. Tower background work in logic.**
- When the camera has to chase the player, 1,300 tiles are redrawn in `renderfixed`: 30 ms/tick.
- Otherwise `scroll_texture` makes two full blended copies of a 336×336 texture per tick: `BlitRGBtoRGBPixelAlpha` + memcpy, 22% in tower_climb.

**6. Per-call overhead** in 1,300–1,550-draw frames: `SDL_RenderCopyF`, `UpperBlit`, `IntersectRect`, `PrepQueueCmdDraw` and similar, together about 15%.

**7. Music decoding** (~10% CPU) and **memcpy** of whole frames (8–14%) remain after P3 (§10).

**Not significant:**
- GS DMA (0.13–0.16 ms per frame);
- vsync wait (0: frames were longer than a vsync);
- allocation;
- the game's own logic outside the tower and warp backgrounds (0.2–0.6 ms per tick).

## 3. Optimisations implemented

All are bit-exact by construction and verified (§6). Each is a runtime switch, all on by default.

| # | Change | Where | What it does |
|---|---|---|---|
| 1 | **ABGR8888 render targets** | VVVVVV patch 0005 (`VVV_TARGET_PIXELFORMAT`, PS2 only) | Targets use the textures' layout, so tile, sprite and glyph blits no longer convert channel order. The window (XBGR) then has the same byte order too |
| 2 | **Same-format 8888 fast paths** | SDL patch 0004 | (a) Modulate (colour and/or alpha) with or without blend for ABGR/ARGB: the arithmetic of SDL's generated `*_Modulate[_Blend]` with exact alpha-0/255 shortcuts. SDL's generated table has **no ABGR8888 destination at all**, so without this these blits would hit `SDL_Blit_Slow`. (b) A scaled (NEAREST) variant: same 16.16 sampling as the generated `*_Scale`. (c) ABGR→XBGR / ARGB→XRGB unmodulated copies as row `memcpy`. (d) ABGR8888 blended fill rects on the fixed-layout path |
| 3 | **No soft-float divisions per draw** | SDL patch 0005 | Bitwise `angle == 0` test before the `angle/360` check; the viewport size is cached while its inputs are bit-identical |
| 4 | **Dead-clear elimination** | SDL patch 0003 | A clear is dropped when the next pixel command in the batch is another clear or an unscaled, unblended copy that covers the whole surface after viewport/clip, emulating `SetDrawState` exactly |
| 5 | **Skip redundant window clears** | VVVVVV patch 0005 / main.cpp hook (`PS2_OPT_WINDOW_CLEARS`) | The two window clears that `render()`'s full-window copy always overwrites, only when the stretch rectangle is the whole window |
| 6 | **Window framebuffer in GS texture memory** | SDL patch 0006 | The window surface *is* the streaming texture's buffer, so `UpdateWindowTexture` no longer copies it. Present waits for the GS before returning, so the next frame can be drawn in place |
| 7 | **Opaque scroll copies** | VVVVVV patch 0005 (`PS2_OPT_OPAQUE_SCROLL`) | `scroll_texture` copies without blending. The scrolled backgrounds are built from `clear()` + blended draws, so they are opaque and a blended copy writes exactly the source |
| 8 | **30 Hz rendering by default on PS2** | VVVVVV patch 0005 (`Game.cpp`) | `over30mode` defaults to off on PS2: one render per logic tick. The "30+ FPS" option is still available (§5.3) |

**Problems found and fixed on the way:**
- **Profiler setup.** `SetTimerHandler` on an allocated counter only fired alongside other alarms (3 samples in 20 s). It was replaced by a self-re-arming `SetTimerAlarm`.
- **First ABGR version.** It sent 2× scaled modulated text to `SDL_Blit_Slow`: 37% in the Gravitron, found in the profile. Fixed by 2(b).
- **Benchmark tower scenes.** The first version measured only the worst case; `tower_climb` was added for typical play.
- **Diagnostics overhead.** The always-on copy-kind counting cost ~2% in tile-heavy frames and is now opt-in.

No optimisation was reverted for failing to help. Every change in the table improved its target scenes in A/B runs and kept golden frames identical.

**Per-step history:**

| Step | Changes | Golden frames | Effect (render ms, room_ss1 / tower / gravitron) |
|---|---|---|---|
| ab | 1–3 | 9/9 identical | 33.5→20.5 / 55.6→31.1 / — (scene was broken) |
| ab2 | + 4, 5 | 9/9 (window CRC added) | 33.4→17.0 / 55.8→27.6 / 45.3→36.0 (Slow-blit regression visible) |
| ab3 | + 2(b–d), 6 | 10/10 | 33.3→14.6 / 55.3→23.7 / 45.4→20.4 |
| final | + 7, 8 | 10/10 | see §4 |

## 4. Before/after (A = P2b configuration, B = P3) [M, music on]

| scene | ticks/s | period mean | period p95 | period p99 | period max | render mean | render p95 | render p99 / max | logic/tick | underruns |
|---|---|---|---|---|---|---|---|---|---|---|
| title | 29.4 → **29.3** | 20.3 → **34.1** | 26.5 → **35.3** | 28.8 → **36.5** | 29.1 → **37.4** | 15.2 → **4.0** | 19.6 → **6.7** | 21.8/22.2 → **9.5/11.5** | 8.4 → **6.8** | 0 → 0 |
| intro_dialogue | 29.4 → **29.4** | 27.4 → **34.0** | 27.8 → **34.3** | 27.8 → **34.9** | 28.2 → **35.0** | 27.1 → **10.7** | 27.4 → **10.9** | 27.4/27.4 → **10.9/10.9** | 0.4 → **0.4** | 0 → 0 |
| room_ss1 | 28.9 → **29.2** | 33.8 → **34.2** | 37.6 → **35.6** | 39.3 → **36.7** | 40.8 → **37.8** | 33.4 → **14.7** | 37.3 → **20.0** | 39.0/40.5 → **20.8/24.1** | 0.3 → **0.3** | 0 → 0 |
| map | 24.7 → **29.3** | 40.6 → **34.2** | 43.7 → **35.3** | 46.0 → **36.3** | 46.7 → **36.7** | 40.3 → **17.4** | 43.5 → **22.1** | 45.8/46.5 → **23.9/26.0** | 0.2 → **0.2** | 0 → 0 |
| room_lab | 27.1 → **29.3** | 36.1 → **34.1** | 40.0 → **35.1** | 40.2 → **36.7** | 41.2 → **37.5** | 35.8 → **15.1** | 39.7 → **19.1** | 40.0/40.9 → **21.6/21.9** | 0.3 → **0.3** | 0 → 0 |
| warpzone | 21.8 → **29.3** | 45.9 → **34.2** | 50.9 → **35.3** | 53.4 → **36.2** | 54.5 → **37.1** | 35.0 → **14.6** | 38.9 → **19.0** | 40.5/43.5 → **20.9/23.4** | 10.9 → **8.2** | 0 → 0 |
| tower | 11.6 → **25.1** | 86.2 → **39.8** | 92.2 → **43.0** | 94.7 → **45.7** | 96.3 → **48.6** | 55.0 → **23.7** | 61.6 → **28.2** | 63.7/66.1 → **29.4/31.7** | 31.1 → **16.1** | 0 → 0 |
| tower_climb | 16.0 → **29.0** | 62.7 → **34.5** | 83.0 → **38.1** | 85.2 → **40.3** | 88.7 → **44.5** | 51.3 → **21.4** | 57.9 → **26.1** | 60.0/60.4 → **27.9/30.7** | 11.3 → **8.3** | 0 → 0 |
| panic_room | 11.7 → **25.2** | 85.8 → **39.7** | 92.6 → **43.5** | 95.4 → **45.8** | 101.4 → **48.6** | 55.5 → **24.0** | 62.8 → **29.1** | 63.7/65.5 → **30.1/31.9** | 30.3 → **15.7** | 0 → 0 |
| super_gravitron | 22.2 → **29.2** | 45.1 → **34.2** | 55.5 → **35.5** | 63.3 → **36.9** | 65.2 → **37.4** | 44.5 → **20.5** | 55.1 → **26.0** | 58.2/64.8 → **28.1/30.9** | 0.5 → **0.6** | 0 → 0 |

All times are in ms.

**Why the period goes up in title and intro.** In A these two scenes rendered interpolated frames: `over30mode` was on and they were light enough. In B the loop period is the logic timestep. The game speed (ticks/s) is the same.

**Where a P3 frame's time goes** (run B, ms per rendered frame):

| scene | VVVVVV draw + SDL queue | SDL rasterisation | GS upload | GS DMA | logic in loop | idle per loop |
|---|---|---|---|---|---|---|
| title | 0.25 | 3.57 | 0.02 | 0.14 | 6.85 | 23.3 |
| intro_dialogue | 1.09 | 9.45 | 0.01 | 0.14 | 0.39 | 22.9 |
| room_ss1 | 1.93 | 12.60 | 0.01 | 0.14 | 0.32 | 19.2 |
| map | 2.89 | 14.40 | 0.02 | 0.14 | 0.23 | 16.5 |
| room_lab | 2.11 | 12.84 | 0.02 | 0.14 | 0.32 | 18.7 |
| warpzone | 1.99 | 12.44 | 0.01 | 0.15 | 8.19 | 11.4 |
| tower | 6.86 | 16.66 | 0.02 | 0.15 | 16.10 | 0.0 |
| tower_climb | 5.99 | 15.19 | 0.02 | 0.15 | 8.31 | 4.9 |
| panic_room | 6.96 | 16.90 | 0.01 | 0.14 | 15.67 | 0.0 |
| super_gravitron | 1.71 | 18.64 | 0.01 | 0.14 | 0.58 | 13.1 |

Baseline (A) for comparison:
- SDL rasterisation 23.5–40.3 ms;
- draw + queue 2.2–18.6 ms;
- GS upload 1.2–1.4 ms.

## 5. Frame-time distributions and worst cases [M]

### 5.1 Distributions

Per-frame CSVs are in `logs/pcsx2/p3-final-*/bench_NN_<scene>.csv`. In run B:
- every scene except the three tower scenes has a **loop period p99 of 34.9–36.9 ms** and a max ≤ 37.8 ms;
- tower_climb has a p99 of 40.3 ms and a max of 44.5 ms;
- **render p99 ≤ 28.1 ms** outside the two worst-case towers (≤ 30.1 ms with them).

### 5.2 Worst-performing scenes

**tower / panic_room** (player below the camera, full background redraw every tick):
- before: 11.6 ticks/s, the game at **39%** speed;
- after: 25.1 ticks/s, **85%** speed;
- period p99 45.7 ms;
- logic 16 ms (1,300 tile draws into the background) + render 24 ms.

This state also occurs briefly during deaths and respawns in towers.

**tower_climb**, typical play: 29.0 ticks/s, period p95 38.1 ms.

**super_gravitron:** 22.2 → 29.2 ticks/s, period p95 55.5 → 35.5 ms.

### 5.3 `over30mode` (run C vs B, all P3 changes on)

With interpolation, rooms present every ~16 ms (period mean 16.0–17.6 ms, p99 21–24 ms):

| scene | ticks/s (C) | period mean / p95 / p99 / max (C) |
|---|---|---|
| room_ss1 | 29.4 | 16.0 / 19.6 / 21.0 / 23.9 |
| map | 29.4 | 17.6 / 22.7 / 23.8 / 26.2 |
| warpzone | 29.4 | 19.6 / 26.4 / 28.1 / 31.1 |
| super_gravitron | 29.4 | 20.7 / 26.6 / 28.9 / 36.8 |
| tower (worst) | 25.1 | 39.8 / 43.0 / 45.3 / 47.7 |

So "30+ FPS" is now viable in PCSX2. It is still **off by default** on PS2:
- it uses all spare CPU, leaving no headroom for the unknown hardware/PCSX2 speed factor;
- frame-to-frame periods alternate between 1 and 2 vsyncs, so pacing is uneven;
- at 30 Hz the game has 11–23 ms idle per loop in normal scenes.

**Revisit after hardware measurements.**

## 6. Rendering correctness

| Test | Result |
|---|---|
| **Golden frames**, game level, cross-build-comparable: 10 scenes, deterministic state after 90 ticks; CRC of `gameTexture` **and** of the window surface (what is presented) | **B vs E (over30 off): 10/10 identical. C vs A (over30 on): 10/10 identical.** Every intermediate step was also 9/9 or 10/10. Two runs of one build give identical CRCs (determinism check) |
| **`sw_exact`**, SDL level (extended in P3): 3 seeds × 6,000 random operations. Covers colour/alpha mod incl. 0/255, BLEND/NONE/ADD/MOD, flips, **2× scaled copies**, clipping, nested targets, **blended fills, clears followed by full opaque copies**. Compared against a no-fast-path ARGB reference | **EE (PCSX2): 6/6 pass**, P1c and P3/ABGR modes, 0 of 307,200 pixels differ. **Host: 6/6 pass** |
| Golden frame images | `logs/pcsx2/p3-final-*/golden_NN_*.png`; visually checked: title, dialogue, rooms, map, warp zone, tower, Panic Room, Super Gravitron |

**Coverage limit.** The golden scenes cover the measured scenes. Other screens (credits, editor, intermissions, flip mode) were not compared bit-exactly, but they use the same blit and fill operations that `sw_exact` covers.

ADD/MOD/MUL blending onto ABGR targets falls back to `SDL_Blit_Slow`:
- the result is correct, but slower;
- VVVVVV uses only BLEND and NONE [C: `grep SDL_BLENDMODE_`].

## 7. Music and memory regression [M]

| Check | Result |
|---|---|
| Underruns | **0** in every benchmark scene (A–E), in the 5-min gameplay run and in an 18-min title soak (19 track opens, 4 loops, 15 pause/resume, an SFX every 5 s) |
| Track open time | 368–380 ms, same as P2b (time-sliced; §P2b 2.4) |
| Max worker slice | 3–4 ms, unchanged |
| Ring minimum fill | 40,878 frames (0.93 s), unchanged |
| Frame spikes at music transitions | Gameplay run max period 41.9 ms (P2b: 102 ms). No new spikes |
| Cost of music (B vs D) | +1.4–2.5 ms render per frame; ~10% EE for decoding. Normal scenes stay at full speed. Worst-case tower: 28.3 (no music) vs 25.1 ticks/s |
| Peak memory | Gameplay: **16.87 MiB** `malloc_in_use` (P2b 17.16; the window buffer is now the texture's), min headroom 6.48 MiB. Benchmark runs show ~0.35 MiB more for the benchmark's own buffers |
| Leaks | 18-min soak: `malloc_in_use` flat at 17.676 MB from minute 3 to the end |
| SFX, pause/resume | Exercised in the soak; no underruns, no errors |

## 8. Real-hardware compatibility audit

### 8.1 Boot and drivers

**Code path** [C]: SDL's PS2 `main()`:
1. resets the IOP;
2. applies `sbv_patches`;
3. calls `init_ps2_filesystem_driver()`;
4. waits for the working directory's device (`waitUntilDeviceIsReady(cwd)`).

**IRX modules embedded in the ELF** [C: `nm`]:
- iomanX, fileXio, sio2man, mcman/mcserv, padman, mtapman, libsd, audsrv;
- **usbd, bdm, bdmfs_fatfs, usbmass_bd**;
- mx4sio, dev9/atad/hdd/pfs, cdfs, poweroff.

wLaunchELF's IOP state is therefore irrelevant, because the game loads its own USB stack.

**Not verifiable in PCSX2** (no USB mass-storage emulation):
- USB enumeration time;
- stick compatibility;
- `mass:` vs `mass0:` naming.

`fs_smoke.elf` checks all three on the console.

### 8.2 Paths, files and FAT32

**Base directory** [C] = `getcwd()`, which ps2sdk derives from `argv[0]`; `physfs_platform_ps2.c` falls back to `argv[0]`'s directory. From wLaunchELF this is expected to be `mass:/VVVVVV/` [E].

**Assets:**
- On PS2, `data-ps2.zip` is preferred over `data.zip`; only `data-ps2.zip` is shipped.
- `lang/` and `fonts/` are loaded from the base directory.
- Saves go to `<base>/savedata/` (`saves/`, `levels/`, `screenshots/`); directories are made with the trailing-slash-safe `ps2_mkdir`.

**FAT32:** `make-usb-package.sh` checks every name and size:
- no FAT-illegal characters;
- no case-only collisions;
- paths ≤ 255 characters;
- files < 4 GiB;
- largest file 63.4 MB.

**Tested in PCSX2 (`host:`) with the package layout** [M]:
- the game boots from the package folder without `data.zip`;
- `savedata/saves/settings.vvv` and `unlock.vvv` are created;
- `fs_smoke` reports ALL OK.

**PCSX2-specific quirk:** `remove()` returned −1 although the file was deleted, so `fs_smoke` judges deletion by the file being gone.

### 8.3 Logging and diagnostics without host:

**Before P3,** the log was written on the game thread:
- the whole buffer (up to 96 KB) was rewritten every 2 s **and on every room change**;
- a 900 KB GS snapshot was written every 1,800 loops.

On USB 1.1 each write would have been a visible stall [E].

**Now,** when cwd is not `host:` (or `storage_real.txt` exists):
- a background thread writes the log at most every 10 s and on milestones;
- room changes no longer force a write;
- snapshots are off unless `snapshots.txt` exists.

Verified in PCSX2 with `storage_real.txt` [M]: log fresh to within ~10 s at the end of a 120 s run; no snapshot files written.

**Limit:** the last ≤10 s before a hard crash may be missing from the log.

### 8.4 Slow USB reads

| Item | Size or rate | Expectation on USB 1.1 [E] |
|---|---|---|
| ELF program segments loaded by wLaunchELF | ~5.4 MB (stripped file 5.7 MB) | a few seconds |
| Assets read at start-up | 2.47 MB in 64 files, 0.37 s in PCSX2 | a few seconds before the title |
| Music | ~25 KB/s average from `data-ps2.zip`, 32 KB PhysFS reads in the worker thread | far below the bus rate; a read stalling >1 s (the ring length) would cause audible silence, counted as `underruns` |
| Log writes | ≤96 KB every ~10 s on a background thread | no game-thread stall |

`fs_smoke` measures sequential and random 32 KB reads and flags any random read over 1 s.

### 8.5 Display (PAL/NTSC, overscan)

SDL's gsKit renderer forces **NTSC 640×448 interlaced, 59.94 Hz** [C: `SDL_render_ps2.c`]. The game is 320×240, stretched by the GS: ×2 horizontally, ×1.87 vertically, with the GS's bilinear or nearest filter as in P1.

On a European SCPH-7500x the console will output this NTSC/60 Hz signal [E]:
- RGB SCART, component, and most 60 Hz-capable TVs should display it;
- **composite on a PAL-only set may lose colour or roll**.

**Overscan:** the bottom room-name line (y 232–239) and the 8 px edges may sit in a CRT's overscan [E].

**PAL 50 Hz:** not implemented, as instructed. It would also change frame pacing: 34 ms ticks vs 20 ms fields.

### 8.6 Other hardware risks

- **Speed factor:** PCSX2 does not model EE caches. The P1c illustration used 1.5× [E].
  - At 1.5×, normal rooms would still fit the 34 ms tick: render ~22–26 ms plus music.
  - The worst-case towers would drop further (~17 ticks/s).
- **EE FPU:** the real FPU is non-IEEE (no denormals, clamping). Risk for `stb_vorbis` (float) and SDL's float rect maths: low, unverified.
- **`SDL_Delay(1)` granularity** on the real kernel decides the music worker's duty cycle (P2b §7.3).
- **USB stick compatibility** varies with the bdm/usbmass driver.

## 9. USB package

```sh
scripts/make-usb-package.sh <your data.zip> [<out-dir>] [--no-build] [--image <file.img>]
```

**Output:** `build/usb-package/VVVVVV/` (git-ignored); 25 entries, 72.1 MiB.

**Contents:**
- `VVVVVV.elf`, `VVVVVV-nomusic.elf` (stripped);
- `fs_smoke.elf`;
- `data-ps2.zip`, repacked from **your** `data.zip`; the input is not modified, and the music blob's SHA-256 is verified;
- `lang/`, `fonts/`;
- `README-PS2.txt` ([docs/ps2-usb-readme.txt](ps2-usb-readme.txt));
- `SOURCE.txt`, with the sha256 of the unstripped ELFs for symbolising crash addresses;
- `MANIFEST.sha256`.

**What the script does:**
- builds the ELFs unless `--no-build`;
- runs the FAT32 rule check and verifies checksums;
- with `--image`, also builds a FAT32 disk image if mtools is installed (it is not on this host, so that option is untested).

No console is needed.

**Copying and launching:**
1. Format the stick FAT32 (MBR).
2. Copy the `VVVVVV` folder to the root and eject.
3. On the PS2: wLaunchELF → FileBrowser → `mass:/` → `VVVVVV/`.
4. Run `fs_smoke.elf` first, then `VVVVVV.elf`.

Full checklist and the diagnostics to collect: [ps2-hardware-first-boot.md](ps2-hardware-first-boot.md).

## 10. Limitations, remaining risks, conclusions

**Remaining bottlenecks** [M, run B profiles]:
- per-pixel alpha blits (`BlitRGBtoRGBPixelAlpha`, 9–21%);
- modulated glyph/sprite blits (6–8%);
- whole-frame `memcpy` (8–13%): the gameplay→game copy and the window copy;
- `SDL_FillRect4` (3–15%; the Gravitron is fill-heavy);
- music decoding (~9%);
- per-call SDL overhead (~15% in towers);
- in the towers, the 1,300-tile background redraw in logic (16 ms per tick when it happens).

**Is a GS-accelerated renderer justified? Not yet.**
- In PCSX2 every scene except the worst-case towers runs at full logic rate, with 11–23 ms idle per loop.
- GS render targets would remove the pixel work, roughly 40% of tower frames, but not the per-call overhead of 1,300–1,550 draws.
- They are a large architectural change, and the actual hardware speed factor is unknown.
- **Decide with hardware data.** If rooms on a real SCPH-7500x measure above ~25 ms render (a factor of more than ~1.7×), the first step would be a GS path for tile layers (tower and room backgrounds), not a full renderer.

**Ready for the first physical test?** Yes, as a test: the package, the self-test, the checklist and the diagnostics are in place, and nothing known blocks it. Main risks:
1. USB stick and driver compatibility;
2. NTSC output on a PAL console and cable;
3. the real EE speed factor;
4. FPU and timing differences.

None of these can be resolved without the console.

**Not done (as instructed):** PAL video mode, memory-card saves, GS render targets.

**Not done (out of scope or not possible here):**
- an audio-capture (waveform) comparison;
- USB tests (PCSX2 has no USB mass storage);
- bit-exact checks of screens outside the 10 scenes.

## Files

**VVVVVV patches:**
- `0002`: CMake, `PS2Bench.cpp`.
- `0003`: `PS2Platform.*` (real-storage logging), `PS2Bench.*` (benchmark, profiler, golden frames), `main.cpp` (logic/verify hooks).
- `0005-ps2-render-optimisations.patch` (new): `Graphics.*`, `Screen.cpp`, `GraphicsResources.cpp`, `Game.cpp`.
- Regenerated with `scripts/regen-vvvvvv-patches.sh`, which checks that the series reproduces the tree.

**SDL patches:**

| Patch | sha256 prefix | Content |
|---|---|---|
| `0003-ps2-sw-render-stats-and-dead-clears` | `8c8efdf4` | software-renderer diagnostics + dead-clear elimination |
| `0004-ps2-same-format-8888-fast-paths` | `a3143e6a` | blits and fills |
| `0005-ps2-render-copy-without-softfloat-division` | `5bc054ac` | render API without per-draw soft-float division |
| `0006-ps2-window-framebuffer-in-texture-memory` | `8913b6e1` | window framebuffer in GS texture memory |

Regenerated with `scripts/regen-sdl2-patches.sh`.

**Scripts and tests:**
- `scripts/make-usb-package.sh`;
- `scripts/ps2-profile.py`;
- `scripts/ps2-bench-compare.py`;
- `scripts/run-vvvvvv-pcsx2.sh` (`BENCH=`);
- `tests/ps2/fs_smoke/`;
- `tests/ps2/sw_exact/` (extended).

**Docs:**
- `docs/ps2-hardware-first-boot.md`;
- `docs/ps2-usb-readme.txt`.

**Reproducible build:** two clean builds (SDL from the tarball plus patches, `build-vvvvvv-ps2.sh --refetch`) give the benchmarked ELFs bit for bit:

| ELF | sha256 |
|---|---|
| `VVVVVV.elf` | `8c9bd389b95a2f53…` |
| `VVVVVV-nomusic.elf` | `74796956dcf82133…` |

The host builds (streaming on/off) still compile.

**Logs** (not committed):
- `logs/pcsx2/p3-final-{A,B,C,D,E}*`;
- `p3-gameplay-on`;
- `p3-soak-title`;
- `p3-fs-smoke`;
- `p3-usb-layout`;
- `sw_exact_opt`.
