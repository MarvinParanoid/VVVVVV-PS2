# Hardware diagnostics: overlay, audio output, exception screen

Builds: `P3.3-DIAG-DEV` (overlay), `P3.4-HW-DIAG` (adds audsrv output metrics, §7, and a private exception-report stack, §8).

**Status:** experimental builds for hardware diagnosis. Validated in PCSX2 only. **No physical PS2 or CRT has shown any of this**, so readability on a real TV and behaviour on a console are unknown.

H1-RC2 and H2-TEST1 (`P3.2-T1`) keep their older three-line overlay; nothing here changes them.

## 1. Modes and controls

Press **L3 + R3** (both stick clicks) together. Each press moves one step; holding does not repeat.

```
OFF → COMPACT → PERF → DIAG → OFF
```

| Mode | What it shows | Area of the 320×240 game image |
|---|---|---|
| OFF | nothing (default) | none |
| COMPACT | one line, top right: `TPS 29.4 PCM 0 HEAP 6.4M` (P3.3: `XRUN`) | about 200×12, ending at x=312, y=14 |
| PERF | performance page | 288×88 at (16,16) |
| DIAG | Flip Mode, storage, audio output and error page | 288×88 at (16,16) |

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
 MUSIC      on  108 ms/s  PCM UR 0
 HEAP FREE  6.3 MiB
 ──────────────────────────────────────
 L3+R3: next page (DIAG)
```

```
P3.4-HW-DIAG                        DIAG
 ──────────────────────────────────────
 FLIP MODE  on   fast 412k  old 0
 STORAGE    USB  log ok 31 fail 0
 AUDSRV Q    88 ms  min  74 ms
 SUBMIT     gap  22 ms  late 0
 DRY EST    0 ms (by clock)
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
| `MUSIC` | Streaming state, music decode time per second of wall time, and **music ring underruns** (`PCM UR`, compact line `PCM`; `XRUN` in P3.3): times the Vorbis decoder had not produced PCM when the mixer asked. Says nothing about audsrv (§7) | decode: last 1 s; PCM UR: since boot |
| `HEAP FREE` | See §3 | instant |
| `FLIP MODE on fast a old b` | Flip Mode on/off; flipped copies done by the P3.1 fast path / by SDL's previous code (`k` = thousands, `M` = millions). P3.3 showed the fast-path switch, the renderer name and the scratch counters too; in P3.4 they are in the log every 10 s (`DIAG renderer=... flip fastpath=... scratch allocs=...`) | since boot |
| `AUDSRV Q`, `SUBMIT`, `DRY EST` | audsrv output queue, see §7 | §7 |
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
- **Counters since boot** (flipped copies, STORAGE, PCM UR, late, starved?) never reset while the game runs.

## 7. Audio output: audsrv queue (P3.4)

### Why

There are two buffers between the music decoder and the speakers:
1. The game's **Vorbis PCM ring** (`Music.cpp`). Its underruns are `PCM UR`.
2. **audsrv's ring on the IOP**. SDL's audio thread feeds it 512 frames (2,048 bytes, 11.6 ms) per submission. Before each submission, `audsrv_wait_audio` waits for space.

If the SDL audio thread falls behind, audsrv's player keeps reading. It replays stale bytes from its ring, which sounds like a **buzz or a repeating fragment**. Every P3.3 counter would still read 0. PCSX2 is unlikely to show this. See [ps2-open-source-ports-comparison.md](ps2-open-source-ports-comparison.md), recommendation #2.

### What audsrv provides

**Sources:**
- The EE client (`libaudsrv.a` in the pinned ps2dev image), disassembled.
- The IOP module (`audsrv.irx`, "version 0.93"), read from ps2sdk's `iop/sound/audsrv/src`. The version string matches; the exact commit of the image is unknown.

| Fact | Evidence |
|---|---|
| `audsrv_queued()` returns **bytes queued in the IOP ring, in the format given to `audsrv_set_format`**. Here that is 44,100 Hz, 16-bit stereo, 176.4 bytes/ms. It is computed from the ring's read and write positions | IOP `audsrv_queued()`. On the EE it is one SIF RPC (command 27) |
| Ring size = 10 × (512 × freq / 48000) frames. At 44.1 kHz stereo 16-bit that is 18,800 bytes = **106.6 ms**. The player consumes 1,880 bytes (10.7 ms) per step | IOP `audsrv_set_format()`, `up_44100_16_stereo` |
| `audsrv_play_audio` copies raw bytes (no conversion) and returns the number accepted, which can be less than requested | IOP `audsrv_play_audio()`; SDL ignores the return value |
| `audsrv_wait_audio(n)` **blocks inside the RPC** until `available ≥ n`. Every EE call goes through one semaphore (`WaitSema` around `sceSifCallRpc` in `call_rpc_1`) | EE disassembly; IOP `audsrv_wait_audio()` |
| When the ring runs dry, the player does **not** stop. It keeps advancing its read position past the write position. There is no underrun counter | IOP `play_thread()` |

**Consequences:**
- `audsrv_queued()` may only be called from the SDL audio thread. From any other thread it would wait while the audio thread is blocked in `audsrv_wait_audio`, up to one submission period.
- **After a starvation the depth is wrong.** Once the reader has passed the writer, `queued()` reports a near-full ring. A low depth is a warning; a dry ring may show up as a high one.
- When the positions are equal, `queued()` returns 0 and `available()` also returns 0, so "empty" and "full" look the same.

### What P3.4 measures (SDL patch `0009-ps2-audsrv-stats`)

**Collection.** In `PS2AUDIO_PlayDevice`, on the SDL audio thread, at every submission:
- one `audsrv_queued()` call, just before `audsrv_play_audio`;
- the time since the previous submission;
- the duration of the `play_audio` call.

Samples go into a 64-entry ring. The game reads it once per frame, with no RPC (`PS2_LoopBegin`). The overlay, the benchmark and the 10 s log line each keep their own window.

| Name | Definition |
|---|---|
| `AUDSRV Q` now / min | Depth before a submission, converted to ms: bytes ÷ 176.4. "min" is the lowest over the last 10 s (overlay), over the scene (benchmark), or over the last 10 s (log) |
| `SUBMIT gap` | Longest interval between two submissions in the last 10 s. One submission is 11.6 ms of audio; in PCSX2 the longest gap is 21–23 ms (submissions come in pairs). After a stall they come back-to-back |
| `late` | Submissions that came later than the audio queued after the previous one: interval > (depth before the previous submission + bytes it added) ÷ 176.4 B/ms. **By the clock, the ring must have run dry.** Since boot |
| `DRY EST` | The sum, over late submissions, of how long the ring was dry by the same clock arithmetic: interval − queued audio. An estimate of how long audsrv replayed stale data. Since boot |
| `starved?` (log and benchmark only) | Submissions where the depth was **higher** than just after the previous submission, or exactly 0. Only this thread writes the ring, so between submissions the depth can only fall; a rise means the reader passed the writer. **Weak, see Limits** |
| `short` (log only) | `audsrv_play_audio` accepted fewer bytes than given |
| `queued() call max` (log only) | Longest `audsrv_queued()` RPC |
| `lost samples` (log only) | Samples the game did not read before the 64-entry ring wrapped (main loop stalled > 0.7 s). Windows then miss them; the counters do not |

**Limits:**
- **`starved?` misses real dry-outs (measured).** `wait_audio` keeps the ring nearly full, so the depth just after a submission is close to the ring size. After a dry-out the reader is some way around the ring, and the wrapped depth almost always reads *lower* than that, not higher. In the stall test it caught **0 of 2** dry-outs, which is why the overlay shows `DRY EST` instead.
- **`starved?` false positives.** A depth of exactly 0 can also mean a full ring (equal positions). That is not expected here, because `wait_audio` leaves at least 2,048 bytes free before each submission.
- **Depth after a dry-out is meaningless** for the same reason. `AUDSRV Q min` is only trustworthy while `late` stays 0.
- **`late` and `DRY EST` are timing inferences.** They assume the IOP plays at exactly 44.1 kHz from the moment of the last submission, and the IOP consumes in 10.7 ms steps. So they can be off by about one step (10.7 ms) near the threshold. A dry-out shorter than that may be missed or over-counted.
- **Window boundaries.** The 10 s log line and the overlay read the counters live but the samples once per frame, so one stall's `late` can appear in the window before its `gap`.
- **Neither is an audible-glitch detector.** A few ms of stale data may be inaudible. Whether a glitch is heard is a question for the tester.
- **Not covered:** the IOP's own timing (SPU2 DMA, IOP load from USB). If the IOP itself falls behind, the EE-side numbers do not show it.

**Cost.** One extra SIF RPC per submission (86 per second) on the audio thread, measured below. It can be switched off for A/B runs with `opts=` bit `0x20` (`SDL_PS2_AUDSRV_STATS=0`).

**Other changes in the log and benchmark:**
- The log gets an `AUDSRV` line every 10 s and a `BENCH scene=... audsrv:` line per scene.
- `bench_results.txt` keeps its columns and appends four: `aq_min late dry_ms starved?`.

## 8. Exception screen: private report stack (P3.4)

**Before P3.4:**
1. libeedebug's level-1 vector switches to its own 8 KB stack, saves all registers to a frame, and calls our handler.
2. The handler copies the frame and points EPC at `exception_report`.
3. libeedebug's `_ee_load_frame` reloads every register from the frame, then `eret`.

So the report ran on **the faulting thread's stack**. With a stack overflow or a corrupt `$sp`, it faulted again. The second fault spun forever in exception context with interrupts masked, so no screen appeared. glampert/quake2-ps2 hit exactly this on hardware (commit `0e010ec`).

**P3.4 changes:**
- The handler still runs on libeedebug's stack in exception context. It still makes no syscalls.
- Before returning, it rewrites the **saved frame** that `_ee_load_frame` restores:
  - `sp` = top of a static 16 KB stack (64-aligned array; top 16-aligned, 64 bytes below the end);
  - `gp` = `_gp`;
  - `s8` (fp) and `ra` = 0, so stack walks stop;
  - `epc` = `exception_report`.

  The offsets were checked in the built ELF against `_ee_load_frame`: `sp` 464, `gp` 448, `s8` 480, `ra` 496, `epc` 520. The upper halves of `sp` and `gp` are written as 0, so the 128-bit `lq` loads a correctly sign-extended address.
- **The original registers stay in the saved copy.** The screen, the serial line and the log line show the faulting `SP`, `RA`, `EPC`, `BadVAddr`, `Cause` and `Status`.

**Nested faults** (bounded; no restarts):
- **A fault inside the report** (its `$sp` is inside the report stack):
  - one fallback runs, `exception_report_fallback`, with a fresh stack. It writes one line to the EE serial port and, if the screen was already set up, one red line on screen ("second exception … during report stage …").
  - Then the thread parks. A further fault in the fallback just parks.
- **A fault in another thread:** that thread is resumed in `exception_park` (`for (;;) SleepThread();`) on a small shared park stack, instead of spinning in exception context. The report thread keeps running.

**Why `SleepThread()` is legal there.** After `eret` the CPU runs in the faulting thread's own context; the kernel did not switch threads. `exception_report` has always ended in `SleepThread()` in this context, and that path is exercised by the PCSX2 crash test since H1.

**Not handled:** a fault inside an interrupt handler or with interrupts disabled resumes in that context. Whether the report works there is unknown. On the PS2's priority scheduler, a parked thread no longer blocks lower-priority threads, but a fault that leaves the kernel itself inconsistent cannot be reported.

**Tests** (`crashtest.txt` next to the ELF; **TEST ONLY**, never in packages or benchmarks). About 20 s after start-up:

| Content | What happens |
|---|---|
| anything else | `teq` trap (code 13) |
| `badsp` | `$sp` set to `0x02800000` (outside the 32 MB RAM), then a trap |
| `nested` | trap; then the report traps once more, in its log stage |

Only the trap path can be tested in PCSX2. It does not raise address errors, and it skips TLB misses on null pointers. TLB-miss and address-error exceptions (codes 2–5) are unproven until hardware.

`audiostall.txt` (**TEST ONLY**): from about 20 s on, every 10 s, the main thread holds the SDL audio device lock for 20, 60, 85, 100, 150 and 300 ms. The log then lists the `late`, `dry est` and `starved?` increments for each stall.

## 9. P3.4 validation (PCSX2 v2.8.2)

ELF `66e280a8…` (`VVVVVV-nomusic.elf` `f393e292…`). Logs: `logs/pcsx2/p34-*`, `p34b-*`, `p34c-*`. These are PCSX2 results; nothing here has run on a console.

**Exception screen** (`crashtest.txt`):

| Test | Result |
|---|---|
| `trap` | Screen, serial line and log line. EPC = `PS2_LoopEnd+0xc38` (the `teq`), `SP 01ffe5b0` = the main thread's own stack, not the report stack |
| `badsp` | Same, with `SP 02800000`: the report ran although the faulting `$sp` points outside RAM |
| `nested` | First report up to its log stage, then on serial: `second exception (code 13 EPC 001ffaf4 …) during report stage log`. EPC = `exception_report+0x25c`, the test trap. The fallback ran once; the system did not spin in exception context. Its on-screen red line was not captured (the report never reached its screenshot step) |

In PCSX2's captured screen the game's last frame shows through below the text. The same happens with H1-RC2 (`logs/pcsx2/rc-crashtest`), so it is not new.

The `badsp` test shows that the new path works. It does not show that the old one would have failed in PCSX2: PCSX2 does not fault on unmapped accesses the way hardware does. Codes 2–5 (TLB miss, address error) remain untested.

**Audio stall test** (`audiostall.txt`; expected dry time = stall + one submission − the ~106 ms queued):

| Stall | `late` | `dry est` | `starved?` | Lowest depth afterwards |
|---|---|---|---|---|
| 20 ms | 0 | 0 | 0 | 70 ms |
| 60 ms | 0 | 0 | 0 | 33 ms |
| 85 ms | 0 | 0 | 0 | 20 ms (106.6 − 91.7 ms gap ≈ 15 expected) |
| 100 ms | 0 | 0 | 0 | 10 ms |
| 150 ms | **1** | **56 ms** | 0 | (meaningless after a dry-out) |
| 300 ms | **1** | **191 ms** | 0 | (meaningless) |

Below the dry-out point, the depths match the clock, which confirms the units (input bytes ÷ 176.4 B/ms).

**Benchmarks** (10 scenes, `settle=4 measure=20`):

| Run | Compared with | TPS | Render |
|---|---|---|---|
| OFF, stats on | P3.3 OFF | ±0.1 | −0.25…+0.11 ms (avg +0.00) |
| OFF, stats off (`opts=0xffffffdf`) | P3.3 OFF | ±0.1 | avg +0.02 ms |
| COMPACT | P3.3 COMPACT | −0.1…0 | avg +0.05 ms |
| DIAG | P3.3 DIAG | ±0.1 | avg +0.07 ms |
| Flip Mode + DIAG | P3.3 same | ±0.1 | ±0.1 ms |

- Golden frames: 10/10 identical to P3.3 in every run.
- Flip Mode used only the fast path (fallback 0).
- Music underruns 0. Memory peak unchanged (17.22 / 17.31 MiB). The static additions are a 16 KB report stack, a 512 B park stack and the 0.5 KB sample ring.
- audsrv in every scene: lowest depth 74 ms, longest gap 21–23 ms, `late` 0.
- `audsrv_queued()` takes about 0.36 ms per call, occasionally 2 ms. `audsrv_play_audio` takes about 0.42 ms.
- **The extra RPC has no measurable cost in PCSX2.** Its cost on IOP/SIF hardware is unknown, which is why the switch exists.

**Other checks:**
- L3+R3 cycles COMPACT → PERF → DIAG (log).
- Save → reset → continue returns to the saved room.
- `VVVVVV-nomusic.elf` logs `AUDSRV` lines (sound effects use audsrv too).
- Host tests: rt_soft 182/182, rt_micro 3/3, sw_exact 6/6, rt_scene 3/3, flip_exact 36/36. In PCSX2, rebuilt against the patched SDL: `flip_exact` 36/36, `sw_exact` 6/6.
