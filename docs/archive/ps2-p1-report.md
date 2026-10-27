# P0/P1 Report: PS2 SDL2 environment, render strategy and present-path optimisation

**Last updated:** 2026-10-21

**Scope:**
- Standalone test ELFs and a patch to SDL's PS2 backend.
- VVVVVV game code is untouched; no game assets are used.

**Related:** [ps2-feasibility.md](ps2-feasibility.md) (Phase 1 audit).

## 0. Bottom line and P2 recommendation

| Question | Answer | Evidence |
|---|---|---|
| Reproducible PS2 build environment? | **Yes.** Pinned ps2dev image; Docker or rootless bwrap | Built here |
| Do software-renderer render targets work on the EE? | **Yes.** All pixel checks pass. The reference frame is bit-identical to the host | PCSX2, Play!, host |
| Is CPU-side final scaling gone? | **Yes.** Final copy 148.7 → **4.8 ms**, present 2.6 → **1.4 ms**. The GS stretches 320×240 to 640×448 | PCSX2 |
| Is the GS output correct (scaling, aspect, colour)? | **Yes, in PCSX2.** VRAM readback matches the window surface in **286,720 / 286,720 pixels, max delta 0** | PCSX2 |
| Was the stock output correct? | **No.** On the stock path the displayed frame is ~1.6× too bright (19% of pixels match). This is audit R7, now confirmed; the patch fixes it | PCSX2 |
| gsKit queue memory reduced safely? | **Yes.** 4.47 → **0.16 MB** in use after renderer creation. 8,960 draws/frame on a 64 KB queue: every cell pixel-exact, 0 drops | PCSX2 |
| Audio still works? | **Yes**, 99.3-100.2% delivered (main thread at priority 64) | PCSX2 |
| Pad input? | **Partial.** Pads enumerate, open and poll. **Button presses are not exercised** in automated runs | PCSX2 |
| 34 ms frame target? | **Not met.** PCSX2 gives **58.4 ms** for the optimised path (Play!: 59.2 ms). PCSX2 does not model EE cache or memory latency, so hardware may well be slower | PCSX2 (not cycle-accurate) |

> **Update (P1c):** profiling against VVVVVV's real per-frame workload, render batching, and bit-exact blit fast paths changed this assessment. Replicas of typical, dialogue, Gravitron and final-stretch frames now measure 17-29 ms in PCSX2. **P2a (minimal integration) is now recommended.** See [ps2-p1c-render-profile.md](ps2-p1c-render-profile.md).

### Recommendation (P1b, superseded by P1c): **NO-GO for P2**

The present-path work did what it was meant to do:
- CPU scaling and format conversion are gone (≈ −145 ms in PCSX2);
- about 4.8 MB of RAM is recovered;
- the stock colour bug is fixed;
- everything is pixel-verified in PCSX2.

The blocker has moved. About **52 ms per frame is SDL software composition** (sprites with colour/alpha mod 26 ms, glyphs 17 ms, background 7.7 ms). That is already above the 34 ms budget in an emulator that is likely optimistic. Porting the game on top of this would ship at roughly 15-17 fps.

**Go for P2 when all of these hold:**
1. **SCPH-7500x** runs `rt_soft_opt`. Config B "cached fg" reaches a mean ≤ 34 ms (p95 ≤ 34 ms preferred), with audio ≥ 95% and the GS-readback check PASS.
2. A person presses pad buttons during `sdl_smoke_opt`'s 10 s input window (PCSX2 with a bound pad, or hardware), and the button events appear in the log.

Condition 1 will very likely need another P1 iteration first: **P1c, composition cost**.

**P1c options**, in order of invasiveness. All of them stay outside game logic.
- **(a) Faster SDL software blits.**
  - Profile `SDL_RenderCopy` with colour/alpha mod in SDL's software renderer. The game's sprites and glyphs are unscaled 1:1, so the cheap path should apply.
  - Add or enable MMI (R5900 128-bit SIMD) or specialised blitters for ARGB8888 → ARGB8888 with modulation.
- **(b) Hybrid renderer (Option B in the audit).** Execute target-to-target compositing on the GS, using VRAM-resident 16-bit targets and the pixel-verified colour fix and queue guard from this patch.

Decide between (a) and (b) **after** the hardware numbers. If hardware is much slower than PCSX2, (a) alone will not be enough.

## 1. What was run where

| Environment | Status | Notes |
|---|---|---|
| **Build:** ps2dev image via rootless bwrap | ✅ | `scripts/ps2-env.sh` switches to Docker automatically once installed |
| **Host reference:** x86-64, SDL 2.32.10, dummy video/audio | ✅ | Validates test expectations; produces the golden frame |
| **PCSX2 v2.8.2**, BIOS SCPH-75001 v2.20 (2006-02-10, NTSC-U) | ✅ **primary** | Your dump, used in place from `~/.cache/vvvvvv-ps2/bios/` and never copied into the repo. GS readback works. EE timing is not cycle-accurate (no cache/memory-latency model) |
| **Play! 0.72** | ✅ secondary | Functional cross-check. Timing agrees with PCSX2 to within ~10% per stage. GS readback unsupported (returns zeros, so the check reports SKIP). `SDL_Quit` hangs (Play! only) |
| **SCPH-7500x** | ⏸ | Copy `out/ps2-tests/*.elf` to USB and launch with wLaunchELF; each writes `<elf>.log` (+ BMP) next to itself |

If your console is PAL (SCPH-75002/3/4), rerun with `ps2-0220e-20060210.bin`; it is extracted alongside. Output stays NTSC either way, because SDL 2.32.10 forces it.

## 2. Reproducible commands

```sh
scripts/build-sdl2-ps2.sh       # SDL 2.32.10 tarball (sha256 pinned) + patches/sdl2 -> build/sdl2-ps2-opt/prefix
scripts/build-ps2-tests.sh      # -> out/ps2-tests/{sdl_smoke,rt_soft}_{stock,opt}.elf (+ .size.txt, .map)
scripts/build-host-tests.sh && scripts/run-host-tests.sh                        # -> logs/host/
PCSX2_BIOS=~/.cache/vvvvvv-ps2/bios/ps2-0220a-20060210.bin \
  scripts/run-pcsx2.sh sdl_smoke_opt sdl_smoke_stock rt_soft_opt rt_soft_stock    # -> logs/pcsx2/<elf>/
scripts/run-play.sh sdl_smoke_opt sdl_smoke_stock rt_soft_opt rt_soft_stock      # -> logs/play/<elf>/
python3 scripts/compare-frames.py logs/host/rt_soft_stock_game.bmp logs/pcsx2/rt_soft_opt/rt_soft_opt_game.bmp
```

**Pins:**

| Component | Pin |
|---|---|
| ps2dev image | `sha256:5476fdfb…dfb05`: GCC 15.2.0, stock SDL **2.32.10**, gsKit v1.5.1 |
| PCSX2 | v2.8.2, AppImage `0c46bb6a…abea` |
| Play! | 0.72, `5523586c…04d3` |
| Patch | `patches/sdl2/0001-ps2-gs-scaled-texture-framebuffer-queue-guard.patch`, sha256 `6c607251…f1d8` |

**ELFs** in `out/ps2-tests/` (sha256 prefix):

| ELF | sha256 prefix |
|---|---|
| `rt_soft_opt` | `ac814828` |
| `rt_soft_stock` | `82df772d` |
| `sdl_smoke_opt` | `ba538fa9` |
| `sdl_smoke_stock` | `ffa97cee` |

## 3. Implementation: patch to SDL 2.32.10's PS2 backend

**Files touched:**
- `src/render/ps2/SDL_render_ps2.c`
- `src/video/ps2/SDL_ps2video.c`
- `include/SDL_hints.h` (+275 lines)

**A/B guarantee:**
- **Every feature is behind a hint.** With no hints set, texture formats and queue sizes match stock. (The colour fix is on by default, because stock output is wrong; `SDL_HINT_PS2_GS_COLOR_SCALE=0` turns it off.)
- The unmodified image SDL is still linked into `*_stock.elf`; that is the previous implementation.
- `rt_soft_opt` also runs the old path as config A in the same binary.

| Change | Mechanism | Default |
|---|---|---|
| **GS-assisted scaling** | The video driver implements `GetWindowSizeInPixels`. `SDL_HINT_PS2_WINDOW_PIXEL_SIZE="320x240"` makes the window surface, the software renderer's output and the texture framebuffer 320×240. The gsKit renderer gains `GetOutputSize` = GS framebuffer (640×448), so SDL's `RenderCopy(texfb, NULL, NULL)` is stretched by the GS | off |
| **32-bit texture framebuffer** | `SDL_HINT_PS2_PREFER_XBGR8888=1` adds XBGR8888 (GS CT32, alpha ignored) first in the format list. SDL then picks it over ABGR1555, and the final copy is a channel swizzle, not a 1555 pack | off |
| **Smaller gsKit queues** | `SDL_HINT_PS2_RENDER_QUEUE_SIZE` / `…_PERSISTENT_QUEUE_SIZE` (bytes, minimum 16 KiB) | stock: 2 MiB ×2 + 256 KiB |
| **Queue guard** | gsKit has **no bounds check** outside `GSKIT_DEBUG`. Space is now checked before every command, and the queue is executed early when short. This is safe: `gsKit_queue_exec` waits for the previous GS FINISH, and textures stay resident until `TexManager_nextFrame`. Geometry, line and point batches are split to fit an empty pool. Anything larger is dropped and counted, never overrun | always |
| **GS colour range (R7)** | TEX0.TFX = MODULATE gives texel×vertex/0x80. Textured vertex RGBA and untextured vertex alpha are scaled 255→128 (exact; SDL3 uses ×127). Texture alpha is converted on `UpdateTexture`. `RenderClear` no longer blends: stock cleared (0,0,64) to (0,0,127) after any blended draw | on; `…GS_COLOR_SCALE=0` restores stock |
| **Diagnostics** | Exported `PS2_render_stats`: texture copy, queue build, GS submit, GS finish, vsync; queue peaks, early flushes, drops. Not a public API | — |

**Why the game needs no code change:**
- `Graphics::get_stretch_info` takes the stretch rectangle from `SDL_GetRendererOutputSize`.
- With a 320×240 surface, that rectangle is (0,0,320,240) in all scaling modes, so the final copy is 1:1.

**Aspect:**
- 320×240 (4:3) fills the whole 640×448 NTSC frame, which displays as 4:3.
- The previous 597×448 letterbox was slightly too narrow on a TV. This is an inference from display geometry; check on a TV.
- 240→448 rows use nearest-neighbour duplication at ×1.87, so row heights are uneven.

**Unchanged:**
- SDL's software renderer, which provides the render-target implementation.
- gsKit's own `SetRenderTarget` no-op.
- NTSC-only output.

## 4. Results

### 4.1 Frame time per stage

PCSX2 v2.8.2: emulated time, not cycle-accurate. Scene: `rt_soft`, cached foreground, VVVVVV-style letterbox final copy, main thread at priority 64.

| Stage (ms) | stock / A | A2: CPU scale, 32-bit | B1: GS scale, 16-bit | **B: GS scale, 32-bit, 64 KB queues** |
|---|---|---|---|---|
| **Software composition** | | | | |
| bg scroll | 7.8 | 7.7 | 7.6 | 7.7 |
| gameplay (25 sprites, colour/alpha mod, flips) | 26.0 | 26.1 | 26.1 | 26.0 |
| text + UI (200 glyphs) | 17.0 | 17.0 | 16.9 | 17.0 |
| game copy | 1.3 | 1.3 | 1.3 | 1.3 |
| **Final copy: scale + format conversion (CPU)** | **148.7** | 30.8 | 30.5 | **4.8** |
| **Present** | | | | |
| texture upload: copy into gsKit texture | 2.3 | 4.6 | 0.6 | 1.2 |
| GS submit (exec) | < 0.01 | < 0.01 | < 0.01 | < 0.01 |
| GS finish (synchronisation) | 0.25 | 0.49 | 0.07 | 0.13 |
| vsync wait | < 0.01 | < 0.01 | < 0.01 | < 0.01 |
| **Frame (mean)** | **203.5 (4.9 fps)** | 88.1 (11.4) | 83.3 (12.0) | **58.4 (17.1 fps)** |
| Frame, p95 | 205.2 | 89.3 | 84.7 | 59.9 |
| Frame, foreground redrawn every frame | 234.1 | 118.7 | 113.9 | 89.0 |

Notes:
- The stock ELF and config A of the opt ELF agree to within 0.1 ms.
- Play! ratios are the same: stock 166.5 ms, B 59.2 ms.
- Vsync waits are about 0 because every frame overruns the 16.7 ms vblank.

**Attribution:**
- Each half of the old final copy costs about 30 ms on its own: stretch only (A2) 30.8 ms, 8888→1555 pack only (B1) 30.5 ms.
- Combined, as a stretch with format conversion, they cost 148.7 ms. This suggests SDL falls back to a much slower generic path for that combination (inference; not profiled).
- B removes both. What is left is a 320×240 swizzle plus a 300 KB texture copy.
- The GS-side cost (submit, DMA, finish) is ≤ 0.13 ms.

### 4.2 Memory

malloc bytes in use; identical in PCSX2 and Play!.

| Point | stock / A | B | Δ |
|---|---|---|---|
| gsKit renderer alone (`sdl_smoke`) | 4,468,040 | **159,240** | −4.31 MB |
| window + software renderer + texture framebuffer (`rt_soft`) | 5,661,048 | **819,960** | −4.84 MB |
| + 11 VVVVVV render targets + placeholder sheets | 10,872,440 | **6,031,352** | −4.84 MB |
| heap headroom at that point (of 32 MiB) | 19.77 MB | **24.59 MB** | +4.82 MB |

- Peak oneshot queue use in an Option A frame: **1,056 B**. Persistent queue: 16 B.
- Loaded ELF size: `rt_soft_opt` 1,747,484 B vs `rt_soft_stock` 1,730,060 B. The difference is the patch plus extra test configs.

### 4.3 Correctness

PCSX2 runs with VRAM readback.

| Check | stock | opt |
|---|---|---|
| 24 software render-target pixel checks + window copy | PASS | PASS (all 4 configs) |
| Reference frame vs host | 0 / 76,800 differ | 0 / 76,800 differ |
| **GS output vs window surface**, 640×448 readback | **FAIL**: 18.9% match, brightness ×1.60 | **B: 100%, max Δ 0**; A2 100% / Δ 0; A, B1 100% within 1555 quantisation (Δ ≤ 7) |
| GS clear (0,0,64) | (0,0,127) ✗ | (0,0,64) ✓ |
| Textured colour mod 128 / 255 / (200,100,50) | 255 / 255 / (255,199,99) ✗ | 127 / 255 / (199,99,49) ✓ |
| Textured alpha 128 over red | (255,255,255) ✗ | (255,127,127) ✓ |
| Queue stress 40×28 cells (2,240 draws/frame) | fills ✓, textured ✗ (colour bug) | all ✓; 30 early flushes; peak 63,392 / 65,536 B; 0 drops |
| Queue stress 80×56 cells (8,960 draws, ~2.6 MB of packets) | not run (would overrun the stock 2 MiB queue) | all 4,480 cells exact; 123 early flushes; 0 drops |

Side finding: an **ARGB8888** texture on the gsKit renderer is converted by SDL to the first listed format, ABGR1555. The GS then expands 5-bit white to 248. This does not affect Option A (game textures live in the software renderer) and is not changed by the patch.

### 4.4 Audio, input, teardown

| | stock | opt |
|---|---|---|
| Audio during rt_soft benchmarks (main priority 64) | 99.9-100.0% | 99.7-100.1% |
| Audio, dynamic vsync / input window (sdl_smoke) | ≈ 100% | 100.1% / 100.0% |
| Audio, vsync off at priority 1 / busy-wait vsync | 0% / 1.9% | 0% / 2.3% (expected; hence priority 64 and dynamic vsync) |
| Pads | 8 enumerated (all multitap slots), opened, polled | same |
| Button events | not exercised | not exercised |
| `SDL_Quit` | completes in PCSX2 (hangs only in Play!) | same |

## 5. Settings the VVVVVV PS2 build will use

These go in a PS2-only init file, not in game logic:

```c
SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
SDL_SetHint(SDL_HINT_PS2_DYNAMIC_VSYNC, "1");
SDL_SetHint(SDL_HINT_PS2_WINDOW_PIXEL_SIZE, "320x240");
SDL_SetHint(SDL_HINT_PS2_PREFER_XBGR8888, "1");
SDL_SetHint(SDL_HINT_PS2_RENDER_QUEUE_SIZE, "65536");
SDL_SetHint(SDL_HINT_PS2_RENDER_PERSISTENT_QUEUE_SIZE, "16384");
SDL_SetHint(SDL_HINT_THREAD_STACK_SIZE, "131072");
SDL_SetHint(SDL_HINT_RENDER_BATCHING, "1");        /* P1c: SDL turns batching off when the driver is forced by hint */
SDL_SetHint(SDL_HINT_SW_BLIT_FASTPATHS, "1");      /* P1c: patch 0002, bit-exact */
/* after SDL_Init: */ ChangeThreadPriority(GetThreadId(), 64);
```

**Superseded:** the go/no-go in §0 has been updated. See [ps2-p1c-render-profile.md](ps2-p1c-render-profile.md), which profiles real VVVVVV frames and now recommends P2a.

Other findings carried over from P1:
- SDL reports 8 joysticks, all with type UNKNOWN and VID/PID 0, so PlayStation glyphs must be forced.
- Unsupported APIs fail harmlessly: message box, OpenURL, LoadObject, text input (no on-screen keyboard), gsKit `RenderReadPixels`.

## 6. Remaining risks

| # | Risk | Severity | Next step |
|---|---|---|---|
| 1 | **Software composition ≈ 52 ms** (PCSX2) against a 34 ms budget; hardware likely slower | **High (P2 blocker)** | Hardware numbers, then P1c: (a) faster SDL software blits for 1:1 modulated copies, or (b) GS-side targets |
| 2 | PCSX2 EE timing has no cache/memory model | High | SCPH-7500x run of `rt_soft_opt` |
| 3 | Pad button events never observed | Medium | Interactive `sdl_smoke_opt` run in PCSX2 or on hardware |
| 4 | 240→448 nearest scaling gives uneven rows | Low/Medium | TV check; optional linear filter or 480/240p GS modes (needs newer SDL2 hints) |
| 5 | Texture alpha converted only on `UpdateTexture`, not `Lock`/`Unlock` | Low | Unused by Option A; documented |
| 6 | Maintaining an SDL fork | Medium | Keep the patch small and hint-gated; offer the colour fix and queue guard upstream (SDL2 branch) |

## 7. Artifacts

| Path | Content |
|---|---|
| `patches/sdl2/` | SDL 2.32.10 PS2 backend patch |
| `scripts/build-sdl2-ps2.sh` | Applies the patch and builds SDL |
| `tests/ps2/rt_soft/`, `tests/ps2/sdl_smoke/` | Test programs |
| `tests/ps2/common/` | Harness and GS VRAM readback; the readback re-arms GS FINISH so gsKit does not hang |
| `out/ps2-tests/` | 4 ELFs + `.size.txt` + `.map` |
| `logs/pcsx2/<elf>/` | Test logs, `emulog.txt`, reference BMPs |
| `logs/play/<elf>/` | Test logs and reference BMPs |
| `logs/host/` | Host reference logs and the golden BMP |
