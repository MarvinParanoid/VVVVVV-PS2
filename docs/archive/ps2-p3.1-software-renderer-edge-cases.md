# P3.1: Software renderer edge cases

**Date:** 2026-10-22

**Branch:** `p3.1-sw-edge-cases`, not committed.

**Release candidate.** `H1-RC2` is unchanged (`SHA256SUMS` verified), and the first-boot checklist still uses RC2. The P3.1 change lives only in the development tree, behind a runtime switch.

**Evidence labels:**
- **[M]** measured in PCSX2 v2.8.2 (software GS renderer, BIOS SCPH-75001);
- **[C]** read in the source.

**No physical PS2 was used.** Every timing in this report is from PCSX2.

## Summary

| Question | Answer |
|---|---|
| What was slow? | **Flip Mode**, a real game option that turns the whole game upside down. In PCSX2 it **doubled render time in every gameplay scene**. Super Gravitron fell from 29.3 to 16.8 ticks/s, the Tower from 25.1 to 17.7, Warp Zone from 29.3 to 24.1 [M] |
| Why? | Every vertically flipped copy (each glyph of text, and the final frame) went through SDL's generic `SW_RenderCopyEx` path: per-call surface allocations, an ARGB8888 intermediate, rotozoom, and a converting blit that lands in `SDL_Blit_Slow` for ABGR targets [C, M] |
| What was fixed? | SDL patch **0008**: flipped copies use one persistent scratch surface in the source's format and the existing fast paths. Switch: `SDL_SW_FLIP_FASTPATH` / `PS2_OPT_FLIP_FASTPATH` |
| Faster in the game? | **In Flip Mode, yes**: Super Gravitron 16.8 → **29.2** ticks/s, Warp Zone 24.1 → **29.3**, Tower 17.7 → **22.9**, Panic Room 17.3 → **22.9**, tower climbing 20.8 → **28.2** [M]. **Normal mode: unchanged**, within noise |
| Pixel-exact? | **Yes, bit-exact.** Golden frames are identical in all 30 comparisons (3 configurations × 10 scenes). `flip_exact`, a new focused test: 9/9 on the PS2 build and 9/9 on the host. `sw_exact`: 6/6 on both. In-game `VERIFY`, now effective: 0 differing pixels in 20 renders |
| P3 flags | `PS2_OPT_OPAQUE_SCROLL` has **never had an effect**; the P3 report has been corrected. The benchmark's `VERIFY` pass **did not actually switch the SDL fast paths**; fixed |
| Also found | Golden frames depended on **saved records left over from earlier runs** (the Super Gravitron best time is drawn in the HUD). Benchmark runs now start without savedata |

## 1. The Flip Mode anomaly

### 1.1 What "flip mode" means

**In G1's synthetic benchmark** it was a scene configuration: the bordered 2× text of the Super Gravitron scene, drawn flipped vertically (`gravitron_flip`, `tests/ps2/gs_poc`).

**In the game it is a real option** [C]. In "Flip Mode" the whole game is shown upside down:
- It is unlocked by `Unlock_FLIPMODE` and toggled in Options → Gameplay (`Input.cpp:994-1002`, `toggleflipmode` `Input.cpp:320`).
- It applies to every mode started by `script.startgamemode` (`Script.cpp:2889`: `graphics.flipmode = graphics.setflipmode`).
- When it is on:
  - every glyph is drawn with `scale_y = -1` (`Font.cpp:1148`), i.e. `SDL_RenderCopyEx(..., SDL_FLIP_VERTICAL)`;
  - the finished frame is copied to the window flipped (`Graphics.cpp:3603`), and so is the screen-shake copy (`:3519`);
  - sprites use the separate pre-flipped `flipsprites` texture, so they are not affected.

### 1.2 Does it occur in normal gameplay?

**Yes, for every player who enables Flip Mode.** It is not limited to the synthetic case. In that mode, every frame contains:
- **the flipped final frame**: a 320×240 copy, 1×;
- **every flipped glyph**: 1× room names, HUD and dialogue text; with `PR_BOR` that is 5 copies per character;
- **2× scaled flipped text** where the game prints it: the Super Gravitron timers and "Best Time" (`Render.cpp:2562-2627`), "SUPER GRAVITRON"/"GO!" (2×/3×), the countdown (4×) and the map screen titles.

**Reproduced in the game** [M]. A new benchmark option `flip=1` (`bench.txt`; it sets `graphics.setflipmode` before each scene) runs the same 10 scenes in Flip Mode:

**Cost of Flip Mode before P3.1** (`p31-base-music` → `p31-base-flip`):

| Scene | TPS normal → flip | Render mean ms normal → flip |
|---|---|---|
| title | 29.3 → 29.3 | 3.98 → 4.00 |
| intro_dialogue | 29.4 → 29.4 | 10.70 → 20.12 |
| room_ss1 | 29.3 → 28.9 | 14.67 → 32.28 |
| map | 29.2 → 29.3 | 17.39 → 22.31 |
| room_lab | 29.3 → 27.8 | 15.18 → 34.64 |
| warpzone | 29.3 → 24.1 | 14.60 → 33.52 |
| tower | 25.1 → 17.7 | 23.80 → 40.01 |
| tower_climb | 29.0 → 20.8 | 21.40 → 39.71 |
| panic_room | 25.2 → 17.3 | 24.14 → 42.20 |
| super_gravitron | 29.3 → 16.8 | 20.46 → 57.64 |

## 2. Responsible SDL code path [C, M]

`SW_RenderCopyEx` (`src/render/software/SDL_render_sw.c`, SDL 2.32.10 + patches) handles every flipped copy, because SDL's core takes the plain-copy path only when `flip == SDL_FLIP_NONE`.

**Unscaled flips** (1× glyphs, the final frame) took patch 0002's fast path. It allocates a mirror surface with `SDL_CreateRGBSurfaceWithFormat` on **every call**, then blits and frees it.

**Scaled flips** (2×/3×/4× text) took SDL's generic rotozoom path, per glyph copy:
1. `SDL_CreateRGBSurfaceFrom` creates a clone of the source.
2. `SDL_CreateRGBSurface` creates an **ARGB8888** intermediate, filled by a converting scaled blit (`SDL_Blit_ABGR8888_ARGB8888_Scale`).
3. `SDLgfx_rotateSurface` allocates another surface and copies it flipped (angle 0).
4. `Blit_to_Screen` copies ARGB8888 into the ABGR8888 target with modulation and blending. SDL's generated blitters have **no ABGR8888 destinations**, so this lands in **`SDL_Blit_Slow`**.
5. Three surfaces are freed.

**Why this is so slow on the PS2** (Flip Mode baseline profile, 1 kHz sampler):

| Cost | super_gravitron | room_ss1 | Cause |
|---|---|---|---|
| `SignalSema` + `WaitSema` + `GetThreadId` + malloc/free | 16.6% | **27.5%** | newlib's allocator lock uses kernel semaphores. Every surface creation and free is several EE syscalls |
| `SDL_Blit_Slow` | **25.7%** | 0% | the ARGB → ABGR converting blit of scaled flipped text |
| `memset` | 2.2% | 3.3% | zeroing a freshly allocated 300 KiB surface for the flipped final frame, every frame |
| EE idle | 1.9% | 4.7% | (saturated) |

**So the cause is:**
- **allocation**, i.e. per-call intermediate surfaces;
- **format conversion** into an ARGB intermediate;
- **the generic rotozoom/Blit_Slow path** used for scaled flips.

It is **not** negative dimensions, texture flipping as such, scaling arithmetic or blending.

## 3. The change: SDL patch 0008

`patches/sdl2/0008-ps2-flipped-copy-fast-path.patch` (132 added lines, `SDL_render_sw.c` only).

**How it works:**
- **Unscaled flips** mirror the source rect into one **persistent scratch surface**, in the source's own format.
- **Scaled flips** are first scaled into the scratch (NEAREST, no modulation, no blending), exactly as the rotozoom path scales them. Then rows and/or columns are reversed in place.
- **Final blit.** The scratch is blitted 1:1 with the source's colour/alpha modulation and blend mode. That reaches the existing same-format P3 fast paths (`SDL_Blit_8888_Modulate_Fast` etc.).
- **Clipping** happens after scaling, as before.

This is the rotozoom path's own order of operations (scale, flip, blit), minus its allocations, its ARGB8888 intermediate and its rotation.

**When it applies:** angle 0, renderer scale 1, any flip, 32-bit sources, NEAREST scaling. Anything else (LINEAR scaling, rotation) falls back to the previous code.

**Switch.** Hint `SDL_SW_FLIP_FASTPATH` (default on; needs `SDL_HINT_SW_BLIT_FASTPATHS`). The game maps it to the new bit `PS2_OPT_FLIP_FASTPATH` (0x10), so `opts=0xffffffef` in `bench.txt` restores the previous behaviour exactly.

**Memory.** One scratch surface, created on the first flipped copy. In Flip Mode it grows to the 320×240 frame: +0.29 MiB [M]. It is never created in normal mode.

**Other changes (tooling and docs only, no rendering change):**
- `PS2Bench`: `flip=1` option; `VERIFY` now switches the SDL hints (§4).
- `scripts/run-vvvvvv-pcsx2.sh`: benchmark runs start without savedata (§5).
- `scripts/regen-sdl2-patches.sh`: hand-maintained patches 0008+ are reverse-applied before 0003-0007 are regenerated, so the file ownership scheme still works.
- `tests/ps2/flip_exact`: a new focused test (§7).
- Corrections in `docs/ps2-p3-report.md` and `docs/ps2-gs-renderer-feasibility.md`.

**Not changed:** game logic, the audio pipeline, saves, USB I/O, the G1 proof of concept code, and `release/`.

## 4. P3 flag audit [C]

| Flag | Defined / used | Effect | Finding |
|---|---|---|---|
| `PS2_OPT_TARGET_ABGR` 0x1 | `PS2Bench.h:19`; `Graphics.h:17` (`VVV_TARGET_PIXELFORMAT`) | Real, **at target creation only** | Switching `ps2_opts` at run time (the `VERIFY` pass) cannot change it; `opts=` in `bench.txt` at start-up can |
| `PS2_OPT_SDL_FASTPATHS` 0x2 | `PS2Platform.cpp:363` → hint `SDL_PS2_P3_FASTPATHS` | Real | The hint was set **once** in `PS2_PreInit`. `VERIFY` changed only `ps2_opts`, so its "reference" render still used the fast paths. **Fixed:** `VERIFY` now sets the hints for each pass |
| `PS2_OPT_WINDOW_CLEARS` 0x4 | `Graphics.cpp:517` | Real, window target only | `VERIFY` renders into `gameTexture`, where this flag never applies |
| `PS2_OPT_OPAQUE_SCROLL` 0x8 | `Graphics.cpp:793` | **None** | It forces blend NONE on `scroll_texture`'s targets, which already are NONE: SDL 2.32's `SDL_CreateTexture` sets no blend mode, and no `set_blendmode` call touches them (all call sites checked). It was never effective, rather than removed or made redundant. **Kept** (removing it changes nothing and adds risk); now documented as no effect |
| `PS2_OPT_FLIP_FASTPATH` 0x10 | new | Real | P3.1 |

**Consequences:**
- The P3 bit-exactness claims still hold. They rest on golden frames compared **across builds** (`opts=0` at start-up) and on `sw_exact`, not on `VERIFY`.
- The P3 report's explanation of row 7 was wrong and is corrected. Its `BlitRGBtoRGBPixelAlpha` share comes from BLEND tiles, not from blended scroll copies.
- With the fix, `VERIFY` compares "SDL fast paths off" with "on". It reported **0 of 76,800 pixels different** in all 10 scenes, in both normal and Flip Mode runs [M].

## 5. Baseline measurements [M]

**Builds:**
- **`P3.1-base`**: RC2's source plus only the `flip=1` benchmark option, with the unchanged SDL. ELF sha256 `5506e037…`.
- **`P3.1-dev`**: adds SDL 0008, the switch and the `VERIFY` fix. ELF sha256 `51254450…`.

**Settings for every run:** `settle=4 measure=20 over30=0`, 10 scenes, each run from clean savedata. Logs: `logs/pcsx2/p31-{base,dev}-{music,nomusic,flip}`, `p31-dev-flip-off`.

**Baseline equals RC2.** With clean savedata, the baseline's golden frames match RC2's validation run 10/10. Timings match within noise.

**Savedata finding.** The first baseline run reported a different Super Gravitron golden frame. The difference was only the "Best Time" line: `swnrecord` 114 vs 63 frames, rows 209–215. Cause:
- `run-vvvvvv-pcsx2.sh` keeps `build/vvvvvv-run/savedata` between runs;
- the benchmark's own Gravitron runs keep setting new records.

Benchmark runs now start without savedata (`KEEP_SAVE=1` keeps it); this was checked with a marker file. The release-candidate tests were not affected: they ran from fresh package copies.

## 6. Before/after comparison [M]

Same PCSX2 settings and the same scenes for both builds. TPS = logic ticks per second (29.4 = full speed); render times in ms.

**Flip Mode, music** (`p31-base-flip` → `p31-dev-flip`):

| Scene | TPS | TPS min (1 s) | Render mean ms | Render max ms | Period p95 ms | Period max ms |
|---|---|---|---|---|---|---|
| title | 29.3 → **29.3** | 29.1 → 29.2 | 4.00 → **4.05** (+1%) | 11.8 → 12.0 | 35.4 → 35.4 | 37.5 → 37.2 |
| intro_dialogue | 29.4 → **29.4** | 29.3 → 29.4 | 20.12 → **13.59** (-32%) | 20.4 → 14.0 | 34.6 → 34.8 | 34.9 → 34.8 |
| room_ss1 | 28.9 → **29.2** | 28.7 → 29.1 | 32.28 → **18.61** (-42%) | 39.6 → 27.6 | 36.8 → 35.6 | 39.9 → 36.9 |
| map | 29.3 → **29.3** | 29.1 → 29.2 | 22.31 → **20.17** (-10%) | 30.4 → 29.0 | 35.2 → 35.1 | 37.4 → 37.3 |
| room_lab | 27.8 → **29.3** | 27.6 → 29.1 | 34.64 → **19.20** (-45%) | 40.2 → 26.8 | 39.0 → 35.1 | 40.5 → 37.5 |
| warpzone | 24.1 → **29.3** | 23.9 → 29.1 | 33.52 → **18.56** (-45%) | 40.7 → 27.2 | 44.6 → 35.3 | 49.5 → 36.5 |
| tower | 17.7 → **22.9** | 17.2 → 22.8 | 40.01 → **27.46** (-31%) | 57.6 → 35.6 | 62.3 → 47.2 | 72.2 → 53.5 |
| tower_climb | 20.8 → **28.2** | 19.8 → 26.9 | 39.71 → **25.14** (-37%) | 46.3 → 34.5 | 54.6 → 41.8 | 63.0 → 45.6 |
| panic_room | 17.3 → **22.9** | 17.1 → 22.5 | 42.20 → **27.98** (-34%) | 54.9 → 35.8 | 64.3 → 47.7 | 73.4 → 54.1 |
| super_gravitron | 16.8 → **29.2** | 11.5 → 29.0 | 57.64 → **24.35** (-58%) | 96.2 → 35.0 | 92.0 → 35.6 | 105.8 → 39.7 |


**Normal mode, music** (`p31-base-music` → `p31-dev-music`):

| Scene | TPS | TPS min (1 s) | Render mean ms | Render max ms | Period p95 ms | Period max ms |
|---|---|---|---|---|---|---|
| title | 29.3 → **29.3** | 29.2 → 29.2 | 3.98 → **4.00** (+1%) | 12.0 → 11.9 | 35.3 → 35.1 | 37.3 → 37.2 |
| intro_dialogue | 29.4 → **29.4** | 29.4 → 29.3 | 10.70 → **10.69** (-0%) | 10.9 → 10.9 | 34.3 → 34.9 | 35.0 → 35.0 |
| room_ss1 | 29.3 → **29.3** | 29.0 → 29.1 | 14.67 → **14.72** (+0%) | 23.0 → 22.5 | 35.4 → 35.4 | 37.1 → 37.3 |
| map | 29.2 → **29.3** | 29.1 → 29.1 | 17.39 → **17.48** (+1%) | 24.3 → 26.0 | 35.4 → 35.3 | 36.9 → 37.0 |
| room_lab | 29.3 → **29.2** | 29.2 → 29.0 | 15.18 → **15.18** (+0%) | 22.8 → 22.6 | 35.2 → 35.6 | 38.0 → 37.5 |
| warpzone | 29.3 → **29.3** | 29.1 → 29.1 | 14.60 → **14.54** (-0%) | 23.7 → 24.4 | 35.5 → 35.6 | 37.5 → 37.5 |
| tower | 25.1 → **25.1** | 25.0 → 25.0 | 23.80 → **23.75** (-0%) | 31.3 → 32.3 | 43.0 → 43.0 | 51.6 → 48.4 |
| tower_climb | 29.0 → **28.9** | 28.6 → 28.5 | 21.40 → **21.24** (-1%) | 31.2 → 30.8 | 37.5 → 38.1 | 44.5 → 44.5 |
| panic_room | 25.2 → **25.2** | 24.9 → 24.8 | 24.14 → **24.20** (+0%) | 32.4 → 32.3 | 43.5 → 43.5 | 48.0 → 49.6 |
| super_gravitron | 29.3 → **29.3** | 29.1 → 29.1 | 20.46 → **20.43** (-0%) | 29.4 → 30.4 | 35.3 → 35.3 | 37.6 → 37.0 |


**Normal mode, no music** (`p31-base-nomusic` → `p31-dev-nomusic`):

| Scene | TPS | TPS min (1 s) | Render mean ms | Render max ms | Period p95 ms | Period max ms |
|---|---|---|---|---|---|---|
| title | 29.4 → **29.4** | 29.3 → 29.3 | 3.65 → **3.65** (+0%) | 4.0 → 4.0 | 34.7 → 34.7 | 34.8 → 34.8 |
| intro_dialogue | 29.4 → **29.4** | 29.4 → 29.3 | 10.66 → **10.67** (+0%) | 10.9 → 10.9 | 34.9 → 34.9 | 34.9 → 35.0 |
| room_ss1 | 29.4 → **29.4** | 29.4 → 29.3 | 13.00 → **13.02** (+0%) | 13.4 → 13.2 | 34.5 → 34.5 | 34.7 → 34.6 |
| map | 29.4 → **29.4** | 29.4 → 29.3 | 15.44 → **15.47** (+0%) | 15.8 → 15.8 | 34.8 → 34.8 | 35.1 → 35.1 |
| room_lab | 29.4 → **29.4** | 29.4 → 29.4 | 13.29 → **13.31** (+0%) | 13.7 → 13.5 | 34.8 → 34.8 | 35.0 → 34.8 |
| warpzone | 29.4 → **29.4** | 29.3 → 29.3 | 13.14 → **13.16** (+0%) | 13.3 → 13.6 | 34.3 → 34.3 | 35.0 → 34.9 |
| tower | 28.3 → **28.3** | 28.3 → 28.3 | 21.02 → **21.04** (+0%) | 21.3 → 21.3 | 35.5 → 35.5 | 36.1 → 36.0 |
| tower_climb | 29.4 → **29.4** | 29.3 → 29.3 | 19.13 → **19.16** (+0%) | 21.1 → 21.1 | 34.6 → 34.7 | 35.3 → 35.4 |
| panic_room | 28.7 → **28.7** | 28.7 → 28.7 | 21.17 → **21.20** (+0%) | 21.5 → 21.5 | 35.0 → 35.0 | 35.5 → 35.5 |
| super_gravitron | 29.4 → **29.4** | 29.3 → 29.4 | 17.99 → **17.94** (-0%) | 22.8 → 22.9 | 34.7 → 34.7 | 35.0 → 35.1 |


**Switch check.** `P3.1-dev` with `opts=0xffffffef` (flip fast path off) reproduces the Flip Mode baseline: every scene within ±0.6 ms render mean (largest: super_gravitron 57.6 → 58.2), and golden frames 10/10 identical.

**The G1 synthetic scenes** (`gs_poc_sw`, unchanged code, relinked):

| Scene | Before | After |
|---|---|---|
| `gravitron_flip` | 137.39 ms | **30.51 ms** |
| `room` (has one flipped 1× line) | 15.06 ms | 14.09 ms |
| other scenes | — | unchanged (±0.01 ms) |

The reference images are byte-identical to G1's.

**Focused timing** (`flip_exact`, PCSX2, OLD = RC2 behaviour):

| Stream | OLD | NEW |
|---|---|---|
| 40 bordered glyphs flipped, 1× | 15.8 ms | 8.0 ms (2.0×) |
| 40 bordered glyphs flipped, 2× | 111.5 ms | 27.5 ms (4.1×) |
| flipped 320×240 frame copy | 3.2 ms | 2.5 ms (1.3×) |

**Profile after the change** (Flip Mode, super_gravitron / room_ss1):

| | super_gravitron | room_ss1 |
|---|---|---|
| allocator/semaphores | 1.1% (was 16.6%) | 1.4% (was 27.5%) |
| `SDL_Blit_Slow` | 0% (was 25.7%) | 0% |
| `memset` | 0.1% (was 2.2%) | 0.1% (was 3.3%) |
| EE idle | 23.0% (was 1.9%) | 39.0% (was 4.7%) |

**Is real gameplay faster?**
- **Flip Mode:** yes, as measured in the game's own scenes.
- **Normal mode:** no. The changed code is not reached there, and the measurements show no change.

## 7. Pixel correctness [M]

| Check | Result |
|---|---|
| Golden frames, `P3.1-base` → `P3.1-dev`, music | **10/10 identical** |
| Golden frames, no music | **10/10 identical** |
| Golden frames, Flip Mode | **10/10 identical** |
| Golden frames, Flip Mode, switch off vs base | 10/10 identical |
| Baseline (clean savedata) vs RC2 validation run | 10/10 identical |
| In-game `VERIFY` (SDL fast paths off vs on), all scenes, normal + Flip Mode | 0 differing pixels in 20 of 20 |
| `flip_exact` on PS2/PCSX2 (new) | **9/9 pass** |
| `flip_exact` on host | **9/9 pass** |
| `sw_exact` (existing; covers flips and 2× scaling) on PS2/PCSX2 and host | **6/6 pass** on both |
| Host suite `run-host-tests.sh`, opt variant | `rt_soft` 182/182, `rt_micro` 3/3, `rt_scene` 3/3, `sw_exact` 6/6. `sdl_smoke` 13/16, and the stock variant fails the same three checks (no game controller, no PS2 render driver on the host), so these are environment failures, not regressions |
| G1 `gs_poc_sw` references | byte-identical before/after |

**What `flip_exact` covers.** 4,000 flipped copies per seed, rendered in four modes and compared pixel-for-pixel with REF (no fast paths, SDL's rotozoom path). It covers:
- V, H and HV flips;
- scales 1×, 2×, 3×, 4×, 6×, 2×1, 1×2, 3×2;
- BLEND, NONE, ADD, MOD;
- colour/alpha modulation including 0 and 255;
- sources with alpha 0, 255 and partial;
- clipping on every edge;
- a render target as the source (full-frame and partial);
- LINEAR textures (must fall back).

**Process error, corrected.** The first `flip_exact`/`sw_exact` run in this session was linked against the **stock SDL from the toolchain image**: the test build was reconfigured while `build/sdl2-ps2-opt/prefix` did not exist, and CMake cached the fallback. Its passes were therefore meaningless. The OLD and NEW timings being identical gave it away. After reconfiguring with an explicit `SDL2_DIR` and checking for the 0008 symbols in the ELFs, the tests above were run again. Only those runs are reported.

**Result: the software renderer stays bit-exact.** No approximate matching was used anywhere in P3.1.

## 8. Audio and memory [M]

| Run | Peak `malloc_in_use` | Minimum headroom | Music underruns |
|---|---|---|---|
| base / dev, music | 17.22 / 17.22 MiB | 6.07 / 6.07 MiB | 0 / 0 |
| base / dev, no music | 16.47 / 16.48 MiB | 6.82 / 6.82 MiB | — |
| base / dev, Flip Mode | 17.22 / **17.51** MiB | 6.07 / **5.77** MiB | 0 / 0 |

- **The +0.29 MiB in Flip Mode is the persistent 320×240 scratch.** The old code allocated the same 300 KiB and freed it again on every frame, which the periodic memory samples do not catch.
- **Music decode cost and underruns are unchanged.**

## 9. Remaining risks and hardware-only unknowns

1. **Allocator cost on hardware.** The old path's cost was dominated by malloc locking through kernel semaphores. PCSX2 runs the real EE kernel, but without cache or memory timing, so the size of the improvement on a console is unknown. The direction is very likely the same.
2. **Flip Mode is still slower than normal mode:**
   - Tower 22.9 vs 25.1 ticks/s;
   - render +3.7 ms in the Tower, +3.9 ms in the Super Gravitron.

   What remains is the flipped final-frame copy (a mirror into the scratch plus a 1:1 blit, two 300 KiB passes) and the per-glyph mirror/scale work. A zero-copy variant (a negative-pitch view for vertical flips) could remove part of it. It was **not implemented**: speculative, and it depends on undocumented SDL behaviour.
3. **A persistent 300 KiB in Flip Mode.** Headroom stays at 5.77 MiB [M].
4. **Coverage.** Golden frames cover the 10 benchmark scenes. Flip-mode screens outside them (credits, intermissions, the editor's flipped playtest) use the same code path that `flip_exact` covers exhaustively, but they were not compared at game level.
5. **RC2 on hardware still has the slow Flip Mode.** The hardware session should not enable Flip Mode when judging RC2's performance, or should note it clearly. RC2 cannot run the `flip=1` benchmark; that needs a P3.1 build.

## 10. Recommendations

| Item | Recommendation |
|---|---|
| SDL patch 0008 (flipped-copy fast path) | **Keep** in the development line. Release it only after RC2's hardware test, then confirm on hardware with `flip=1` |
| `PS2_OPT_FLIP_FASTPATH` switch | **Keep** for A/B runs |
| `VERIFY` hint fix | **Keep** |
| Benchmark `flip=1` option | **Keep** |
| Savedata reset for benchmark runs | **Keep** |
| `PS2_OPT_OPAQUE_SCROLL` | **Keep the flag, documented as no effect.** Remove it only together with a future patch-series cleanup |
| Zero-copy vertical flip for the final frame | **Defer.** Only if hardware shows Flip Mode is still too slow |

**What to test on hardware** (after RC2's first session):
1. A P3.1 build with `bench.txt` = `preset=hardware` plus `flip=1`, once with the default `opts` and once with `opts=0xffffffef`.
2. Normal mode with `preset=hardware`, to confirm it matches RC2.
3. A few minutes of Flip Mode gameplay with the overlay, especially the Super Gravitron.
