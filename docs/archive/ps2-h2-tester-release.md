# H2: public tester release H2-TEST1

**Date:** 2026-10-23

**Status:** ready to hand to volunteer testers; not published. **No physical PS2 has run any of this.**

## Contents

`scripts/make-public-test-release.sh H2-TEST1` builds `build/public/VVVVVV-PS2-H2-TEST1.zip` (57 files, 8.2 MiB). It contains **no game data**.

| Path | Content |
|---|---|
| `usb/VVVVVV/` | H1-RC2: the frozen binaries from `release/VVVVVV-PS2-H1-RC2/bin`, unchanged |
| `usb/VVVVVV-P31DEV/` | Experimental build `P3.2-T1` (P3.1 Flip Mode fix + P3.2 hardening), with three tagged benchmark configurations |
| `usb/GSPOC/` | Optional G1 prototypes |
| `tools/prepare-ps2-usb.py`, `tools/repack-data-zip.py` | Complete the folders with the tester's own `data.zip` (Python 3.8+, standard library only) |
| `README.txt`, `TESTING.txt` | Source: `docs/tester/` |
| `LICENSE-VVVVVV.md`, `THIRD-PARTY-NOTICES.txt`, `BUILD-INFO.txt`, `SHA256SUMS` | Licences, build identity, checksums |

The developer's full stick comes from `scripts/make-hw-test-stick.sh <data.zip>` (147 MB in `build/usb-hw/`). Both use the same layout code: `make-usb-package.sh`, with `--no-data` for the public zip.

## Verification

**Automated (no emulator needed):**
- **The public zip contains no game data.** The script refuses `*.zip`, `*.vvv`, audio, `*.bin`, `*.ppm`, PNGs outside `fonts/`, and any file over 8 MiB. Its file list was also inspected by hand.
- **The zip is deterministic:** a rebuild is byte-identical.
- **The tester route reproduces the developer's stick exactly.** Unzipping, then `prepare-ps2-usb.py` with `data.zip`, gives folders byte-identical to `build/usb-hw/` (A and B). The input `data.zip` is unchanged.
- **The tool gives clear messages** for a missing file, a file that is not a zip, an incomplete `data.zip`, and a wrong folder.
- **Clean clone of the repository (committed files only):** the game ELFs, `fs_smoke`, `gs_poc_*` and `flip_exact` are bit-identical to the working tree.
- **H1-RC2 reproduced from git.** Exporting commit `25c493c` (the publication commit, with RC2's patches) and building gives `symbols/VVVVVV.elf` and `VVVVVV-nomusic.elf` bit for bit. `release/VVVVVV-PS2-H1-RC2/SHA256SUMS` passes.
- **Host tests:** `flip_exact` 36/36, `sw_exact` 6/6.

**In PCSX2, on folders prepared from the public zip** (logs: `logs/pcsx2/h2-*`):
- `flip_exact` 36/36; `sw_exact` 6/6.
- `fs_smoke` reports ALL OK in both folders.
- Save → reset → continue returns to the saved room in both builds.
- The benchmarks (`preset=hardware`) completed and wrote separate files:
  - `bench_results.txt` (RC2);
  - `bench_results_{normal,flip,flipoff}.txt` (`P3.2-T1`).
- Golden frames are identical between RC2 and `P3.2-T1` normal (6/6), and between Flip Mode with and without the fast path (6/6).
- Path counters: Flip Mode ran only the fast path (fallback 0, scratch allocs 0 per scene); the `flipoff` run used only the previous code.
- 0 music underruns everywhere.

| Scene | RC2 TPS | P3.2-T1 normal | P3.2-T1 Flip | P3.2-T1 Flip, fast path off |
|---|---|---|---|---|
| intro_dialogue | 29.4 | 29.4 | 29.4 | 29.4 |
| room_ss1 | 29.3 | 29.3 | 29.2 | 28.9 |
| tower | 25.0 | 25.0 | 22.8 | 17.6 |
| tower_climb | 29.0 | 28.9 | 28.2 | 20.7 |
| panic_room | 25.1 | 25.1 | 22.8 | 17.3 |
| super_gravitron | 29.3 | 29.3 | 29.2 | 17.0 |

## Fixes found while preparing H2

1. **`SOURCE.txt` listed stale VVVVVV patch hashes** when the build tree had not been re-fetched (all P3.1/P3.2 development builds). Patch hashes are now computed at build time, and a stale tree or SDL library is flagged and refused by the packaging scripts. H1-RC2 was built with `--refetch` and is not affected.
2. **The game ELF embedded this repository's git commit.** VVVVVV's `version.cmake` found our repository once it became a git repository, so every commit changed the binary. The build now stops git at the source tree, which reproduces RC2's "(commit?)" behaviour.
3. **Packaging steps existed only as bash and locale-dependent sorting.** They are now one Python tool used by both the developer and testers. The output is byte-identical except for the manifest line order, which is now bytewise instead of following the locale.

## Licensing (see LICENSE.md)

- Binaries only. No game data.
- The VVVVVV licence and attribution are included, and so are third-party notices.
- **No permission or licence exception from Terry Cavanagh is relied on.** The informal discussion about non-commercial ports is not an enacted exception.
- **Review before publishing:** the VVVVVV README's note that compiled distributions "might need permission", and the linked-only notices for newlib, the GCC runtime and FatFs.
