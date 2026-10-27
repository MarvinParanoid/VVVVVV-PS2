VVVVVV for PlayStation 2 - unofficial build (tested on one PS2 model so far)
============================================================================

VVVVVV is copyright Terry Cavanagh. This is an altered, unofficial version
based on the VVVVVV source code (github.com/TerryCavanagh/VVVVVV), not made
or endorsed by its authors. VVVVVV is a commercial game; if you enjoy it,
please buy it at thelettervsixtim.es.

This folder was prepared from your own data.zip (tools/prepare-ps2-usb.py or
scripts/make-usb-package.sh).
The game data in it is yours; do not redistribute it.

Contents (copy the whole VVVVVV folder to the ROOT of a FAT32 USB stick)
  mass:/VVVVVV/
    fs_smoke.elf          storage self-test. Run this FIRST.
    VVVVVV.elf            the game, music streamed from data-ps2.zip
    VVVVVV-nomusic.elf    the game without music (if VVVVVV.elf has audio trouble)
    data-ps2.zip          your data.zip with the music stored uncompressed
    lang/  fonts/         text and fonts
    fs_smoke_expect.txt   checksums fs_smoke.elf compares against
    MANIFEST.sha256       checksums of everything (sha256sum -c MANIFEST.sha256 on a PC)
    README-PS2.txt        this file
    SOURCE.txt            build ID and exact source versions
    optional/bench.txt    copy next to VVVVVV.elf to run the benchmark at start
    optional/overlay.txt  copy next to VVVVVV.elf to start with the overlay on

1. Storage self-test
  wLaunchELF -> FileBrowser -> mass:/ -> VVVVVV/ -> fs_smoke.elf
  A text screen lists each check; it ends with ALL OK or PROBLEMS plus the
  names of failed checks. It also writes fs_smoke.log into the folder.
  On the tested console opendir(".") is reported as failed; that one is a
  known issue.
  Photograph the screen, then reset the console.

2. The game
  wLaunchELF -> FileBrowser -> mass:/ -> VVVVVV/ -> VVVVVV.elf
  A white-on-black start-up list appears first (IOP reset, drivers, boot
  device, assets archive, SDL, renderer). It is replaced by the game when
  the renderer starts. If the game cannot start, a FATAL ERROR or CPU
  EXCEPTION screen stays up instead: photograph it, then reset.

3. Diagnostic overlay
  Press L3 + R3 together (both sticks down) to step through four modes:
    OFF
    COMPACT  one line: TPS (ticks per second), PCM underruns, free HEAP
    PERF     TPS and 10 s average/minimum, FRAME and RENDER ms, MUSIC
             (decode ms per second, PCM UR underruns), HEAP FREE
    DIAG     FLIP MODE, STORAGE (log writes ok/fail), AUDSRV Q (audio
             queue), SUBMIT (late audio), DRY EST, LAST ERR
  and back to OFF. Values update once a second. TPS 29.4 is full game
  speed. For a report, photograph the PERF and DIAG pages.

4. Benchmark (no input needed, about 4 minutes)
  Copy optional/bench.txt into the VVVVVV folder and start VVVVVV.elf.
  It plays six fixed scenes on its own (dialogue, a room, the Tower twice,
  Panic Room, Super Gravitron). When the overlay appears with "BENCH DONE",
  reset the console. Results: bench_results.txt (one line per scene) and
  vvvvvv_ps2.log. Delete bench.txt afterwards to play normally.

Saves
  Everything is written to the stick, in this folder: savedata/saves/
  (settings.vvv, unlock.vvv, qsave.vvv/tsave.vvv), vvvvvv_ps2.log and the
  benchmark files. Nothing is written to the memory card. The folder must
  be writable, or the game stops at start-up with an explanation.

Display
  Always NTSC 640x448 at 60 Hz, also on a European (PAL) console. Use RGB
  SCART, component or a TV that accepts 60 Hz; over composite on a
  PAL-only TV the picture may be black and white or roll.

Controls
  Left stick / d-pad move, Cross = flip/confirm, Circle = back/pause menu,
  Triangle = map, Square = interact, R1 = restart, L3+R3 = overlay.

What to send back (copy from the stick on a PC)
  fs_smoke.log, vvvvvv_ps2.log, bench_results.txt (if you ran it); if saves
  misbehave, the names and sizes of the files in savedata/saves/ (send the
  save files themselves only if a developer asks and you agree); photos of
  any error screen; console model, wLaunchELF
  version, USB stick model and size. The log is written about every 10 s,
  so the last seconds before a crash may be missing.
