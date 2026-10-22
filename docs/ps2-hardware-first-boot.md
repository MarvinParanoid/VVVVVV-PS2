# First PS2 boot: checklist (SCPH-7500x, release candidate H1-RC2)

This is a short, printable checklist. Background and troubleshooting are in [ps2-h1-hardware-readiness.md](ps2-h1-hardware-readiness.md).

**Nothing here has been tested on a real console yet.**

## 0. On the PC, before the session

- [ ] Make the package from the frozen release candidate (nothing is rebuilt):

  ```sh
  scripts/make-usb-package.sh ~/path/to/data.zip --from-release release/VVVVVV-PS2-H1-RC2
  ```

  Without a frozen release folder (e.g. a fresh clone), `scripts/make-usb-package.sh ~/path/to/data.zip` builds everything and packages it the same way (build ID `dev` unless `PS2_BUILD_ID` is set).

  It must end with `checksums OK` and no `FAT32 problem` lines.
- [ ] Use a FAT32 stick with an MBR partition table. Smaller and older sticks are safest for the PS2's USB 1.1 port.
- [ ] Copy `build/usb-package/VVVVVV/` to the stick root, so you have `/VVVVVV/VVVVVV.elf`.
- [ ] On the stick, in `VVVVVV/`, run `sha256sum -c MANIFEST.sha256`. Every line must say OK. Eject safely.
- [ ] Note: stick model and size, wLaunchELF version, TV and cable (RGB SCART / component / composite).

## 1. Console

- [ ] FMCB memory card in slot 1; DualShock 2 in port 1; the stick in a **front** USB port **before** power-on.
- [ ] Nothing is written to the memory card. Saves and logs go to the stick, in `VVVVVV/savedata/` and next to the ELF.
- [ ] Boot into wLaunchELF and check that its menu is visible on this TV and cable.
- [ ] The game always outputs **NTSC 60 Hz**. If the picture is black and white or rolls, change the cable or TV.

## 2. Storage self-test: `mass:/VVVVVV/fs_smoke.elf`

- [ ] The text screen ends with **ALL OK**. If it ends with PROBLEMS, it lists the failed checks. Photograph it.
- [ ] Note the `device ready after … ms`, `sequential … KB/s` and `random … worst … ms` values.
- [ ] Reset the console.

## 3. Game: `mass:/VVVVVV/VVVVVV.elf`

- [ ] A white start-up list appears. Its first line ends in **H1-RC2**. Then: IOP reset → drivers → boot device ready → assets archive → SDL started → starting the renderer.
- [ ] The title screen follows. Note how many seconds it took from launch.
- [ ] Press ✕: the menu music starts within about 0.5 s. Listen for clicks or gaps.
- [ ] Press **L3+R3** (both sticks): the overlay appears. Note TPS / FRAME / RND / FREE. Press L3+R3 again to hide it.
- [ ] Start a new game and skip the dialogue with ✕. Walk a few rooms: smooth, with music and sound effects?
  - Overlay check in a normal room: **TPS ≈ 29.4** and **UNDERRUN 0** expected.
- [ ] Open the map with △, go to the SAVE tab (→ three times) and press ✕ to save. Check on the PC later that `VVVVVV/savedata/saves/qsave.vvv` exists.
- [ ] Reset, launch again, then play → continue: you are back in the saved room.
- [ ] Optional: try `VVVVVV-nomusic.elf` if audio misbehaves.

## 4. Benchmark (about 4 min, hands off)

- [ ] Copy `optional/bench.txt` into `VVVVVV/` (on the PC, or with wLaunchELF's copy/paste).
- [ ] Launch `VVVVVV.elf` and touch nothing. Six scenes play by themselves.
- [ ] Wait for the overlay to show **BENCH DONE**, then reset.
- [ ] Remove `bench.txt` afterwards, otherwise the benchmark runs on every launch.

## 5. If something fails

- [ ] **FATAL ERROR or CPU EXCEPTION screen:** photograph the whole screen. It stays up; nothing else is needed from the console.
- [ ] **Black screen:** note how long you waited and whether wLaunchELF itself was visible.
- [ ] **Frozen start-up list:** note the last line shown.
- [ ] **Bring back** from `VVVVVV/` on the stick:
  - `fs_smoke.log`;
  - `vvvvvv_ps2.log`;
  - `bench_results.txt` (and `bench_*.csv`);
  - `savedata/`.
- [ ] **Write down:**
  - console model;
  - TV and cable;
  - wLaunchELF version;
  - stick;
  - what you did and what you saw or heard;
  - timings.
