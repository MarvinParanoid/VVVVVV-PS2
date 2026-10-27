# Releasing

How release candidates are built, checked and tested; what CI does; and what must be true before a public release.

Building an archive does **not** publish it. **No GitHub Release has been published.** Executables have been exposed, though: three CI runs (`1711c68`, `fb61092`, `742a7ce`) uploaded ELFs as GitHub Actions artifacts of the public repository, downloadable by signed-in users until they expire (2026-11-22/25); see [license-compliance-matrix.md](license-compliance-matrix.md). Publication is a separate, manual decision, gated by [the release gates](#release-gates) and the open items in [license-compliance-matrix.md](license-compliance-matrix.md).

## Status (2026-10-27)

| | |
|---|---|
| Hardware-playable baseline | local tag `H3-FIRST-HARDWARE-PLAYABLE` → `b1e087c` (build `P3.4.3-SAVEPAD`); rebuilds bit-identically |
| Current candidate | `H4-RC1`, commit `a435397`, `build/public/VVVVVV-PS2-H4-RC1.zip` (local), SHA-256 `15c94a35735a66081afa948900b9ec69441157b4cbdab97dfe28895da69d94ce`; unstripped `VVVVVV.elf` `3fac5284…`, stripped `d86d940d…` |
| Console check of `H4-RC1` | session s7, partly done (see [the checklist](#hardware-check-of-a-release-candidate)) |
| CI verification of the release mode (H4.3) | **pending**: the release-mode workflow and `a435397` have not run on GitHub; draft notes in `docs/h4-ci-verification-report.md` (not final) |
| Licensing | **blocked** on [fjtrujy/ps2_drivers#27](https://github.com/fjtrujy/ps2_drivers/issues/27) |

## What the archive contains

```
VVVVVV-PS2-<name>/
  README.txt                 end-user guide (docs/release/README.txt)
  BUILD-INFO.txt             build ID, commit, toolchain, ELF checksums, hardware-validation note
  SHA256SUMS                 every other file
  LICENSE-VVVVVV.md          VVVVVV Source Code License
  THIRD-PARTY-NOTICES.txt    notices of everything linked into the ELFs, incl. open items
  LICENSES/                  LGPL-2.0, LGPL-2.1, pthread-embedded COPYING, FatFs, newlib, zlib, MIT
  tools/                     prepare-ps2-usb.py, repack-data-zip.py
  usb/VVVVVV/                VVVVVV.elf, VVVVVV-nomusic.elf, fs_smoke.elf (stripped),
                             SOURCE.txt, README-PS2.txt, lang/, fonts/,
                             optional/overlay.txt, optional/bench.txt
```

There is no game data, no frozen or experimental build, no prototype, no test switch (`crashtest.txt`, `iodiag.txt` and the like), no log and no benchmark output. `scripts/check-package.py zip --release` enforces this with an allowlist. Users add their own `data.zip` with `tools/prepare-ps2-usb.py` (see the archive's `README.txt`).

The unstripped ELFs and `SOURCE.txt` go to `build/public/VVVVVV-PS2-<name>-symbols/<name>/`, for symbolising crash reports. They are not part of the archive.

## Rules the script enforces

`scripts/make-public-test-release.sh --release <name> [--build-root DIR]` stops unless all of these hold:

- `DIR/out/vvvvvv-ps2/SOURCE.txt` says `build <name>`. The ELFs were built with `PS2_BUILD_ID=<name>`, so an older build is never picked up.
- `SOURCE.txt` has no `WARNING` (stale tree) and no `-dirty` (uncommitted changes at build time).
- The revision in `SOURCE.txt` is the commit `DIR` has checked out now, and `DIR` has no uncommitted change to tracked files.
- `VVVVVV.elf` and `VVVVVV-nomusic.elf` have exactly the SHA-256 that `build-vvvvvv-ps2.sh` recorded in `SOURCE.txt` (`elf <sha256>  <file>` lines), and both contain the build ID `<name>`. A replaced or rebuilt ELF stops the script.
- `DIR/build/ps2-tests-opt/fs_smoke.elf` has the SHA-256 recorded in `BUILD-STAMP.txt` there (written by `build-ps2-tests.sh`), and that stamp names the same clean commit.

`DIR` defaults to the repository itself and must be inside it, because the build container only sees the repository. `BUILD-INFO.txt` also names the commit of the packaging scripts. If those had uncommitted changes, it says so ("NOT for publication").

**What this guarantees, and what it does not.** The archive contains the files that the build scripts produced and recorded in `DIR` at one clean commit, named by `BUILD-INFO.txt`. Nothing was swapped between building and packaging, and no older build was reused.

The check cannot detect someone deliberately editing both a file and its record. It also does not prove that the commit's source produced the ELF. That proof is a rebuild from the commit with a matching checksum: a clean checkout, or the CI run of the same commit.

For the H4 candidates, such a rebuild comparison is part of the gates. `THIRD-PARTY-NOTICES.txt` separates the licence notices that are included (part A) from compliance items that are still open (part B).

## Reproducing locally

From a clean commit, either in the repository itself or in a separate worktree. The worktree lets you keep unrelated uncommitted files in the main tree:

```sh
git worktree add --detach build/rc/<name>-src <commit>
cd build/rc/<name>-src
scripts/build-sdl2-ps2.sh
PS2_BUILD_ID=<name> scripts/build-vvvvvv-ps2.sh --refetch
scripts/build-ps2-tests.sh
cd -
scripts/make-public-test-release.sh --release <name> --build-root build/rc/<name>-src
```

Output:
- `build/public/VVVVVV-PS2-<name>.zip` and its `.sha256`;
- the unpacked tree `build/public/VVVVVV-PS2-<name>/`;
- the symbols folder.

The zip is deterministic: sorted entries, fixed timestamps and modes. Two runs from the same build give the same SHA-256.

Checks after packaging:
1. `python3 -I scripts/check-package.py zip build/public/VVVVVV-PS2-<name>.zip --build <name> --commit <sha> --release`. The script runs this itself.
2. Follow the archive's `README.txt` on a PC: unzip, `sha256sum -c SHA256SUMS`, `tools/prepare-ps2-usb.py <data.zip> usb/VVVVVV` (must print `ready:`).
3. Copy `usb/VVVVVV` to a FAT32 USB stick. Then `sha256sum -c MANIFEST.sha256` inside the copied folder, after remounting the stick.
4. PCSX2 boot of the prepared folder: `scripts/run-package-pcsx2.sh <folder> VVVVVV.elf <tag> 45`. The log must reach `MILESTONE first frame` with no `WARN`/`ERROR` line.
5. On a console: [the checklist below](#hardware-check-of-a-release-candidate).

## Continuous integration

The workflow is [`.github/workflows/ci.yml`](../.github/workflows/ci.yml). Two jobs run on `ubuntu-24.04`. The token is read-only (`permissions: contents: read`), no secrets are used, actions are pinned by commit, and no step creates a GitHub release.

**Triggers:**
- pull requests (the head commit is built);
- pushes to `main`;
- tags `H*-RC*`;
- manual runs (**Actions → CI → Run workflow**), with an optional `release` name.

A manual run builds the head of the selected branch. There is no input for an arbitrary commit.

| Job | What it does |
|---|---|
| Host reference tests | fetches VVVVVV, builds SDL 2.32.10 (stock and patched) and the host tests, runs them. This includes the renderer checks and `fs_semantics`. Uploads `host-test-logs` |
| PS2 build | Docker backend with the pinned ps2dev image. Builds SDL, VVVVVV and the PS2 test programs, then packages: **release mode** (`--release <tag or name>`) for a tag or a manual run with a name; otherwise the CI test archive (`--ci`, build ID `ci-<first 12 hex of the commit>`). Writes `BUILD-INFO.txt` into the job summary |

**Artifacts:**
- `metadata-<build>`: `BUILD-INFO.txt`, `SOURCE.txt`, the zip's `.sha256`. It contains every ELF and archive checksum, so a CI build can be compared without downloading executables.
- `VVVVVV-PS2-<build>.zip` (the archive) and `symbols-<build>` (unstripped ELFs): 30 days.

The repository is public, so artifacts can be downloaded by any signed-in GitHub user. In the working tree (uncommitted, under review in H4.3), these two executable uploads only run when the repository variable `UPLOAD_EXECUTABLES` is `true`. Runs before that change uploaded them (see `docs/h4-ci-verification-report.md`).

**Checked automatically:**
- patches apply to the pinned sources;
- everything compiles;
- no stale or dirty tree is packaged;
- host renderer checks and `fs_semantics` pass;
- the archive passes `check-package.py`: no game data, BIOS, secrets, local paths or caches; valid ELFs; `SHA256SUMS`; build ID and commit; and the release allowlist in release mode.

**Not checked by CI:**
- anything that needs `data.zip` (gameplay, music, golden frames);
- anything that needs a BIOS (PCSX2 runs);
- anything that needs a console (USB/FatFs behaviour, timing, controllers);
- bit-identity with a local build, which is compared by hand: [building.md](building.md#reproducibility).

**Runs so far:** `1711c68`, `fb61092` and `742a7ce` (normal CI, all successful; 742a7ce includes `fs_semantics`). The release mode has not run on GitHub yet.

A CI release-candidate build is not "hardware-tested" until a console session with that exact archive is recorded.

## Hardware check of a release candidate

Use the release archive and a FAT32 (MBR) stick, prepared with `tools/prepare-ps2-usb.py` and the tester's own `data.zip`.

| # | Check | `H4-RC1`, session s7 (2026-10-27) |
|---|---|---|
| 1 | `fs_smoke.elf` ends with ALL OK | pending: not run from the `H4-RC1` folder. The tool itself ran on the console in s1 (`H1-RC2` build), where every check passed except `opendir(".")` ([known-issues.md](known-issues.md#technical-debt)) |
| 2 | `VVVVVV.elf`: boot console, no leftover picture, title in ≤ 10 s | **pass**: first frame 7.95 s (log); no picture (tester) |
| 3 | no WARN/ERROR; overlay `LAST ERR` empty | **pass** by log; no overlay photo |
| 4 | ≥ 30 min of play across areas; TPS ≥ 29.0; `PCM UR 0`, `late 0`, `DRY EST 0` | pending (s7 log covers ~30 s: 34.3 ms period, 0 underruns, 0 late) |
| 5 | D-pad and left stick; unplug the pad for 5 s, replug, stick still works | pending (stick worked in s6; replug never tested) |
| 6 | save, change a setting, **full power-off**, Continue restores the room, setting kept | **pass**: saves and room restored (log), power-off (tester), language setting kept |
| 7 | `VVVVVV-nomusic.elf` plays for 2 min | pending |
| 8 | keep `vvvvvv_ps2.log`, `savedata/` sizes, photos of the PERF and DIAG pages | partly: log and save sizes kept locally; no photos |

The console session logs are kept locally (`logs/hw/`, not in git). The s1–s7 history is summarised in [archive/h4-first-public-release-plan.md](archive/h4-first-public-release-plan.md).

## Release gates

| Gate | Criterion | `H4-RC1` |
|---|---|---|
| Source | baseline tag rebuilds its hashes; candidate built from a clean commit | pass |
| Clean build | clean checkout → candidate ELFs; no WARNING or `-dirty` | pass (local) |
| File system | no hang on missing files; no WARN/ERROR on a normal start | pass (s7) |
| Saves | write, reload after power-off, setting kept | pass (s7) |
| Controller | D-pad, buttons, left stick; replug | partly (replug pending) |
| Stability | ≥ 30 min without exception or hang | pending |
| Audio | 0 underruns, 0 late in that session | pass for a short window; long session pending |
| Performance | TPS ≥ 29.0 (10 s minimum) in normal rooms | pass (s6 overlay; s7 log 34.3 ms period) |
| Packaging | asset-free, single folder, deterministic, checks pass, user path works | pass (local) |
| CI | green on the candidate commit; CI and local ELF hashes compared | pending (H4.3) |
| Licensing | ps2_drivers resolved; publication decision recorded | **blocked** |
| Documentation | no false hardware claims; one-console scope stated | pass (this revision) |

## Earlier candidates

- **`H1-RC2`** (frozen with `scripts/freeze-release.sh` in the maintainer's local `release/`) and the **`H2-TEST1`** tester archive were validated in PCSX2 only. Both **hang on a real PS2**. They are kept as records and must not be distributed.
- `freeze-release.sh` still works for read-only local snapshots; it now copies [installing.md](installing.md) into the snapshot.
- The legacy tester mode of `make-public-test-release.sh` (no flag) packages `H1-RC2`; see [known-issues.md](known-issues.md#technical-debt).

## Publishing (not implemented)

There is deliberately no publishing workflow. When one is added, its design (from the CI design notes) is:
- **trigger:** manual dispatch only, with a required reviewer;
- **assets:** downloaded from a validated CI run of the exact commit and re-checked with `check-package.py`, never rebuilt;
- **release type:** a draft **pre-release**;
- **permissions:** write permission only for that job;
- **content:** no data-bearing package.
