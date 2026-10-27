# Known issues and limitations

## Compatibility

The real-PS2 column comes from **one console**:
- PlayStation 2 Slim SCPH-7500x (EU), Free McBoot;
- one 8 GB USB stick (MBR, FAT32, 32 KB clusters);
- an official DualShock 2;
- component video.

It applies to builds from tag `H3-FIRST-HARDWARE-PLAYABLE` (build `P3.4.3-SAVEPAD`) on. The last checked build is release candidate `H4-RC1` (commit `a435397`), on 2026-10-27, session s7.

**Older builds, including `H1-RC2` and `H2-TEST1`, hang on a real PS2** at the first missing file. Other models and setups are untested.

| Area | PCSX2 v2.8.2 | Real PS2 (SCPH-7500x, 1 console) |
|---|---|---|
| Boot from a USB stick folder, title screen | ✅ tested | ✅ first frame ~8 s after boot |
| No warnings or errors in a normal start | ✅ | ✅ `H4-RC1` (log) |
| Gameplay with DualShock 2 | ✅ tested (emulated pad) | ✅ D-pad, buttons and left analog stick |
| Streamed music + sound effects | ✅ 0 underruns | ✅ 0 underruns, no late audio submissions |
| Save / continue (to the USB stick) | ✅ tested | ✅ saves written and restored, also after a full power-off |
| Full game speed in normal rooms (29.4 ticks/s) | ✅ measured | ✅ 29.5 ticks/s, 10 s minimum 29.0 |
| Tower / Panic Room worst case | ⚠️ ~25 of 29.4 ticks/s (85%) | ❓ unmeasured |
| Flip Mode (upside-down option) | ✅ full speed in most scenes | ❓ unmeasured |
| Overlay, exception screen | ✅ tested | ✅ overlay; the exception screen appeared once (in a build before the fixes) |
| `fs_smoke.elf` USB self-test | ✅ ALL OK | ⚠️ the tool ran in s1 (the `H1-RC2` folder's build): device, reads, CRCs, write and delete OK, 608 KB/s; it still ends with PROBLEMS because `opendir(".")` fails on the console. Not yet run from the `H4-RC1` folder |
| NTSC video output on a PAL console | — | ✅ over component video on one TV |
| No leftover picture while loading | — (VRAM starts empty in PCSX2) | ✅ `H4-RC1` |
| Play sessions over 30 minutes | ✅ 18-minute soaks | ❓ untested (sessions so far: a few minutes each) |
| Controller unplugged and replugged | — | ❓ untested |
| `VVVVVV-nomusic.elf` | ✅ | ❓ untested |
| Full or write-protected stick, power loss during a save | — | ❓ untested |
| Other consoles (fat models, other regions), other sticks, HDD/OPL/disc launch, multitap, third-party pads | — | ❓ untested |

PCSX2 does not model the PS2's CPU caches or memory latency, so PCSX2 numbers are not hardware numbers.

## Limitations

- **NTSC only:** always 640×448 at 60 Hz, also on PAL consoles. Use a TV and cable that accept 60 Hz (RGB SCART, component). Over composite on a PAL-only TV the picture may be black and white or roll. There is no PAL mode.
- **Saves go to the USB stick only,** in `savedata/` next to the game. No memory-card saves. The game folder must be writable.
- **USB mass storage only.** HDD, network and disc launches have not been tried.
- **English only.** The CJK font sheets do not fit the GS texture limit. On the very first start the game still shows its language menu with English as the only entry: press Cross. The choice is saved.
- **Slowdown in the worst scenes.** The Tower and the Panic Room run at about 85% speed in PCSX2 when the camera chases the player. Not yet measured on the console.
- **Music starts about 0.5 s** after a track change.
- `over30mode` (interpolated 30+ FPS) is off by default. It can be enabled in the options but costs speed.
- **Saving is not atomic.** A full stick or a power loss during a save can leave that save empty or truncated. The game then logs an error and starts that file fresh. A USB error during reading or writing looks like a short file to the game, not like an error (ps2sdk behaviour, see [architecture.md](architecture.md#files-saves-and-the-ps2sdk-seams)).
- **The D-pad feels abrupt for very small steps.** Reported once on the console. The game's logic runs at its normal 34 ms tick; no input lag was found. Not investigated further.
- Only one `data.zip` (the full game's) has been tested.

## Not a public release yet

`H4-RC1` is a release candidate, not a published release. Publication is blocked until these items are settled:
- **Licensing:** the licence of `ps2_drivers`, linked into the ELF through SDL (issue [fjtrujy/ps2_drivers#27](https://github.com/fjtrujy/ps2_drivers/issues/27)), and the open items in [license-compliance-matrix.md](license-compliance-matrix.md).
- **Remaining checks:** the CI and hardware checks listed in [releasing.md](releasing.md#release-gates).

## Technical debt

- **Legacy tester mode.** `scripts/make-public-test-release.sh` without `--ci`/`--release` still builds the old H2-style tester archive: frozen `H1-RC2`, which hangs on a console, plus an experimental build and the G1 prototypes. Its texts are `docs/tester/README.txt`, `docs/tester/TESTING.txt` and `tests/ps2/gs_poc/README-GSPOC.txt`. Do not use it. It should be removed in a separate cleanup.
- **Build tools not pinned.** The host build tools inside the toolchain container (`apk add cmake make …`) are not version-pinned; `BUILD-INFO.txt` records them.
- **`fs_smoke` reports `opendir(".")` as failed on the console.** Listing the current directory by a relative path fails on the tested PS2 (s1), so the self-test ends with PROBLEMS although every read, CRC and write check passes. The game played normally on the same stick. The check is unchanged in `H4-RC1`; it should be fixed or explained in the tool before the `fs_smoke` gate can pass.
- **ps2sdk revision.** The exact ps2sdk revision in the toolchain image is not recorded. The nearest upstream commit at the image date is `329e2bb`.
