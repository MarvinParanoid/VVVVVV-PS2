# G1: GS hardware renderer feasibility study

**Date:** 2026-10-22

**Status:** research only.
- The game, the SDL patches and release candidate `H1-RC2` are unchanged (`SHA256SUMS` verified; `fs_smoke.elf` rebuilds bit-identical).
- One standalone proof of concept was added: `tests/ps2/gs_poc/`, built only in the `opt` test variant.

**Evidence labels:**
- **[M]** measured in PCSX2 v2.8.2 (software GS renderer, BIOS SCPH-75001). PCSX2 has no EE cache model, and its GS/DMA timing is not cycle-accurate.
- **[C]** read in the current source (file:line where useful).
- **[D]** documented GS/EE behaviour (Sony GS User's Manual, ps2sdk/gsKit headers).
- **[E]** estimate.

> **No hardware measurement exists.** Every number below comes from PCSX2. Nothing here predicts real PS2 performance. §9 lists what only a console can answer.

## Summary

| Question | Answer |
|---|---|
| Can VVVVVV be drawn with GS primitives without changing game logic? | **Yes, technically.** Everything the game draws is axis-aligned textured or flat rectangles with NONE or BLEND, colour modulation, vertical flips and integer scaling. All game drawing goes through a dozen helpers in `Graphics.cpp` |
| Does SDL's existing GS renderer help? | **No.** `SetRenderTarget` is a no-op [C]. Even ignoring that, its per-call CPU cost on the same call stream equals the software renderer's in tile-heavy scenes (Tower 31.6 vs 31.7 ms [M]) |
| What would a custom GS renderer gain? | On the same synthetic Tower frame: EE packet generation **2.2 ms vs 31.7 ms** for the software path [M] (0.77 µs per draw vs ~11 µs) |
| Output correct? | 99.6–99.97% of pixels exact, max channel error 2/255, no structural differences [M]. Golden-frame CRCs cannot stay bit-exact (§3.4) |
| Biggest obstacles | 4 MiB VRAM does not hold all of the game's render targets at 32 bpp (§3.3). The interaction with SDL's own use of the GS. Readback for 3 features. A large PS2-only patch to maintain |
| Recommendation | **Defer.** Decide after the hardware benchmark, using the criteria in §6. The PoC ELFs should be run on the console in the same or a later session (§7) |

## 1. Current rendering architecture [C]

### 1.1 Configuration

**Software renderer on top of gsKit.**
- `PS2Platform.cpp:326-333` selects the **SDL software renderer**, with batching, the P1/P3 blit fast paths and a 320×240 window.
- The window framebuffer is a streaming **XBGR8888 gsKit texture**. `SDL_CreateWindowTexture` ignores the "software" hint for this and picks "PS2 gsKit" (`SDL_video.c:260-293`).
- Patch 0006 makes the window surface point directly at that texture's EE memory.

**Present path:**
1. `SDL_UpdateTexture` is a no-op copy here.
2. `RenderCopy` of the window texture becomes 2 textured triangles stretched to the 640×448 framebuffer (`SDL_render.c:3508`, `SDL_render_ps2.c:581-587`).
3. `gsKit_queue_exec`, `gsKit_finish`, `gsKit_sync`/flip.

**Division of labour.** The GS does **one** draw per frame: the final stretch. The EE does everything else, i.e. all composition into render targets.

### 1.2 Textures and render targets

**Render targets** (`Graphics.cpp:173-209`). All are `SDL_TEXTUREACCESS_TARGET`, ABGR8888 on PS2 (`Graphics.h:17`, patch 0005):

| Target | Size | Blend when used as a source | Role |
|---|---|---|---|
| `gameTexture` | 320×240 | NONE | final frame; copied to the window |
| `gameplayTexture` | 320×240 | NONE | gameplay layer; also a stale cache for the menu slide |
| `menuTexture` | 320×240 | NONE | map/pause menu |
| `ghostTexture` | 320×240 | BLEND (`Editor.cpp:1169`) | editor only |
| `tempShakeTexture` | 320×240 | NONE | screen shake |
| `foregroundTexture` | 320×240 | **BLEND** (`Graphics.cpp:2857,2909`) | cached room tiles |
| `backgroundTexture` | 336×336 | NONE | warp backgrounds |
| `tempScrollingTexture` | 336×336 | NONE | scratch for `scroll_texture` |
| `towerbg.texture` | 336×336 | NONE | tower background |
| `titlebg.texture` | 336×336 | NONE | title background |

SDL 2.32's `SDL_CreateTexture` leaves `blendMode` at 0 (NONE). Only the two targets marked above are ever set to BLEND [C].

**Image textures.**
- Loaded once as ABGR8888, STATIC, BLEND (`GraphicsResources.cpp:53-72, 169`; `SDL_render.c:1548`).
- Sprites, fonts, teleporter, elephant and the site images are recoloured to white at load (`TEX_WHITE`); the one-way tiles use a grayscale `_tint` variant.
- No image texture changes after load.
- The only per-frame `SDL_UpdateTexture` is the "bad signal" filter: it reads `gameTexture` back with `SDL_RenderReadPixels`, filters it on the CPU and writes it back (`GraphicsUtil.cpp:214,275`).

### 1.3 Per-frame flow (gameplay)

**At 30 Hz in logic** (`gamerenderfixed`):
- the tower background is redrawn into `towerbg.texture`, either in full (33×40 = **1,320 tiles**) or scrolled with `scroll_texture` plus 80–160 new tiles;
- the same for warp backgrounds (`Graphics.cpp:2737-3027`).

**Full redraws happen:**
- on room change;
- on each tower colour-cycle step (4 ticks in every 49);
- whenever the camera has to catch up with the player (`Logic.cpp:979-983`). This is the Tower and Panic Room worst case.

**Render** (`Render.cpp:2376-2521`, `Graphics.cpp:3583-3606`):
1. Into `gameplayTexture`: the background (fills and stars, a 320×240 copy of a cached background, or **1,200 uncached tiles** on final-level backgrounds 7–9).
2. The foreground: one BLEND copy of `foregroundTexture` in normal rooms, or **1,240 uncached tiles** plus 80 spikes in towers.
3. Entities, text (each glyph is 1 copy, 5 with `PR_BOR`, 9 with `PR_FULLBOR`), textboxes, fades.
4. Copy `gameplayTexture` → `gameTexture`, then overlay text.
5. `gameTexture` → window, flipped vertically in flip mode.

**Primitives.** Everything goes through helpers in `Graphics.cpp`:
- `copy_texture`, `draw_texture_part` → `SDL_RenderCopyEx` (angle always 0);
- `fill_rect`, `draw_rect`, `draw_line`, `draw_points`, `clear`.

The only blend modes are NONE and BLEND. Colour modulation uses arbitrary RGB. Alpha modulation below 255 occurs only in the editor ghosts. Scaling is integer only (2× text, a 6× sprite). There is no rotation.

**Draw counts per rendered frame** [M], from the RC2 benchmark log (`BENCH scene=… cmds/render`):

| Scene | Render cmds | Copies | Fills | Render ms | Logic ms/tick | TPS |
|---|---|---|---|---|---|---|
| title | 39 | 27 | 2 | 3.96 | 6.89 | 29.2 |
| intro_dialogue | 271 | 175 | 55 | 10.69 | 0.39 | 29.4 |
| room_ss1 | 395 | 294 | 53 | 14.75 | 0.31 | 29.3 |
| map | 649 | 624 | 5 | 17.43 | 0.23 | 29.3 |
| warpzone | 347 | 311 | 3 | 14.57 | 8.22 | 29.3 |
| tower | 1,545 | 1,507 | 3 | 23.76 | **16.04** | **25.1** |
| tower_climb | 1,290 | 1,254 | 3 | 21.25 | 8.43 | 29.0 |
| panic_room | 1,550 | 1,514 | 3 | 24.13 | **15.60** | **25.2** |
| super_gravitron | 312 | 259 | 14 | 20.51 | 0.53 | 29.3 |

In the Tower the "logic" time is mostly rendering: the 1,320-tile background redraw runs inside the logic tick.

## 2. Verified bottlenecks

### 2.1 Where the EE time goes [M]

Source: the 1 kHz profiler from the RC2 benchmark run, symbolised with `release/VVVVVV-PS2-H1-RC2/symbols/VVVVVV.elf`. Functions are grouped by hand:
- **pixel:** blit inner loops, `memcpy`, fills;
- **SDL per-call:** render API, queue, clipping, `UpperBlit`;
- **game draw:** `Graphics::*` and text.

| Scene | Idle | Pixel work | SDL per-call | Music decode | Game draw | Pixel : per-call (of busy time) |
|---|---|---|---|---|---|---|
| room_ss1 | 49% | 31% | 5% | 10% | 1% | 60% : 9% |
| map | 42% | 35% | 8% | 10% | 1% | 60% : 15% |
| super_gravitron | 33% | 43% | 4% | 12% | 1% | 64% : 6% |
| tower_climb | 12% | 48% | 21% | 11% | 4% | 55% : 23% |
| **tower** | **0%** | **47%** | **34%** | 11% | 4% | **47% : 34%** |
| **panic_room** | **0%** | **48%** | **34%** | 11% | 4% | **48% : 34%** |

**Normal scenes.** The EE is idle 33–49% of the time in PCSX2. They do not need a GS renderer at PCSX2 speed.

**Tower worst case.** The EE is saturated: about half the time goes to pixel work and a third to SDL per-call overhead.
- The single largest function is `BlitRGBtoRGBPixelAlpha` (21.6%). That is the per-pixel alpha blend of the ~2,600 unmodulated BLEND tiles per loop: 1,320 background plus 1,240 foreground.
- Per-call functions include `SDL_RenderCopyF`, `SDL_UpperBlit`, `SDL_IntersectRect`, `PrepQueueCmdDraw` and `RenderGetViewportSize`, each 2–5%.

**So the bottleneck is CPU-side composition of many small 8×8 tiles: pixel blending plus per-draw overhead.** It is not GS fill rate, not texture upload (0.01–0.02 ms per frame) and not GS DMA (0.13–0.16 ms per frame) [M].

### 2.2 Correction to the P3 report [C]

`PS2_OPT_OPAQUE_SCROLL` (patch 0005, `Graphics.cpp:786-815`) forces blend NONE on `scroll_texture`'s copies. But:
- those targets are never set to BLEND (`set_blendmode` call sites);
- SDL 2.32 creates textures with blend NONE.

The flag therefore changes nothing. It is harmless and was not changed (no renderer changes in G1).

The P3 report attributed `BlitRGBtoRGBPixelAlpha` + `memcpy` (22% in `tower_climb`) to blended scroll copies. That attribution is wrong. The scroll copies were already plain copies (the `memcpy` part). The per-pixel alpha blending comes from the BLEND **tiles**: 10.4% in `tower_climb`, 21.6% in `tower` [M].

## 3. GS constraints relevant to VVVVVV

### 3.1 Capabilities

| Need | GS | Notes |
|---|---|---|
| Textured and flat rectangles | SPRITE primitive, 2 vertices [D] | The PoC draws every op as one sprite: 3 qwords in REGLIST mode |
| NONE / BLEND | ABE off, or ALPHA = (Cs−Cd)·As>>7 + Cd [D] | Alpha 0x80 = 1.0. Alpha test (ATST NOTEQUAL 0, fail KEEP) skips transparent texels, as SDL does |
| Colour modulation | TFX=MODULATE: Cv = Ct·Cf>>7 [D] | Only 128 steps per channel, so not exact in general (§3.4) |
| Vertical flip | Swap V coordinates | Needs a −1/16 texel bias (§3.4) [M] |
| Integer scaling | Sprite with a larger destination, nearest filter | 2× text matched SDL exactly [M] |
| Render targets | FRAME and TEX0 may point at any VRAM area [D] | No gsKit API for this. The PoC writes FRAME/SCISSOR/TEXFLUSH through A+D [C, M] |
| Paletted textures | PSMT8/PSMT4 with a CT32 CLUT (CSM1 order) [D] | Every VVVVVV sheet fits losslessly (§3.3) |
| Max texture size | 1024×1024 (TW/TH ≤ 10) [D] | The largest image is 960×96 (`teleporter.png`) |
| Readback (GS → EE) | Local→host transfer [D] | Slow and synchronous. Needed for: bad-signal filter, screenshots, benchmark golden CRCs. SDL's PS2 renderer has no `RenderReadPixels` [C] |

### 3.2 The existing SDL GS renderer ("PS2 gsKit") [C]

- `SetRenderTarget` returns 0 and does nothing, yet the driver advertises `SDL_RENDERER_TARGETTEXTURE` (`SDL_render_ps2.c:307-310, 906`).
- No CLUT formats; only CT32/CT16 textures.
- No `RenderReadPixels`.
- Every copy becomes 2 textured triangles (SDL has no `QueueCopy` for it), with a `gsKit_TexManager_bind` per draw chunk.
- MOD/MUL blend is approximate (`SDL_render_ps2.c:551-556`).
- gsKit source is not available locally, so TexManager eviction and transfer behaviour can only be inferred from headers.

### 3.3 VRAM budget [C, M]

**Images.** All 21 graphics sheets need 4.24 MiB at 32 bpp, which is more than the whole 4 MiB VRAM. Unique colours per sheet (read from the user's `data.zip`, not copied anywhere) [M]:
- tiles: 160 / 133 / 205 colours;
- sprites and flipsprites: 28 / 21;
- most others: ≤ 22.

As lossless PSMT8/PSMT4 they need **0.87 MiB**. The partial alpha in `sprites.png` (9 levels) goes into the CLUT's alpha.

**Render targets.** The game creates six 320×240 and four 336×336 targets. Page-aligned at 32 bpp they need 6 × 320 KiB + 4 × 528 KiB = **3.94 MiB**. With a 640×448 framebuffer (1.09 MiB) that is far over 4 MiB.

**What the PoC used.** 5 targets, a single framebuffer and 4 paletted sheets: 3.58 MiB of 4.00 [M].

**What a real renderer needs.** A target lifetime plan. For example:
- allocate `ghostTexture`, `titlebg`, `menuTexture` and `tempShakeTexture` only while they are in use;
- or scan out a 320×240 buffer directly instead of keeping a 640×448 framebuffer;
- or replace `scroll_texture` by a wrap-around (REPEAT) background, which removes `tempScrollingTexture`.

None of this is designed yet. It is the main architectural risk.

### 3.4 Exactness: GS arithmetic vs SDL [D, M]

- **Unmodulated copies, fills, binary-alpha BLEND, 2× scaling:** exact.
- **White textures with arbitrary colour modulation:** exact, if the CLUT stores RGB 0x80: 0x80·m>>7 = m, the same as SDL's 255·m/255. Every modulated glyph and sprite in the PoC scenes matched, apart from partial-alpha edge pixels.
- **Coloured textures with modulation** (the grayscale one-way tiles): ±1–2. The GS colour scale has 128 steps, SDL's has 255.
- **Partial alpha** (`sprites.png` edges, alpha 247–254): ±1. The two blend formulas round differently.
- **Vertical flips:** PCSX2's GS samples sprite V at the top edge of each pixel row. Without a −1/16 texel bias, flipped 2× text was shifted by one row (706 wrong pixels, error up to 145). With the bias it matches. This is an emulator observation; check on hardware.

**Consequence.** The benchmark's CRC golden frames cannot stay bit-exact on a GS path. The regression check would become "≥ 99.5% exact pixels, max channel error ≤ 2, no pixel off by more than 4", plus a readback path.

## 4. Candidate architectures

| | A. Current SDL software renderer | B. SDL "PS2 gsKit" renderer | C. Custom GS renderer behind `Graphics.cpp` helpers | D. Hybrid: GS tile layers, software sprites/text overlay |
|---|---|---|---|---|
| Works today | yes | **no**: no render targets; would need targets, CLUT and readback added to SDL | PoC only | no |
| EE cost, synthetic Tower frame [M] | 31.7 ms | 31.6 ms (call stream only, output wrong) | **2.2 ms** packet build | [E] ~3–5 ms GS part + ~3 ms software overlay + overlay clear/upload |
| EE cost per 8×8 tile [M] | ~11.3 µs | ~11.2 µs | **0.77 µs** | GS part as C |
| VRAM | 1 framebuffer (gsKit) | targets + textures at 32 bpp: does not fit | needs paletted textures and target lifetimes (§3.3) | as C, plus an overlay target |
| Upload bandwidth per frame | 300 KiB window texture (0.13–0.15 ms [M]) | TexManager re-transfers whatever is evicted (unknown) | ~0 in gameplay (textures and CLUTs once; the PoC's 132 KiB Tower packet is commands) | 300 KiB overlay every frame |
| Correctness risk | none (golden frames) | high (MOD/MUL approximations, no targets) | low to medium: ±2 levels, flip bias, readback for 3 features | medium: draw order (fades and fills sit between layers) |
| Complexity | done | high, inside SDL | high: renderer, VRAM manager, CLUT conversion, readback, fallback switch | higher than C for less gain |
| Maintenance | small patches | patching SDL's backend | a large PS2-only VVVVVV patch to rebase on upstream | both |
| SDL compatibility | full | full API | bypasses SDL rendering. SDL must not also drive the GS (its window texture uses gsKit) — unverified | both at once |
| Potential gain | — | only pixel-heavy scenes (Gravitron 28→3 ms [M]); none for tiles | all rendering, including the Tower worst case | less than C |

**B is rejected.** It would need render targets, CLUT and readback implemented in SDL. Its per-call cost alone already equals the software renderer's in the scenes that are slow.

**D is not recommended.** It needs the same GS target machinery as C, so it is no cheaper to build. It adds a 300 KiB overlay upload and draw-order hazards, and sprites and text are the easy part for the GS anyway.

**Alternatives without the GS** (not studied here; listed for completeness):
- cheaper tile paths in software, e.g. drawing towers' tile rows directly into the target surface instead of one SDL call per tile;
- skipping or amortising the full tower background redraw.

These attack the same 34% per-call share and part of the pixel share, at much lower risk.

## 5. Implementation effort [E]

These are rough estimates for one developer familiar with the code. They are not measured.

| Work item (approach C) | Estimate |
|---|---|
| Backend for the `Graphics.cpp` helpers: copy, fill, clear, lines/points, target switch, blend, mods; packet builder from the PoC | 1–2 weeks |
| VRAM manager with target lifetimes; CLUT conversion at load, including translated and recoloured sprites | 1 week |
| Present path without SDL's gsKit window texture; vsync and pacing | 0.5–1 week |
| Readback (bad-signal filter, screenshots, benchmark CRCs); tolerance-based golden checks | 0.5–1 week |
| Flip mode, menus and slide cache, editor ghosts, screen shake, fades; runtime switch back to the software path | 1 week |
| Validation in PCSX2 and on hardware | 1 week + hardware sessions |
| **Total** | **about 5–7 weeks**, plus ongoing rebase cost of a large patch |

## 6. Success criteria

### Trigger: when a GS renderer is worth starting

Decide after the RC2 hardware benchmark (`bench_results.txt`) and the PoC on hardware (§7). Start only if **both** of these hold:
1. **The software path is too slow on hardware.** Any of:
   - a common scene (intro_dialogue, room_ss1, map, super_gravitron) has `tps` < 29.0 or `tps_min` < 28.0;
   - or tower/panic_room has `tps` < 24 (clearly worse than PCSX2's 25).
2. **The GS path is fast on hardware.** `gs_poc_gs` reports C build plus GS completion for the Tower frame at ≤ 25% of `gs_poc_sw`'s total for the same frame, with correctness as in PCSX2 (≥ 99.5% exact, max delta ≤ 2).

**If hardware matches PCSX2** (common scenes at full speed, only the Tower and Panic Room worst case at ~85%), a GS renderer would fix a transient catch-up case at the cost of a large patch. Then **reject it for now** in favour of the software alternatives in §4.

### Acceptance: what a GS renderer must achieve on hardware

- Tower and Panic Room worst case: `tps` ≥ 29.0 and `tps_min` ≥ 28.0.
- All benchmark scenes: render mean ≤ 8 ms, max ≤ 20 ms, period p99 ≤ 36 ms.
- EE idle ≥ 30% in normal rooms (profiler `kernel/idle`); 0 music underruns.
- VRAM within 4 MiB with no per-frame texture uploads in gameplay (CLUT and texture uploads only on load or room change).
- Pixels: ≥ 99.5% exact, max channel delta ≤ 2, 0 pixels off by more than 4 in every benchmark scene, including flip mode. Visual review of fades, menus and screen shake.
- The software path stays selectable at run time.

PCSX2 results alone do not meet these criteria.

## 7. Proof of concept: `tests/ps2/gs_poc/`

### What it is

Two ELFs execute the **same display lists**:
- **Images:** generated in code, with the statistics of the real sheets (no game assets).
- **Scenes:** sized from the RC2 benchmark counts.

| Scene | Ops | Copies | Fills | Models |
|---|---|---|---|---|
| room | 351 | 294 | 53 | room_ss1: starfield, cached foreground (BLEND), 12 modulated sprites, bordered text, one flipped line |
| tower | 2,807 | 2,801 | 0 | worst case: full 1,320-tile background redraw + scroll copy + 1,240 foreground tiles + 80 spikes |
| tower_climb | 1,575 | 1,567 | 0 | typical: `scroll_texture` via the temp target + 84 new tiles + foreground |
| super_gravitron | 265 | 247 | 14 | 11 nested fills, 20 sprites, 2× bordered text |
| gravitron_flip | 265 | 247 | 14 | the same in flip mode (2× text flipped) |
| room_setup | 905 | 903 | 0 | foreground cache redraw on room entry |

**The two programs:**
- **`gs_poc_sw.elf` (path A).** Software renderer, configured with the game's hints and called the way `draw_grid_tile` calls it (QueryTexture, colour/alpha mod set and reset, RenderCopyEx). It writes `gs_poc_ref_<scene>.bin` (`gameTexture`) as the reference. With `gs_poc_b.txt` present it uses SDL's "PS2 gsKit" renderer instead (path B, cost only).
- **`gs_poc_gs.elf` (path C).**
  - Raw GIF packets; PSMT8 + CLUT sheets; CT32 targets in VRAM.
  - Measures EE packet build, DMA kick, and DMA + GS completion (FINISH) separately, and texture/CLUT/frame uploads.
  - Compares its `gameTexture` readback with the reference.

### Results in PCSX2 [M]

Means of 60 frames. Logs are in `logs/pcsx2/g1-poc-{sw,gs,b}/`.

| Scene | A: software total | B: SDL gsKit (cost only) | C: EE packet build | C: DMA+GS (emulator) | C packet | C exact pixels | C max Δ |
|---|---|---|---|---|---|---|---|
| room | 15.06 ms | 4.05 ms | **0.22 ms** | 0.01 ms | 16 KiB | 99.64% | 2 |
| tower | 31.66 ms | 31.62 ms | **2.16 ms** | 0.06 ms | 132 KiB | 99.97% | 2 |
| tower_climb | 24.64 ms | 18.09 ms | **1.21 ms** | 0.03 ms | 74 KiB | 99.97% | 2 |
| super_gravitron | 28.21 ms | 3.09 ms | **0.17 ms** | 0.01 ms | 12 KiB | 99.92% | 1 |
| gravitron_flip | **137.39 ms** | 3.24 ms | 0.17 ms | 0.01 ms | 12 KiB | 99.91% | 1 |
| room_setup | 9.00 ms | 10.11 ms | 0.70 ms | 0.02 ms | 42 KiB | — | — |

**Uploads, path C** (mean of 10) [M]:
- PSMT8 512×256 (128 KiB): 59 µs;
- PSMT8 512×512 (256 KiB): 114 µs;
- CLUT (1 KiB): 3–4 µs;
- one CT32 320×240 frame (300 KiB): 133 µs.

**How to read this:**
- **C's EE cost is 7–15× below A's** on every scene. It scales with the op count at 0.63–0.78 µs per op. This is the one result expected to carry over to hardware in *direction*; the size of the ratio needs the console.
- **"DMA+GS" is emulator timing and says nothing about the real GS.** For a scale: the Tower frame writes about 0.8 M pixels, including target clears and the final 640×448 stretch. At the documented ~1.2 Gpixel/s textured peak that is about 0.7 ms [D/E], but small sprites, CLUT loads and texture-cache misses will cost more. This must be measured on the console.
- **The synthetic scenes cost more than the real game in path A:** room 15.1 vs 14.75 ms, Gravitron 28.2 vs 20.5 ms. That is because they are calibrated on op counts, not on time. Comparisons are A vs C on the same scene, not against the game.
- **Flip mode slow path:** the software renderer took 137 ms for the flip-mode Gravitron, because flipped *and* scaled copies leave SDL's fast paths. Whether the game hits this in flip mode (Gravitron text is drawn at 2×) is unverified. It can be checked in PCSX2 by playing the Gravitron in flip mode with the overlay on, independently of any GS work.
- **PCSX2 pitfalls found while writing the PoC:**
  - The GIFtag PRIM field (PRE) had no effect with REGLIST data: the GS drew single points. That is consistent with PRE applying to PACKED mode only. PRIM is now written by A+D.
  - `ps2_screenshot` with a non-zero `y` returned zeros. Bands are now read by page-row address instead.

### Running it

```sh
scripts/build-ps2-tests.sh                 # builds build/ps2-tests-opt/gs_poc_{sw,gs}.elf
mkdir -p build/gpoc && cp build/ps2-tests-opt/gs_poc_*.elf build/gpoc/
PCSX2_BIOS=… scripts/run-package-pcsx2.sh build/gpoc gs_poc_sw.elf g1-poc-sw 120   # first: writes the references
PCSX2_BIOS=… scripts/run-package-pcsx2.sh build/gpoc gs_poc_gs.elf g1-poc-gs 50
```

**On a console:**
1. Copy both ELFs into a folder on the stick (for example `mass:/GSPOC/`).
2. Start `gs_poc_sw.elf` and wait about 1–2 minutes. There is no "done" screen; the log ends with `END`.
3. Reset and start `gs_poc_gs.elf`, then wait about 30 s.
4. Bring back `gs_poc_sw.log`, `gs_poc_gs.log` and the `gs_poc_gs_*.ppm` files.

These ELFs are not part of RC2.

## 8. Recommendation: **defer**

**The case for GS acceleration is real:**
- the worst case is CPU-bound composition of thousands of 8×8 tiles;
- a custom GS path cuts its EE cost by an order of magnitude in PCSX2, with near-exact output;
- the game funnels all drawing through a small set of helpers.

**But do not start it now:**
1. **Unknown need.** On PCSX2, every common scene already runs at full speed with 33–49% idle EE. Only the Tower and Panic Room catch-up case is slow (85%). The real console may be slower or faster; the hardware benchmark decides (§6).
2. **Cost and risk.** About 5–7 weeks [E] and a large PS2-only patch. A VRAM lifetime design is required, because the targets do not fit at 32 bpp. Ownership of the GS must be taken over from SDL, and golden frames lose bit-exactness.
3. **Cheaper options first.** If the hardware shows only the Tower worst case is slow, try the software alternatives (§4) first.

**Next steps** (no development until the hardware results arrive):
1. Run the RC2 hardware benchmark.
2. Run `gs_poc_sw`/`gs_poc_gs` on the console.
3. Apply §6.

## 9. Questions only real hardware can answer

1. **Actual speed of the software path:** `bench_results.txt` for all six hardware-preset scenes, and the profile split (pixel vs per-call) on a real EE with real caches.
2. **EE packet-build cost:** PCSX2 shows 0.63–0.78 µs per op with no cache model. Real cost depends on write-back cache behaviour for a 132 KiB packet; uncached or scratchpad building may be needed.
3. **GS completion time** for ~2,800 small PSMT8 sprites with CLUT loads and several target switches: texture page buffer (8 KiB) behaviour, page breaks, `TEXFLUSH` cost.
4. **GIF DMA throughput** for 74–132 KiB per frame, and contention with the SPU2/IOP traffic of music streaming.
5. **Rasterisation and blending details:**
   - sprite sampling convention (the −1/16 V bias for flips);
   - MODULATE and ALPHA rounding;
   - alpha test behaviour.

   PCSX2's software renderer emulates them; the console is the authority.
6. **Readback:** whether `ps2_screenshot` bands work at non-zero `y` on hardware, and how slow a 300 KiB GS→EE readback is (needed for the bad-signal filter, screenshots and benchmark CRCs).
7. **SDL and a custom GS path together:** whether SDL's PS2 video driver can run without its gsKit window texture, so that a custom renderer owns the GS. This needs code work and can be tested in PCSX2 first.
8. **The PCSX2→hardware ratio for path A vs path C:** whether the 7–15× gap survives.
