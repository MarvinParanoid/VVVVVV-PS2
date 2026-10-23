# Diagnostics overlay (P3.3, build `P3.3-DIAG-DEV`)

**Status:** experimental build for hardware diagnosis. Validated in PCSX2 only. **No physical PS2 or CRT has shown this overlay**, so its readability on a real TV is unknown.

H1-RC2 and H2-TEST1 (`P3.2-T1`) keep their older three-line overlay; nothing here changes them.

## 1. Modes and controls

Press **L3 + R3** (both stick clicks) together. Each press moves one step; holding does not repeat.

```
OFF → COMPACT → PERF → DIAG → OFF
```

| Mode | What it shows | Area of the 320×240 game image |
|---|---|---|
| OFF | nothing (default) | none |
| COMPACT | one line, top right: `TPS 29.4 XRUN 0 HEAP 6.4M` | about 208×12, ending at x=312, y=14 |
| PERF | performance page | 288×88 at (16,16) |
| DIAG | renderer, storage and error page | 288×88 at (16,16) |

How to start in a mode:
- `overlay.txt` next to the ELF starts in COMPACT.
- In `bench.txt`, `overlay=1` gives PERF, `overlay=2` gives DIAG and `overlay=3` gives COMPACT. When a benchmark ends, PERF is shown with `BENCH DONE`.

Every change is logged as `OVERLAY off|compact|performance|diagnostics (L3+R3)`.

### Layout

The page is a black panel 16 px inside each edge of the 320×240 image. That keeps it inside the usual CRT overscan, which is about 5% per side.

| Element | Content |
|---|---|
| Header | build ID on the left; `PERF` or `DIAG` on the right |
| Rule | a line under the header |
| Six rows | grey label at x=8, value at x=88 within the panel (8×8 game font; room for 25 value characters) |
| Rule | a line under the rows |
| Footer | benchmark status, an error hint, or the next L3+R3 step |

Normal values are at most 24 characters. A longer one, such as a counter in the billions, is clipped at the panel edge; it never spills onto the game.

The overlay ignores Flip Mode, so its text stays upright. Before P3.3 it was drawn upside down in Flip Mode, and H1-RC2 still does that.

**Colours:**
- white: normal;
- pink: TPS more than 0.5 below target, or any music underrun;
- amber: an error, or failed log writes;
- green: `LAST ERR none`, benchmark status;
- grey: `N/A` and the hint.

```
P3.3-DIAG-DEV                       PERF
 ──────────────────────────────────────
 TPS        29.4 / 29.4 target
 TPS 10s    avg 29.4  min 29.3
 FRAME      34.0 ms  max 36.1
 RENDER     15.0 ms
 MUSIC      on  108 ms/s  XRUN 0
 HEAP FREE  6.3 MiB
 ──────────────────────────────────────
 L3+R3: next page (DIAG)
```

```
P3.3-DIAG-DEV                       DIAG
 ──────────────────────────────────────
 RENDERER   SDL software
 FLIP MODE  on   FASTPATH on
 FLIPPED    fast 412k  old 0
 SCRATCH    0 allocs  0 failed
 STORAGE    USB  log ok 31 fail 0
 LAST ERR   none
 ──────────────────────────────────────
 L3+R3: hide
```

## 2. Metrics

All values are recomputed every **0.5 s** (2 Hz). Between updates the overlay copies a cached texture.

| Row | Meaning | Window |
|---|---|---|
| `TPS a / b target` | **Game logic ticks per second** (simulation rate), not frames. Target = 1000/34 = 29.4. Below target, the game runs slower than real time. It never skips ticks | last 1 s |
| `TPS 10s avg / min` | Average over the last 10 s, and the lowest 1 s window within it | 10 s, in 1 s steps |
| `FRAME a ms max b` | Main-loop period: the time from one loop start to the next. Includes logic, render, present and waiting for the next tick. **Not render time.** At full speed ≈ 34 ms. `max` is the longest single period | average over 1 s; max over 10 s |
| `RENDER` | Time from the start of `render()` to the end of the present, overlay included | average over 1 s |
| `MUSIC` | Streaming state, music decode time per second of wall time, and total underruns (`XRUN`) since boot | decode: last 1 s; XRUN: since boot |
| `HEAP FREE` | See §3 | instant |
| `RENDERER` | SDL renderer name (`software` expected) | instant |
| `FLIP MODE` | Flip Mode on/off, and whether the P3.1 flipped-copy fast path is enabled | instant |
| `FLIPPED fast / old` | Flipped copies done by the P3.1 fast path / by SDL's previous code (`k` = thousands, `M` = millions) | since boot |
| `SCRATCH` | Scratch-buffer allocations by the fallback path, and how many failed | since boot |
| `STORAGE` | `USB` or `host`, and the number of successful and failed log-file writes | since boot |
| `LAST ERR` | Last error or warning logged (first 24 characters; the full text is in the log) | since boot |

**`N/A`:**
- TPS, FRAME and RENDER read `N/A` until 1 s of data exists after the overlay is opened.
- `TPS 10s` reads `N/A` until 2 s of data exists. Until 10 s have passed, it covers what exists so far.
- The compact line shows `TPS --.-` instead.
- History starts empty each time the overlay is opened, so values from earlier play do not mix in.

## 3. HEAP FREE: what it is, and what it is not

```
HEAP FREE = (end of heap − current program break) + free bytes inside malloc's arena
          = (EndOfHeap() − sbrk(0)) + mallinfo().fordblks
```

- **It is:** how much more the EE heap could still hand out in total.
- **It is not "free RAM".** It excludes:
  - the ELF;
  - static data;
  - thread stacks;
  - memory the IOP, SPU2 or GS own.
- **It is not the largest allocatable block.** Free bytes inside the arena may be fragmented, so a single large `malloc` can fail while HEAP FREE shows more than its size.

The label is therefore `HEAP FREE`, not `FREE RAM`.

H1-RC2's `FREE` showed the same quantity under a less accurate name.

## 4. Cost

| What | When |
|---|---|
| Combo poll | Every frame, in every mode, OFF included: one controller read |
| Timing counters | Every frame and every logic tick, in every mode: a few additions |
| Bucket roll | Every 0.5 s, only while visible: one `snprintf` per row |
| Glyph redraw | Every 0.5 s, only while visible: up to about 250 8×8 glyph blits into a 288×88 texture (COMPACT: about 25) |
| Texture copy | Every frame while visible: one unblended copy (COMPACT: ≤216×12; pages: 288×88) |
| Allocations, file I/O, audio locks | Never. The texture is created once, on the first show |

Measured in PCSX2: see §5.

## 5. Validation (PCSX2 v2.8.2, `preset=hardware`, ELF `462ccac8…`)

Same benchmark (`settle=4 measure=20`, 10 scenes, PCSX2 harness) for each mode. Baseline: H2-TEST1's `P3.2-T1` ELF run the same day (`logs/pcsx2/p33-h2-hidden`). Logs: `logs/pcsx2/p33g-*`.

| Scene | TPS: H2 / OFF / COMPACT / PERF / DIAG | Render ms: H2 / OFF / COMPACT / PERF / DIAG |
|---|---|---|
| title | 29.3 / 29.2 / 29.3 / 29.2 / 29.3 | 3.97 / 3.98 / 4.08 / 4.78 / 4.72 |
| intro_dialogue | 29.4 / 29.4 / 29.4 / 29.4 / 29.4 | 10.69 / 10.70 / 10.79 / 11.34 / 11.30 |
| room_ss1 | 29.3 / 29.2 / 29.3 / 29.2 / 29.3 | 14.67 / 14.67 / 14.77 / 15.33 / 15.35 |
| map | 29.3 / 29.2 / 29.3 / 29.3 / 29.3 | 17.37 / 17.44 / 17.59 / 18.08 / 18.09 |
| room_lab | 29.3 / 29.3 / 29.3 / 29.2 / 29.2 | 15.08 / 15.09 / 15.20 / 15.76 / 15.82 |
| warpzone | 29.3 / 29.3 / 29.3 / 29.3 / 29.3 | 14.60 / 14.55 / 14.73 / 15.30 / 15.28 |
| tower | 25.1 / 25.1 / 25.0 / 24.6 / 24.7 | 23.72 / 23.74 / 23.87 / 24.66 / 24.48 |
| tower_climb | 29.0 / 29.0 / 28.9 / 28.8 / 28.9 | 21.33 / 21.33 / 21.45 / 22.01 / 21.96 |
| panic_room | 25.2 / 25.2 / 25.1 / 24.7 / 24.7 | 24.25 / 24.11 / 24.30 / 25.10 / 24.99 |
| super_gravitron | 29.2 / 29.3 / 29.3 / 29.3 / 29.3 | 20.30 / 20.55 / 20.39 / 21.19 / 21.05 |

**Summary:**
- **OFF costs nothing measurable:** within ±0.1 TPS of H2-TEST1 in every scene.
- **COMPACT:** +0.10 ms render on average; TPS unchanged within noise.
- **PERF and DIAG:** +0.5 to +1.0 ms render (average +0.7 ms). TPS unchanged where the game runs at full speed. In the two CPU-bound scenes (tower, panic_room) it drops by 0.4–0.5. Most of this is the per-frame 288×88 copy: about 2.5× the area of RC2's 320×32 bar, which cost about 0.3 ms.
- **Memory:** +0.09 MiB peak, the 288×88 texture, in all visible modes.
- **Golden frames:** 10/10 identical to H2-TEST1 in OFF, COMPACT, PERF and DIAG. They are captured before the overlay is drawn.
- **Music:** 0 underruns in every run.

**Flip Mode, DIAG page** (`p33g-flip-diag`, against P3.2 Flip Mode without the overlay, `p32-B-flip`):
- golden frames 10/10 identical, also against the fast-path-off runs;
- only the fast path ran (fallback 0, scratch allocs 0 per scene);
- render +0.6 to +1.1 ms; tower and panic_room 22.9 → 22.5 TPS;
- the text is upright.

**Other checks:**
- **Save → reset → continue** with `overlay.txt` (COMPACT): back in the saved room.
- **Log writes failing:** the log path was replaced by a directory during the run. The DIAG page showed `STORAGE host  log ok 6 fail 13` in amber, and the game and the benchmark carried on.
- **Screenshots:** `logs/pcsx2/p33g-*/shot_*.png` and `logs/pcsx2/p33ui-*/shot_*.png`.

## 6. Limitations and open questions

- **Readability on a CRT or through composite is unverified.** The game font at 1:1 on a 640×448 interlaced output may flicker or blur. The 16 px margins are an assumption about overscan, not a measurement.
- **COMPACT sits top right** because time trials use the top left. It **covers the Super Gravitron's "Best Time"** (top right) and can partly cover dialogue boxes or level art in that corner; switch to OFF to see them.
- **PERF and DIAG cover most of the top half of the screen.** Use them to read numbers, not while playing.
- **FRAME and RENDER are averages.** A single slow frame shows only in `FRAME max`.
- **The overlay costs time.** A benchmark run with the overlay visible is slightly slower than one without; compare like with like.
- **Counters since boot** (FLIPPED, SCRATCH, STORAGE, XRUN) never reset while the game runs.
