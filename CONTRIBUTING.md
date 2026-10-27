# Contributing

Thanks for your interest! The most valuable contribution right now is a
**test on a real PlayStation 2**. The port has been tested on one console
model only (see [docs/known-issues.md](docs/known-issues.md)), so reports from
other models, regions and USB sticks help most.

## Hardware test reports

Use the **Hardware test report** issue template. A useful report contains:
- console model (sticker on the bottom, e.g. SCPH-75004) and region;
- how you launched it (FMCB / FreeHDBoot / PS2BBL, wLaunchELF version, etc.);
- USB stick model and size, and the port you used;
- TV, cable and video mode;
- the build ID shown on the start-up screen (e.g. `H4-RC1`, or `dev` for your own build);
- the `fs_smoke` result screen, and photos of any error screen;
- the files from the game folder on the stick:
  - `fs_smoke.log`;
  - `vvvvvv_ps2.log`;
  - `bench_results.txt` (if you ran the benchmark).

[docs/installing.md](docs/installing.md) walks through a session, and [docs/releasing.md](docs/releasing.md#hardware-check-of-a-release-candidate) lists the full release-candidate check.

**Never attach or upload VVVVVV game data.** That means `data.zip`, `data-ps2.zip`, extracted graphics, sounds or music. Screenshots and photos of the game are fine in issues.

## Code changes

**How changes are stored.** This repository stores changes to upstream projects as **patch series**, not as copies of their source:

| Upstream | Pinned in | Patches |
|---|---|---|
| VVVVVV `2.4-updates` | `scripts/fetch-vvvvvv.sh` | `patches/vvvvvv/*.patch` |
| SDL 2.32.10 | `scripts/build-sdl2-ps2.sh` | `patches/sdl2/*.patch` |

**Workflow:**
1. Build once (see [docs/building.md](docs/building.md)). This creates the patched trees `build/vvvvvv-src/` and `build/sdl2-ps2-opt/src/`.
2. Edit those trees and rebuild with `scripts/build-vvvvvv-ps2.sh` (no `--refetch`, which would discard your edits). For SDL, rebuild the library with the commands inside `scripts/build-sdl2-ps2.sh`, without its re-extract step.
3. Turn your edits back into patches:
   - VVVVVV: `scripts/regen-vvvvvv-patches.sh`
   - SDL: `scripts/regen-sdl2-patches.sh`

   Each file belongs to exactly one patch, and the scripts list the mapping. Add a new file to the right list. Both scripts check that the series reproduces your tree.
4. Verify from scratch:

   ```sh
   scripts/build-sdl2-ps2.sh && scripts/build-vvvvvv-ps2.sh --refetch
   ```

**Ground rules:**
- **Keep PS2 changes isolated.** Guard them with `__PS2__` / `__ps2sdk__` or CMake `if(PS2)`, and do not change game logic.
- **Rendering changes must be bit-exact.** Run the benchmark with golden frames before and after, and compare:

  ```sh
  PCSX2_BIOS=/path/to/bios.bin BENCH="settle=4 measure=20" scripts/run-vvvvvv-pcsx2.sh
  cp logs/pcsx2/VVVVVV/vvvvvv_ps2.log before.log    # then rebuild with your change, run again
  scripts/ps2-bench-compare.py before.log logs/pcsx2/VVVVVV/vvvvvv_ps2.log
  ```

  It must report `golden frames identical`.
- **Label measurements honestly.**
  - Say whether a number comes from PCSX2 or from hardware.
  - PCSX2 does not model the EE caches, so its timings are optimistic.
  - Do not describe anything as working on hardware unless it has been run on a console.
- **No proprietary files in commits:** no game assets, BIOS dumps, built ELFs or emulator screenshots of the game. `.gitignore` covers the usual locations; check `git status` before committing.

## Licensing of contributions

By contributing, you agree that your changes are licensed under the license of the part of the repository you change (see [LICENSE.md](LICENSE.md)):
- **VVVVVV patches:** the VVVVVV Source Code License (non-commercial);
- **SDL and PhysicsFS patches:** zlib;
- **everything else:** MIT.
