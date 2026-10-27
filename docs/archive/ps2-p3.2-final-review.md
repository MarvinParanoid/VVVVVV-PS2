# P3.2: Final review, regression hardening and hardware test preparation

**Date:** 2026-10-23

**Branch:** `p3.2-final-review`, on top of `21989bd` (P3.1). Not committed.

**Evidence labels:**
- **[M]** measured in PCSX2 v2.8.2, or on the host where noted;
- **[C]** read in the source.

**No physical PS2 was used.** All performance numbers are PCSX2 numbers.

## Summary

| Question | Answer |
|---|---|
| P3.1 code review | **Passed with two hardening fixes**, no functional bug. (1) The scratch buffer was a process-wide static shared by all software renderers. It is now owned by each renderer. (2) There was no reliable way to tell which path ran. SDL now exports counters, and the test refuses to link against an SDL without them |
| Pixel-exact? | **Yes.** Golden frames identical in every comparison (§5). `flip_exact` 36/36 on PCSX2 and 36/36 on the host. `sw_exact` 6/6 on both. `VERIFY` 0 differing pixels |
| Performance consistent with P3.1? | **Yes**, every scene within noise. Flip Mode: Super Gravitron 16.9 → 29.2 TPS, Tower 17.7 → 22.9; normal mode unchanged. **0 scratch allocations** during any measured scene |
| Memory / audio | Peak 17.51 MiB in Flip Mode (+0.29 MiB scratch), otherwise unchanged; 0 underruns in all runs |
| Experimental package | **Ready:** `build/usb-hw/{VVVVVV, VVVVVV-P31DEV, GSPOC}` (147 MB), built by `scripts/make-hw-test-stick.sh` and validated in PCSX2 from the package folders |
| H1-RC2 | **Unchanged.** `SHA256SUMS` verified; package A runs RC2's frozen binary byte for byte |
| Recommendation | **P3.1 is ready for hardware testing, after the RC2 baseline session**, following `docs/ps2-hardware-test-plan.md` |

## 1. Repository state at the start [C]

- On branch `p3.1-sw-edge-cases`, the working tree was clean:
  - P3.1 was already committed by the user (`21989bd`);
  - G1 (`7e4ba3a`) and the public-release commit (`25c493c`) are below it.
- `stash@{0}` (README GIF + LICENSE note) is present and untouched.
- `origin/main` is at `25c493c`; nothing from G1/P3.1 has been pushed.
- No unrelated user changes were found.
- H1-RC2: `SHA256SUMS` verified.
- P3.2 work happens on a new branch `p3.2-final-review`. Nothing was committed, rebased or reset.

## 2. Code review of SDL patch 0008 [C]

### 2.1 Buffer lifecycle

**Before P3.2:**
- One `static SDL_Surface *sw_flip_scratch` was shared by every software renderer in the process.
- `SW_DestroyRenderer` of **any** renderer freed it; the others re-created it lazily.
- That left no dangling pointer in single-threaded use. But two software renderers on two threads (e.g. `SDL_CreateSoftwareRenderer` on a worker thread, which SDL applications do) would race on it, including a use-after-free.
- **Fix:** the scratch is now `SW_RenderData::flip_scratch`, created lazily by the renderer's first flipped copy and freed in `SW_DestroyRenderer`, with a `data != NULL` guard for the creation-failure path.

**Behaviour after the fix:**

| | |
|---|---|
| Allocated | Lazily, by `SW_FlippedCopy`, in the source texture's pixel format, at the size of the copy |
| Owner | The renderer (`SW_RenderData`) |
| Resized | When a copy is wider or taller than the scratch (new size = max of both), or the source format changes. Never shrunk |
| Released | With the renderer (`SW_DestroyRenderer`) |
| Stale state after renderer re-creation | None: a new renderer starts with `flip_scratch = NULL` (calloc'd data) |
| Shared between renderers | No |
| Leak | None per renderer. Size is bounded by the largest flipped copy the program makes: for VVVVVV the 320×240 frame (300 KiB). A program that makes one huge flipped copy keeps that much until the renderer is destroyed; the old code allocated the same amount per call and freed it. **Accepted, documented** |

### 2.2 Correctness

| Area | Finding |
|---|---|
| H, V, HV flips | Unscaled: mirrored row/column copy into the scratch, as patch 0002 did. Scaled: scale first, then reverse rows (V) and/or columns (H) in place, the rotozoom path's own order. Covered by `flip_exact` (all three flips, every scale) |
| Scaling | NEAREST only, via `SDL_PrivateUpperBlitScaled` (the same 16.16 sampling as before). LINEAR, rotation or renderer scale ≠ 1 use the previous code |
| Clipping | The final 1:1 blit clips after scaling, exactly like the previous code. `srcrect` is clipped to the texture by SDL's core before the renderer sees it. Tested on every edge |
| Alpha / blending | The scratch carries the source's blend mode and colour/alpha modulation. Bit-exact for BLEND, NONE, ADD and MOD (tested). MOD keeps the old order (modulate, then MOD-blend) |
| Pixel formats | Any 32-bit source format (the scratch uses the source format; copying is format-agnostic). Tested: ARGB8888, ABGR8888, RGBA8888, BGRA8888, XRGB8888. Indexed, 16/24-bit and RLE (`SDL_MUSTLOCK`) sources use the previous code |
| Source/destination overlap | Safe: the source is always copied into the scratch before anything is written to the destination, including a target copied onto itself |
| Zero / odd sizes | `w <= 0 || h <= 0` falls back. An empty `srcrect` never reaches the renderer: SDL's core returns early. Odd widths and heights tested (37×29 texture, 5×7 rects) |
| Pitch / alignment | All rows use the surfaces' own `pitch`. 32-bit loads on SDL-allocated surfaces are 4-byte aligned. Software renderer textures are always SDL-allocated; a screen surface with pitch W·4+52 was tested as the destination |
| Integer overflow | No size arithmetic in the patch. `SDL_CreateRGBSurfaceWithFormat` checks pitch (≤ INT32) and `size_t` overflow and returns NULL, which falls back to the previous code. Tested with a 600,000,000×1 flipped copy |
| Side effect on the source texture | The scaled branch sets the source's blend/mods to NONE/255 for the scale and restores them. `SDL_PrivateUpperBlitScaled` sets `SDL_COPY_NEAREST` in the source's blit map, and the next unscaled blit clears it again (`SDL_surface.c:766`). Effect: one extra blit-map recalculation, no pixel effect. The previous code worked on a clone and did not touch the source |

### 2.3 Failure handling

| Failure | Behaviour |
|---|---|
| Scratch allocation or growth fails | The old scratch is already freed, `flip_scratch = NULL`, and `scratch_alloc_failures` is counted. The copy goes to the previous code, which reports its own error if it also fails. The SDL error string is not touched. **Tested:** the next copies re-allocate and work |
| Scaled blit into the scratch fails | Source state restored; falls back |
| Renderer creation fails | `SW_DestroyRenderer` is called with `driverdata == NULL`; guarded |
| Unsupported or unexpected source | Not 32-bit, RLE/locked, LINEAR or rotated: falls back before any allocation |
| Error reporting | `SDL_RenderCopyEx` is batched: execution errors never reach its return value (0 on both paths). This is SDL's normal semantics and is unchanged; the failure counter makes it visible in tests |

### 2.4 Concurrency and re-entrancy

**No locks.** Correctness relies on SDL's renderer rule: a renderer and its textures are used from one thread at a time. VVVVVV renders only from its main thread.

**After the fix, different renderers on different threads share no state**, with one exception: the hint flag. It is a `SDL_bool` written by the hint callback (which runs in `SDL_SetHint`'s thread) and read per copy: a benign race, the same pattern as patches 0002/0004.

**`SDL_sw_flip_stats`** counters are plain `Uint32` increments, diagnostic only. They could under-count if two renderers copy concurrently.

**Re-entrancy:** `SW_FlippedCopy` calls only surface blits, which cannot re-enter the renderer.

**After `SDL_Quit()`** SDL clears hint callbacks. The cached flag then keeps its last value until the process restarts. This is the same pattern as the P1/P3 fast-path hints; VVVVVV never re-initialises SDL.

## 3. Changes in P3.2

| # | Change | Why |
|---|---|---|
| 1 | 0008: scratch owned by `SW_RenderData` (§2.1) | Removes shared state between renderers |
| 2 | 0008: `SDL_sw_flip_stats` counters (fast, fallback, scratch allocs, failures) | Proof of which path ran; steady-state allocation check. **Readers declare it `extern`, so they cannot link against an SDL without this patch** |
| 3 | 0008: no `SDL_ClearError` on fallback | Keeps SDL's error semantics |
| 4 | `PS2Bench`: per-scene `flipped copies: fast= fallback= scratch allocs=` line; build ID in the `bench_results` header; `tag=NAME` (suffix on every output file plus a copy of the log) | Path evidence per scene; several hardware runs in one folder without overwriting |
| 5 | `PS2Platform`: boot log line `render opts=0x…: P3 SDL fast paths on/off, flip fast path on/off` | The active path is visible in every log, not only in benchmarks |
| 6 | `tests/ps2/flip_exact`: per-mode path check plus a lifecycle test (§4) | Coverage gaps from the review |
| 7 | `scripts/make-usb-package.sh`: `--folder NAME`, `--label TEXT` (defaults unchanged) | Experimental package in its own folder with a banner |
| 8 | `scripts/make-hw-test-stick.sh` (new) | One reproducible command for the whole stick (A + B + GSPOC) |
| 9 | `scripts/build-vvvvvv-ps2.sh`: `repo <git describe>` line in `SOURCE.txt` | Source revision on the stick, `-dirty` when uncommitted |
| 10 | `run-*-pcsx2.sh`: collect `bench_results*.txt`, tagged logs, profiles | Tagged runs |
| 11 | `docs/ps2-hardware-test-plan.md`, this report, `tests/ps2/gs_poc/README-GSPOC.txt` | Deliverables |

**Note on `SOURCE.txt`.** Because of #9, a rebuild of H1-RC2 from source now gets one extra line in `SOURCE.txt`. The ELFs are unaffected: RC2 reproduction compares ELFs. RC2 itself is not rebuilt.

## 4. Tests [M]

**Link evidence.** `nm` shows `SDL_sw_flip_stats` in `flip_exact.elf`, `sw_exact_opt.elf`, `gs_poc_sw.elf` and the game ELF. `flip_exact` cannot link without it.

| Test | PCSX2 (PS2 ELF) | Host |
|---|---|---|
| `flip_exact`: REF/OLD/NEW/NEW_ARGB bit-exact vs REF, 3 seeds × 4000 flipped copies | 9/9 | 9/9 |
| `flip_exact`: each mode ran its intended path (NEW ≈3,816 fast / ≈185 fallback (LINEAR and unsupported); OLD/REF 0 fast) | 12/12 | 12/12 |
| `flip_exact`: lifecycle × 5 formats (fast = previous code over 460,800 pixels; 0 steady-state allocs; oversized copy falls back with exactly 1 failure) | 15/15 | 15/15 |
| **`flip_exact` total** | **36/36** | **36/36** |
| `sw_exact` (existing) | 6/6 | 6/6 |
| Host suite, opt (`rt_soft`, `rt_micro`, `rt_scene`, `sw_exact`) | — | 182/182, 3/3, 3/3, 6/6 |
| Host `sdl_smoke` | — | 13/16 in **both** stock and opt: no game controller and no PS2 render driver on a host. Environment, not a regression |
| `fs_smoke` in package B | ALL OK | — |

**Lifecycle test details.** In each run:
- two renderers are alive at once, with mixed flipped and plain copies;
- the scratch grows to 512×512 and shrinks back;
- one renderer is destroyed and a new one created mid-stream;
- the screen surface has a pitch of 1332 bytes, and a texture is 37×29.

## 5. Performance comparison [M]

**The runs:**
- **A** = frozen H1-RC2, run from a copy of package A;
- **B** = P3.1-DEV;
- **C** = P3.1-DEV with `opts=0xffffffef` (flip fast path off).

All runs: 10 scenes, `settle=4 measure=20 over30=0`, clean savedata, music on unless stated. Logs: `logs/pcsx2/p32-*`.

**Normal mode, music: A = H1-RC2 (frozen) / B = P3.1-DEV / C = P3.1-DEV with the flip fast path off**

| Scene | TPS A / B / C | Frame mean ms A / B / C | Frame max ms A / B / C | Render mean ms A / B / C | Render max ms A / B / C |
|---|---|---|---|---|---|
| title | 29.2 / 29.3 / 29.3 | 34.19 / 34.14 / 34.17 | 38.0 / 37.5 / 37.4 | 3.96 / 4.04 / 3.96 | 9.6 / 11.6 / 11.6 |
| intro_dialogue | 29.4 / 29.4 / 29.4 | 34.03 / 34.02 / 34.02 | 35.1 / 35.2 / 35.1 | 10.69 / 10.70 / 10.68 | 11.0 / 11.0 / 10.9 |
| room_ss1 | 29.3 / 29.3 / 29.3 | 34.17 / 34.12 / 34.18 | 37.6 / 36.9 / 37.2 | 14.75 / 14.82 / 14.74 | 23.0 / 23.5 / 24.1 |
| map | 29.3 / 29.3 / 29.3 | 34.15 / 34.19 / 34.12 | 37.0 / 37.8 / 36.6 | 17.43 / 17.46 / 17.40 | 26.0 / 26.0 / 25.4 |
| room_lab | 29.3 / 29.3 / 29.3 | 34.16 / 34.18 / 34.18 | 37.8 / 37.0 / 37.6 | 15.21 / 15.22 / 15.12 | 23.4 / 22.6 / 22.8 |
| warpzone | 29.3 / 29.3 / 29.3 | 34.13 / 34.16 / 34.16 | 37.0 / 37.1 / 37.1 | 14.57 / 14.56 / 14.52 | 23.9 / 24.0 / 22.9 |
| tower | 25.1 / 25.1 / 25.1 | 39.83 / 39.82 / 39.82 | 51.8 / 46.9 / 47.9 | 23.76 / 23.72 / 23.70 | 30.9 / 32.3 / 31.8 |
| tower_climb | 29.0 / 29.0 / 29.0 | 34.48 / 34.51 / 34.51 | 44.5 / 42.6 / 44.5 | 21.25 / 21.32 / 21.25 | 31.2 / 31.3 / 29.9 |
| panic_room | 25.2 / 25.2 / 25.2 | 39.74 / 39.72 / 39.72 | 49.0 / 48.8 / 48.9 | 24.13 / 24.03 / 24.08 | 31.9 / 32.2 / 32.2 |
| super_gravitron | 29.3 / 29.3 / 29.2 | 34.17 / 34.15 / 34.19 | 37.8 / 37.1 / 37.5 | 20.51 / 20.43 / 20.43 | 30.7 / 31.1 / 30.5 |

**Normal mode, music: C (flip fast path off) → B (P3.1-DEV)**

| Scene | TPS | TPS min (1 s) | Frame mean ms | Frame max ms | Render mean ms | Render max ms |
|---|---|---|---|---|---|---|
| title | 29.3 → **29.3** | 29.1 → 29.2 | 34.17 → 34.14 | 37.4 → 37.5 | 3.96 → **4.04** (+2%) | 11.6 → 11.6 |
| intro_dialogue | 29.4 → **29.4** | 29.3 → 29.4 | 34.02 → 34.02 | 35.1 → 35.2 | 10.68 → **10.70** (+0%) | 10.9 → 11.0 |
| room_ss1 | 29.3 → **29.3** | 29.1 → 29.1 | 34.18 → 34.12 | 37.2 → 36.9 | 14.74 → **14.82** (+1%) | 24.1 → 23.5 |
| map | 29.3 → **29.3** | 29.0 → 29.0 | 34.12 → 34.19 | 36.6 → 37.8 | 17.40 → **17.46** (+0%) | 25.4 → 26.0 |
| room_lab | 29.3 → **29.3** | 29.2 → 29.1 | 34.18 → 34.18 | 37.6 → 37.0 | 15.12 → **15.22** (+1%) | 22.8 → 22.6 |
| warpzone | 29.3 → **29.3** | 29.1 → 29.1 | 34.16 → 34.16 | 37.1 → 37.1 | 14.52 → **14.56** (+0%) | 22.9 → 24.0 |
| tower | 25.1 → **25.1** | 25.0 → 24.9 | 39.82 → 39.82 | 47.9 → 46.9 | 23.70 → **23.72** (+0%) | 31.8 → 32.3 |
| tower_climb | 29.0 → **29.0** | 28.6 → 28.5 | 34.51 → 34.51 | 44.5 → 42.6 | 21.25 → **21.32** (+0%) | 29.9 → 31.3 |
| panic_room | 25.2 → **25.2** | 24.8 → 24.8 | 39.72 → 39.72 | 48.9 → 48.8 | 24.08 → **24.03** (-0%) | 32.2 → 32.2 |
| super_gravitron | 29.2 → **29.3** | 29.1 → 29.2 | 34.19 → 34.15 | 37.5 → 37.1 | 20.43 → **20.43** (+0%) | 30.5 → 31.1 |

**Flip Mode, music: C (fast path off = the RC2 code path) → B (fast path on)**

| Scene | TPS | TPS min (1 s) | Frame mean ms | Frame max ms | Render mean ms | Render max ms |
|---|---|---|---|---|---|---|
| title | 29.2 → **29.3** | 29.2 → 29.1 | 34.20 → 34.17 | 37.5 → 37.5 | 4.00 → **4.01** (+0%) | 12.3 → 11.5 |
| intro_dialogue | 29.4 → **29.4** | 29.4 → 29.3 | 34.01 → 34.01 | 34.9 → 34.8 | 20.01 → **13.58** (-32%) | 20.3 → 14.0 |
| room_ss1 | 28.9 → **29.3** | 28.7 → 29.1 | 34.61 → 34.17 | 39.9 → 36.9 | 32.38 → **18.60** (-43%) | 39.7 → 27.0 |
| map | 29.3 → **29.3** | 29.2 → 29.1 | 34.17 → 34.12 | 37.4 → 37.2 | 22.33 → **20.11** (-10%) | 30.4 → 26.8 |
| room_lab | 27.9 → **29.3** | 27.7 → 29.1 | 35.86 → 34.15 | 40.7 → 37.6 | 34.52 → **19.21** (-44%) | 40.4 → 26.8 |
| warpzone | 24.1 → **29.3** | 23.9 → 29.2 | 41.45 → 34.19 | 50.0 → 37.2 | 33.48 → **18.48** (-45%) | 40.7 → 27.5 |
| tower | 17.7 → **22.9** | 17.3 → 22.8 | 56.39 → 43.61 | 72.2 → 51.5 | 40.36 → **27.51** (-32%) | 57.6 → 35.7 |
| tower_climb | 20.9 → **28.3** | 19.8 → 27.0 | 47.92 → 35.35 | 63.1 → 50.9 | 39.72 → **25.21** (-37%) | 51.5 → 34.7 |
| panic_room | 17.3 → **22.9** | 17.1 → 22.5 | 57.80 → 43.69 | 73.0 → 54.9 | 42.30 → **27.98** (-34%) | 59.6 → 35.8 |
| super_gravitron | 16.9 → **29.2** | 11.5 → 29.1 | 59.41 → 34.21 | 98.7 → 37.4 | 57.51 → **24.38** (-58%) | 98.4 → 35.2 |

**Golden frames: identical, 10/10, in all of these:**
- A vs B, normal mode;
- A vs RC2's own validation run;
- C vs B, normal mode;
- C vs B, Flip Mode;
- B vs the P3.1 runs, normal, Flip Mode and no music;
- C Flip Mode vs the P3.1 baseline in Flip Mode.

**Path and allocation evidence** (B/C logs, per measured scene):
- **B normal:** `fast=0 fallback=0`. Flip Mode code is not reached.
- **B Flip Mode:** `fast` = 19,371–185,176 per scene, `fallback=0`, `scratch allocs=0` in every measured scene. 2 allocations since boot, both during the first flipped frames of the golden pass: growth to the glyph size, then to the full frame. **No steady-state allocations.**
- **C Flip Mode:** `fast=0`, `fallback` = 19,338–176,328 per scene. The disabled path really is the previous code.

**Package validation runs** (the packaged `VVVVVV-P31DEV` folder, `preset=hardware`, the three tagged configurations, final ELF): they match the full runs to within ±0.1 TPS. See the expected-results table in the test plan.

**Consistency with P3.1.** B Flip Mode vs `p31-dev-flip`: identical TPS (±0.1) and goldens. C Flip Mode vs `p31-base-flip`: identical TPS (±0.1).

**First-use cost.** The scratch is allocated twice per session, on the first flipped glyph and on the first flipped frame: 2 × ≤ 300 KiB. This lands in the golden pass, outside every measurement.

## 6. Memory and audio [M]

| Run | Peak `malloc_in_use` | Minimum headroom | Underruns |
|---|---|---|---|
| A (RC2) | 17.22 MiB | 6.07 MiB | 0 |
| B normal | 17.22 MiB | 6.07 MiB | 0 |
| B Flip Mode | 17.51 MiB | 5.77 MiB | 0 |
| B no music | 16.48 MiB | 6.82 MiB | — |
| C normal | 17.22 MiB | 6.07 MiB | 0 |
| C Flip Mode | 17.22 MiB | 6.06 MiB | 0 |

`VERIFY` (SDL fast paths off vs on, in-run): 0 differing pixels in all 50 B/C renders.

## 7. Hardware test builds

**Command:**

```sh
scripts/make-hw-test-stick.sh ~/.cache/vvvvvv-ps2/assets/data.zip
```

It produces `build/usb-hw/` (147 MB):

| Folder | Content | Identity |
|---|---|---|
| `VVVVVV/` | Package A: H1-RC2 from `release/VVVVVV-PS2-H1-RC2` (not rebuilt) | `VVVVVV.elf` sha256 `2331d351…`, identical to RC2 `bin/` |
| `VVVVVV-P31DEV/` | Package B: P3.1-DEV, README banner "EXPERIMENTAL, not a release candidate", `optional/bench-{normal,flip,flipoff}.txt` | `SOURCE.txt`: `build P3.1-DEV`, `repo 21989bdc4d55-dirty`, patch 0008 `52636f69…`; game ELF unstripped `595cfdea…`, stripped `210118e4…` |
| `GSPOC/` | Optional: G1 prototypes `gs_poc_sw.elf` / `gs_poc_gs.elf` + README | Built from `tests/ps2/gs_poc` (unchanged code; `gs_poc_sw` now links SDL with 0008) |

**Isolation.**
- Each game writes saves, logs and benchmark outputs only into its own folder, so A and B cannot overwrite each other.
- Every screen, log and `bench_results*` header names the build.
- Remaining shared-state risk: a person copying a `bench.txt` into the wrong folder. The test plan addresses it.

**Redistribution.** The packages contain the user's own game data and are for the user's stick only. Nothing from them is committed.

**Source revision.** The package was built from uncommitted P3.2 changes, hence `-dirty`. The patch hashes in `SOURCE.txt` still identify the exact sources. For a clean `repo` line, rebuild after committing.

## 8. Remaining risks

1. **Hardware is untested.** The size of the Flip Mode gain on a console depends on the real cost of malloc locks and memory bandwidth, which PCSX2 does not model.
2. **Flip Mode is still slower than normal mode** in the towers (22.9 vs 25.1 TPS) because of the extra flipped full-frame pass. No further work in P3.2, by design.
3. **The scratch is retained** at the largest flipped copy size per renderer (300 KiB for VVVVVV).
4. **`SDL_sw_flip_stats` is a non-API global symbol** in our SDL build. It is harmless, but it is part of patch 0008's surface.
5. **Hint-callback lifetime across `SDL_Quit()`** follows the existing P1/P3 pattern (§2.4).
6. **Golden-frame coverage** is the 10 benchmark scenes; other flipped screens rely on `flip_exact`'s exhaustive SDL-level coverage.

## 9. Recommendation

**P3.1 (with the P3.2 hardening) is ready for physical hardware testing as an experimental build.** Run it in session 2 of `docs/ps2-hardware-test-plan.md`, after the H1-RC2 baseline (session 1).

**Do not freeze it as a release candidate before** the criteria in the plan's §8 hold on a console.

**Proposed commits** (not created):
1. `P3.2: harden flipped-copy fast path (per-renderer scratch, path counters)`: `patches/sdl2/0008…`, `tests/ps2/flip_exact/main.c`.
2. `P3.2: bench/log path reporting, tagged benchmark outputs`: `patches/vvvvvv/0003…`, `scripts/run-*-pcsx2.sh`.
3. `P3.2: hardware test packaging and plan`: `scripts/make-usb-package.sh`, `scripts/make-hw-test-stick.sh`, `scripts/build-vvvvvv-ps2.sh`, `tests/ps2/gs_poc/README-GSPOC.txt`, `docs/ps2-hardware-test-plan.md`, `docs/ps2-p3.2-final-review.md`.
