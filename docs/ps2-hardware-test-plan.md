# Hardware test plan: H1-RC2 baseline, P3.1-DEV, optional G1

**Target:** PS2 Slim SCPH-7500x, FMCB → wLaunchELF → USB (FAT32).

**Nothing in this project has run on a real PS2 yet.** All expected values below are PCSX2 measurements. Treat them as reference points, not predictions: the console may be slower or faster.

Do the sessions **in order**. Session 1 must be complete before the experimental build is touched.

**Contents:**
1. Preparing the stick
2. Session 1: H1-RC2 baseline
3. Session 2: P3.1-DEV (experimental)
4. Session 3: G1 prototypes (optional)
5. Getting the files back
6. Expected results (PCSX2) and what counts as a pass
7. Troubleshooting
8. Criteria for making P3.1 the next release candidate

## 1. Preparing the stick (on the PC)

```sh
# needs: out/vvvvvv-ps2 built with PS2_BUILD_ID=P3.1-DEV, build/ps2-tests-opt (fs_smoke, gs_poc_*)
scripts/make-hw-test-stick.sh ~/path/to/data.zip
#   -> build/usb-hw/{VVVVVV, VVVVVV-P31DEV, GSPOC}
#   it must print "release H1-RC2 unchanged" and "package A runs the frozen RC2 binary"
```

Copy the three folders to the **root** of a FAT32 stick (MBR partition table; ~150 MB):

```
/VVVVVV/          package A  H1-RC2, the frozen release candidate
/VVVVVV-P31DEV/   package B  P3.1-DEV, EXPERIMENTAL (README starts with a banner)
/GSPOC/           optional   G1 prototypes
```

Check the copy on the PC, then eject safely:

```sh
for d in VVVVVV VVVVVV-P31DEV GSPOC; do (cd /run/media/$USER/STICK/$d && sha256sum -c --quiet MANIFEST.sha256 && echo "$d OK"); done
sync
```

**Isolation between packages:**
- Each game writes its saves (`savedata/`), log and benchmark files **into its own folder**, so A and B never share or overwrite state.
- Nothing is written to the memory card.
- **The only shared risk is human:** copying a `bench.txt` into the wrong folder, or mixing up photos. Every screen and log names its build, so check the build ID before writing anything down.

| | Package A | Package B |
|---|---|---|
| First line of the start-up list | `… experimental test build H1-RC2` | `… experimental test build P3.1-DEV` |
| Overlay status line | `STATUS OK  H1-RC2 …` | `STATUS OK  P3.1-DEV …` |
| `vvvvvv_ps2.log` first line | `=== VVVVVV PS2, build H1-RC2 ===` | `=== VVVVVV PS2, build P3.1-DEV ===` |
| `bench_results*.txt` header | (no build field) | `# VVVVVV PS2 benchmark, build P3.1-DEV: …` |

**Write down for every session:**
- console model (sticker);
- FMCB and wLaunchELF versions;
- stick model and size;
- TV and cable;
- date.

## 2. Session 1: H1-RC2 baseline (package A, folder `VVVVVV`)

**Flip Mode stays OFF for this whole session.** RC2 has the known slow Flip Mode path (fixed in P3.1). Baseline numbers must be normal mode only.

1. **Stick and files.** wLaunchELF → FileBrowser → `mass:/`. All three folders must be visible. If not, see §7.
2. **Storage self-test.** `mass:/VVVVVV/fs_smoke.elf`.
   - It must end with **ALL OK**.
   - Photograph the screen. Note "device ready after", "sequential … KB/s" and "random … worst".
   - Reset.
3. **Boot the game.** `mass:/VVVVVV/VVVVVV.elf`.
   - The start-up list must end in **H1-RC2**. Note the seconds from launch to the title screen.
4. **Basic checks:**
   - Press ✕ on the title: menu music within ~0.5 s.
   - **L3+R3** shows the overlay. Note TPS / FRAME / RND / FREE, then hide it.
   - Start a new game and skip the dialogue.
   - Walk through 5–10 rooms. Check controls (stick and D-pad, ✕ flip, ○ back, △ map) and graphics (no garbage, no missing tiles). Listen for clicks or gaps in the music at room transitions.
   - Overlay in a normal room: **TPS ≈ 29.4, UNDERRUN 0** is expected.
5. **Save and load.**
   - △ → → → (SAVE tab) → ✕.
   - Reset, start the game again, then **play → continue**: you are back in the saved room.
6. **Benchmark.**
   1. Copy `VVVVVV/optional/bench.txt` into `VVVVVV/` (wLaunchELF: copy, then paste in `VVVVVV/`).
   2. Launch `VVVVVV.elf` and touch nothing for about 4 minutes, until the overlay shows **BENCH DONE**.
   3. Reset, then **delete `VVVVVV/bench.txt`**.
7. **Collect files:** §5.

## 3. Session 2: P3.1-DEV (package B, folder `VVVVVV-P31DEV`)

1. `mass:/VVVVVV-P31DEV/fs_smoke.elf` must end with **ALL OK** (this checks package B's own files). Reset.
2. **Boot.** `mass:/VVVVVV-P31DEV/VVVVVV.elf`. The start-up list must end in **P3.1-DEV**. If it says H1-RC2, you are in the wrong folder.
3. **Normal gameplay.** Repeat session 1 step 4 briefly; numbers should match package A.
4. **Flip Mode.**
   - **If it is not unlocked yet:** title → options → accessibility → **unlock play modes** → **unlock flip mode**. This writes the unlock to `VVVVVV-P31DEV/savedata/`; package A is unaffected.
   - **To turn it on:** title → play → **play modes** → **flip mode**, then start or continue a game.
   - **Check:** everything should be upside down and correct. Text must be readable (mirrored vertically, not garbled).
   - **Overlay:** note TPS in a normal room.
   - Start or continue a game: everything should be upside down and correct.
   - Text must be readable (mirrored vertically, not garbled).
   - Overlay: note TPS in a normal room.
5. **Benchmarks.** The three tagged configurations write separate files, so they can run one after another:

   | Run | Copy this file to `VVVVVV-P31DEV/bench.txt` | Writes |
   |---|---|---|
   | B-normal | `optional/bench-normal.txt` | `bench_results_normal.txt`, `vvvvvv_ps2_normal.log` |
   | B-flip | `optional/bench-flip.txt` (Flip Mode, fast path **on**) | `bench_results_flip.txt`, `vvvvvv_ps2_flip.log` |
   | B-flipoff | `optional/bench-flipoff.txt` (Flip Mode, fast path **off** = the RC2 code path) | `bench_results_flipoff.txt`, `vvvvvv_ps2_flipoff.log` |

   **For each run:**
   1. Copy the file as `bench.txt`, replacing the previous one.
   2. Launch, wait for **BENCH DONE** (~4 min), then reset.

   **After the three runs, delete `bench.txt`.**

   `flip=1` sets Flip Mode inside the benchmark only. The Flip Mode setting from step 4 does not matter for the benchmark.
6. **Compare on the spot** (optional). The overlay's last status names the results file. On a PC later, compare the three files (§6).
7. **Interactive Flip Mode** (with the overlay on, a few minutes each where reachable from your save):
   - the Tower;
   - the Panic Room (the final level);
   - the Super Gravitron (Secret Lab; if locked, unlock it in the same accessibility menu: **unlock secret lab**).

   Note the TPS range and any audio clicks or gaps.
8. **Save and load** in package B (as session 1 step 5). The save lands in `VVVVVV-P31DEV/savedata/`; package A's save must be unaffected.
9. **Collect files:** §5.

## 4. Session 3: G1 prototypes (optional, folder `GSPOC`)

1. Run `mass:/GSPOC/gs_poc_sw.elf`. Wait 1–2 minutes: there is no "done" screen; the log ends with `END`. Reset.
2. Run `mass:/GSPOC/gs_poc_gs.elf`. Wait about 30 s; it shows the synthetic scenes. Reset. It needs the `gs_poc_ref_*.bin` files written by step 1.
3. Collect `gs_poc_sw.log`, `gs_poc_gs.log` and `gs_poc_gs_*.ppm`.

**How to read the numbers** (`docs/ps2-gs-renderer-feasibility.md`):
- **"build"** is only the EE time to write the GS command packet. It is **not** GPU rendering time.
- **"gs"** runs from the DMA start until the GS raises **FINISH**. FINISH is set only after the GS has completed all drawing queued before it, so this is the real completion time on a console. In PCSX2 that timing is not representative.
- **"UPLOAD"** lines are EE→VRAM transfers, also measured to FINISH.
- **The two programs answer different questions:**
  - `gs_poc_sw` is the current software path, including its present to the GS;
  - `gs_poc_gs` is the hypothetical GS renderer.

  Compare `gs_poc_sw`'s total with `gs_poc_gs`'s build **plus** gs, never with build alone.
- **"CORRECT"** lines compare the GS output with the software reference: ≥ 99.5% exact and max delta ≤ 2 in PCSX2.

## 5. Getting the files back (after each session)

1. Power off the console normally; do not pull the stick while a game runs. The log is written about every 10 s on USB, so the last seconds before a crash may be missing.
2. On the PC, copy the whole folder(s) from the stick, e.g.:

   ```sh
   mkdir -p ~/ps2-hw-results/$(date +%F)-session1
   cp -r /run/media/$USER/STICK/VVVVVV ~/ps2-hw-results/$(date +%F)-session1/
   ```

**Files that matter:**

| Folder | Files |
|---|---|
| `VVVVVV/` | `fs_smoke.log`, `vvvvvv_ps2.log`, `bench_results.txt`, `bench_*.csv`, `prof_*.txt`, `savedata/` |
| `VVVVVV-P31DEV/` | `fs_smoke.log`, `vvvvvv_ps2*.log`, `bench_results_{normal,flip,flipoff}.txt`, `bench_*_*.csv`, `prof_*_*.txt`, `savedata/` |
| `GSPOC/` | `gs_poc_sw.log`, `gs_poc_gs.log`, `gs_poc_gs_*.ppm` |

**Photos:**
- every `fs_smoke` result screen;
- any FATAL ERROR or CPU EXCEPTION screen (whole and legible);
- the overlay in a normal room, in the Tower, and in Flip Mode;
- the picture edges (overscan).

**Do not share** `data-ps2.zip` or the package folders: they contain your game data. Logs, results and photos are fine.

## 6. Expected results (PCSX2) and what counts as a pass

Expected values for the `preset=hardware` scenes, from PCSX2 (TPS; full speed 29.4):

| Scene | A: H1-RC2 tps / tps_min | B-normal | B-flip | B-flipoff |
|---|---|---|---|---|
| intro_dialogue | 29.4 / 29.3 | 29.4 / 29.4 | **29.4 / 29.3** | 29.4 / 29.4 |
| room_ss1 | 29.2 / 29.2 | 29.3 / 29.0 | **29.3 / 29.1** | 28.8 / 28.6 |
| tower | 25.0 / 24.9 | 25.0 / 24.9 | **22.8 / 22.7** | 17.7 / 17.4 |
| tower_climb | 29.0 / 28.4 | 29.0 / 28.2 | **28.2 / 26.8** | 20.8 / 20.0 |
| panic_room | 25.1 / 24.7 | 25.1 / 24.9 | **22.8 / 22.4** | 17.3 / 17.0 |
| super_gravitron | 29.3 / 29.0 | 29.2 / 29.1 | **29.3 / 29.1** | 17.0 / 11.5 |

Sources: package A from the H1.1 validation run (`logs/pcsx2/rc-bench-hw`); package B from PCSX2 runs of the packaged `VVVVVV-P31DEV` folder with the three tagged configurations (`build/p32-pkgB`). Render mean in Flip Mode (ms): super_gravitron 24.7 (on) vs 57.2 (off), tower 27.5 vs 40.2. Underruns 0 in all runs; memory 17.2 MiB, 17.5 MiB with the flip fast path.

**Session 1 passes when:**
- `fs_smoke` reports ALL OK;
- the game boots;
- controls, music, save/continue work;
- the benchmark completes.

Any TPS is acceptable for the baseline: the point is to measure it.

**Session 2 passes when:**
- B-normal ≈ package A within a few percent;
- B-flip ≥ B-flipoff in every scene;
- no new artefacts in Flip Mode;
- music underruns stay 0;
- save/continue works.

## 7. Troubleshooting

| Symptom | Check |
|---|---|
| `mass:/` empty or missing | Stick FAT32 + MBR? Front port, inserted before power-on? Try another (smaller/older) stick |
| `fs_smoke` PROBLEMS | Photograph the failed checks. CRC failures mean a bad copy: re-copy and re-run `sha256sum -c MANIFEST.sha256` |
| Black screen after launch | Wait 30 s; note whether wLaunchELF itself was visible; NTSC 60 Hz needs a TV/cable that accepts it |
| Start-up list stops | Note the last line; that is the step that hangs |
| FATAL ERROR / CPU EXCEPTION | Photograph it whole; note the build ID on it; reset |
| Wrong build ID on screen | Wrong folder; start again from the right one |
| BENCH DONE never appears | Note how long you waited and the last overlay values; reset; bring back the log |
| Benchmark ran on a normal launch | `bench.txt` was left in the folder; delete it |
| Flip Mode not in the menu | Unlock it: options → accessibility → unlock play modes → unlock flip mode (package B only). The benchmark's `flip=1` works without it |

## 8. Criteria for making P3.1 the next release candidate

All of these must hold, on the console:
1. **Session 1 complete.** RC2 baseline recorded, nothing in it pointing at P3.1's code.
2. **Normal mode unchanged.** B-normal within run-to-run noise of package A (TPS ±0.2, render mean ±5%) in every preset scene. The golden-frame CRC lines in `vvvvvv_ps2_normal.log` equal those in package A's `vvvvvv_ps2.log`.
3. **Flip Mode faster.**
   - B-flip ≥ B-flipoff TPS in every scene;
   - clearly faster where PCSX2 showed the problem (super_gravitron, tower, panic_room, tower_climb);
   - the B-flip and B-flipoff golden CRCs identical to each other.
4. **Path and state checks.**
   - The `flipped copies` lines in `vvvvvv_ps2_flip.log` show `fallback=0` and `scratch allocs=0` during every measured scene.
   - `vvvvvv_ps2_flipoff.log` shows `fast=0`.
   - `VERIFY` lines report 0 differing pixels.
5. **Stability.**
   - No music underruns in any run;
   - no FATAL ERROR or CPU EXCEPTION;
   - save/continue works in package B;
   - package A's save is untouched.
6. **Visual check.** Flip Mode looks right on the TV: text, final frame and screen shake.

If all hold:
1. Rebuild P3.1 from a committed revision with a new build ID (e.g. `H1-RC3`).
2. Freeze it with `scripts/freeze-release.sh`.
3. Repeat a short session on it.
