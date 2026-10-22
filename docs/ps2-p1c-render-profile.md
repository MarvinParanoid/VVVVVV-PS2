# P1c: Software renderer profile vs. VVVVVV's real workload

**Date:** 2026-10-21

**Scope:**
- Test ELFs and patches to SDL only.
- VVVVVV source unchanged; no game assets used.

**Related:** [ps2-p1-report.md](ps2-p1-report.md).

**Evidence labels:**
- **[M] Measured.** PCSX2 v2.8.2, BIOS SCPH-75001 v2.20. PCSX2 has no EE cache or memory-latency model, so absolute times are probably optimistic versus hardware.
- **[C] From code.** Read in VVVVVV 2.4.4 / SDL 2.32.10 source, with file:line references.
- **[E] Estimate.**

## Recommendation

**Proceed with a minimal VVVVVV integration (P2a) now**, with the two software-renderer improvements from this phase. **Do not build GS render targets yet.**

Why:
- With the improvements, measured replicas of real VVVVVV frames fit the 34 ms budget for typical rooms, dialogue, the Super Gravitron and the final stretch. They are at or slightly above budget only in the uncached-tile tower scenes (33-41 ms).
- Frame time is now dominated by **per-call overhead** in SDL's software renderer, not by pixel fill. GS render targets would not remove that overhead; the game would still issue the same ~1,300 draw calls.
- The remaining unknowns can only be measured with the real game:
  - real asset alpha coverage;
  - game-logic CPU time;
  - Ogg Vorbis decode time on the audio thread;
  - the PCSX2→hardware factor.

  P2a gets those numbers. It should be instrumented, but needs no game-logic changes.

**Revisit GS render targets only if** P2a on hardware shows common scenes (rooms, dialogue) above 34 ms at p95 *after* the per-call work listed under P1d below.

## 1. What dominates [M]

From `rt_micro`. Times are per call, drawing into a 320×240 render target. The three columns are cumulative:
- **base:** P1 config B;
- **+fast:** adds patch 0002 (`SDL_HINT_SW_BLIT_FASTPATHS`);
- **+batch:** adds `SDL_HINT_RENDER_BATCHING=1`.

| Operation | base | +fast | +batch | What scales |
|---|---|---|---|---|
| 1×1 copy (pure per-call cost) | 19.8 µs | 19.8 | **12.2** | per call |
| 8×8 tile, no mod (`RenderCopyEx` flip NONE, as VVVVVV calls it) | 24.1 | 24.1 | **14.6** | ~85% per-call |
| 8×8 tile, colour mod set/reset | 51.9 | 36.4 | **26.9** | per call + per-pixel modulate |
| 8×8 glyph (30% opaque), colour mod | 57.3 | 27.5 | **18.0** | per call + per-pixel modulate |
| 32×32 sprite, colour mod | 542 | 221 | **212** | per pixel |
| 32×32 sprite, no mod | 60.3 | 60.3 | 50.8 | per pixel |
| 320×240 copy NONE / BLEND (50% empty) | 1,227 / 2,634 | same | 1,217 / 2,626 | per pixel (16 / 34 ns/px) |
| 320×240 clear / FillRect NONE | 1,117 / 1,128 | 1,052 / 1,062 | 1,052 / 1,053 | per pixel (14 ns/px) |
| FillRect 2×2 (star) | 12.3 | 12.3 | **2.8** | per call |
| Render-target switch (incremental, with one tile) | ≈ 23 | ≈ 23 | ≈ 23 | per switch |
| 320×240 `CopyEx` FLIP_V (flip-mode final copy) | **56,480** | 3,155 | 3,148 | rotozoom path |

**Findings:**
1. **Per-call overhead is the dominant cost for VVVVVV.** It was about 20 µs per draw (~5,800 EE cycles in PCSX2), and 12 µs with batching. VVVVVV issues 100-1,300 small draws per frame.
2. **Batching was off.** SDL 2.32.10 disables render batching when the render driver is chosen via `SDL_HINT_RENDER_DRIVER` [C: `SDL_render.c:1008-1013`], so every command was flushed on its own. `SDL_HINT_RENDER_BATCHING=1` fixes it with no code change.
3. **Colour-modulated blending fell into SDL's generic auto-blitter.** It does up to 11 `/255` divisions per pixel, with no transparent or opaque shortcut [C: `SDL_blit_auto.c` `…_Modulate_Blend`]. VVVVVV colour-mods every glyph (including the 4 black outline passes of `PR_BOR` text), every coloured sprite and every textbox border tile [C: `Graphics.cpp:1219-1230`, `Font.cpp:1320-1333`].
4. Non-modulated `BLEND` already uses SDL's hand-written blitter, which has alpha 0/255 shortcuts. Fills and clears are memset-like (14 ns/px). Pixel throughput is not the main problem.
5. Flipped `RenderCopyEx` goes through rotozoom: per call it clones the surface, creates scratch surfaces and does `double` trigonometry, which is soft-float on the EE. VVVVVV hits this **only in flip mode** [C: `Graphics.cpp:1178-1196` + SDL's no-flip fast path `SDL_render.c` `RenderCopyExF`].
6. Render-target switches cost about 23 µs each. VVVVVV does 4 per frame (+4 per logic tick in warp/tower rooms) [C]. This is negligible.

## 2. Synthetic benchmark vs real code [C]

| Aspect | Synthetic `rt_soft` | VVVVVV 2.4.4 |
|---|---|---|
| Flipped sprites via `CopyEx` | 2/3 of 25 | **0** unless flip mode |
| Sprites per frame | 25, some alpha 128 | typical 3-7, max ~12-15; alpha mod always 255 |
| Glyph draws per frame | 200 | room name 75-90 (`PR_BOR` ×5); dialogue ~585; Gravitron HUD ~250 |
| Background scroll | every frame | logic rate, warp/tower rooms only |
| Translucent 288×80 fill | yes | none; textbox fills are opaque NONE |
| Uncached 8×8 tiles per frame | 0 | towers/hallways **900-1,300** |
| Textbox border tiles | none | ~70-80 colour-modded per box |

The synthetic test over-weighted pixel-heavy and flip work and under-weighted call count. The `rt_scene` replicas below are built from the real per-frame counts instead.

## 3. Realistic frame costs

`rt_scene` replicates each scene's draw sequence on the full Option A path (software render → 320×240 XBGR8888 texture framebuffer → GS). Per-scene operation **counts are [E]**, derived from game code. **Times are [M].**

| Scene (counts [E]) | P1 config B | + fast paths | **+ batching** | Hardware at 1.5× [E] |
|---|---|---|---|---|
| a. typical room: stars, 6 sprites, room name | 22.5 ms | 17.9 | **16.6** | ~25 |
| b. dialogue: (a) + 2 boxes, 210 glyphs, prompt | 52.8 | 34.7 | **28.5** | ~43 |
| c1. Super Gravitron: 5 screens of fills, 14 sprites, 250 HUD glyphs | 42.1 | 29.2 | **26.6** | ~40 |
| c2. The Tower: 1,220 uncached tiles + scroll | 51.8 | 50.6 | **37.5** | ~56 |
| c3. Panic Room minitower: 1,290 tiles + room name | 60.1 | 55.7 | **41.2** | ~62 |
| c4. tower-hallway bg: 1,200 tiles | 49.8 | 46.2 | **33.5** | ~50 |
| c5. final stretch: fg rebuild every 2nd frame | 41.6 | 33.8 | **27.1** (alternating ~18 / ~36) | ~41 |
| c7. "The Warning": 12 sprites, 156 conveyor tiles | 28.7 | 22.3 | **19.6** | ~29 |

The stock-SDL ELF (previous implementation) measures **163-201 ms** for the same scenes [M].

**Not included in any number above** (all [E]):
- **Game logic per tick.** Not measured; entity updates and collision.
- **Vorbis music decode.** It runs on the audio thread, which pre-empts rendering at priority 64; not measured.
- **Real assets.** Real sprite and glyph alpha coverage may differ from the procedural sheets.
- **PCSX2-to-hardware factor.** The 1.5× column is only an illustration.

**Frame-rate behaviour:** VVVVVV's fixed-timestep loop runs extra logic ticks when a frame is late [C: `main.cpp:913-959`]. Scenes over 34 ms therefore drop *rendered* fps (e.g. ~24 fps in c3), but game speed stays correct.

## 4. Optimisations done

All three are low risk and bit-exact.

| Change | Type | Gain (rt_scene) | Correctness [M] |
|---|---|---|---|
| `SDL_HINT_RENDER_BATCHING=1` | hint only | −1.3 to −14.5 ms (largest in tile-heavy scenes) | rt_soft config D: all 24 pixel checks pass; GS output 100% match; reference frame bit-identical |
| **Patch 0002.** Exact fast path for ARGB8888 colour-modulate + BLEND blits (skip srcA=0, direct write for srcA=255, identical arithmetic otherwise) | SDL `video/SDL_blit.c`, hint-gated | −1.2 to −18.1 ms (largest in dialogue and Gravitron) | `sw_exact`: 3 random streams × 6,000 ops (BLEND/NONE/ADD/MOD, colour/alpha mod incl. 0/255, flips, clipping, nested targets): **0 of 921,600 pixels differ** on the EE and on the host |
| **Patch 0002.** Unrotated, unscaled `RenderCopyEx` flips as one mirrored copy plus one blit instead of rotozoom | SDL `render/software`, same hint | flip mode: 56.5 → 3.2 ms per frame | same `sw_exact` run |

**Patches** (sha256 prefix):
- `0001-ps2-gs-scaled-texture-framebuffer-queue-guard.patch`: `32e8779f`
- `0002-sw-blit-fastpaths.patch`: `c389bb78`

**Validated ELFs:**

| ELF | sha256 prefix |
|---|---|
| `rt_micro_opt` | `9dc37469` |
| `rt_scene_opt` | `2a639eec` |
| `sw_exact_opt` | `ac7dfd9a` |
| `rt_soft_opt` | `c85ca996` |

## 5. Next low-risk steps (P1d, alongside P2a)

These are not done yet; gains are [E].
1. **Profile the remaining ~12 µs per call** with a PCSX2 EE profiler or sampling. Candidates [C]:
   - the `SDL_RenderCopyEx` → `RenderCopyF` float conversions and the `double` angle check (soft-float);
   - per-command `PrepTextureForCopy`, which makes 3 setters even when nothing changes;
   - `SDL_UpperBlit` clipping, and blit-map revalidation per destination.

   Halving per-call cost would take the tower scenes (1,200-1,300 draws) from 33-41 ms to roughly 25-30 ms [E].
2. **Measure Vorbis decode cost on the EE** with a free test Ogg; this decides how much frame budget audio consumes.
3. **Only if still short:** GS-side tile layers (the tower worst case) or full GS render targets (audit Option B).

## 6. How to reproduce

```sh
scripts/build-sdl2-ps2.sh && scripts/build-ps2-tests.sh
scripts/build-host-tests.sh && scripts/run-host-tests.sh           # includes sw_exact_opt on the host
PCSX2_BIOS=~/.cache/vvvvvv-ps2/bios/ps2-0220a-20060210.bin \
  scripts/run-pcsx2.sh sw_exact_opt rt_micro_opt rt_micro_stock rt_scene_opt rt_scene_stock rt_soft_opt
```

Logs: `logs/pcsx2/<elf>/<elf>.log`.
