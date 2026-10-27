# H4: first public release candidate — audit and plan

Status: plan only (2026-10-26). Nothing in this document has been implemented, committed, tagged or published. Hardware results come from one console (PlayStation 2 Slim SCPH-7500x, EU/PAL, Free McBoot, one 8 GB USB stick, FAT32 with 32 KB clusters) and one tester.

## 1. Executive summary

The port runs on a real PS2. Build `P3.4.3-SAVEPAD` boots from USB and keeps full game speed with clean audio. It writes saves and reloads them after a restart, and the analog stick works. Five bugs blocked hardware play. All five were in the seams between ps2sdk and PhysicsFS/SDL, not in the game:

| # | Symptom on the console | Cause | Fix (uncommitted) |
|---|---|---|---|
| 1 | Hang at the first missing file (start-up, after "SDL started") | ps2sdk reports "file not found" on `mass:` as errno 4, which is `EINTR`; PhysicsFS retried `open()` forever | `patches/vvvvvv/0001`: no EINTR retry on PS2 |
| 2 | Every save file 0 bytes | ps2sdk `fsync()` always fails (`ENOSYS`); `PHYSFS_close` then returned **without closing**, and FatFs writes the file size only at close | `0001`: no fsync on PS2 |
| 3 | `Could not stat file: permission denied` (ERROR); `lang/en` not found | ps2sdk `stat()` of a missing path sets errno 1 (`EPERM`); PhysicsFS stops its search-path walk on any error but "not found" | `0001`: failed stat = not found on PS2 (in `P3.4.4`, not yet on hardware) |
| 4 | Left stick dead | SDL never switched the DualShock 2 to analog mode | `patches/sdl2/0010` |
| 5 | Old OSDSYS/FMCB picture during the ~4 s load | new framebuffer shown before anything is drawn; VRAM not cleared | `0003`: two black frames (in `P3.4.4`, not yet on hardware) |

Recommendation: a public **pre-release** is reachable **after a small cleanup**, not after substantial work. Details are in §14. The remaining blockers are:
- one more false ERROR on hardware (the `mkdir` warning, §4.2);
- packaging that would still ship the hanging H1-RC2;
- user documentation that still says "never tested on a real PS2";
- the licensing decision;
- one full release-candidate session on the console.

## 2. Verified current state

### 2.1 Repository

- Branch `main`, HEAD `fb61092` ("Exception report stack, audsrv diagnostics"). It equals `origin/main` on GitHub (`MarvinParanoid/VVVVVV-PS2`, public). There are no GitHub releases.
- Tag `H2-TEST1` → `bc250a7`. Since then, `fb61092` added:
  - SDL patch `0009` (audsrv stats);
  - the `0003` diagnostics overlay and exception stack;
  - CI (`.github/workflows/ci.yml`, `scripts/check-package.py`).
- CI **has run on GitHub**: runs for `1711c68` and `fb61092`, both "success" (GitHub API, 2026-10-23). `docs/ci.md` still says it has not run. Whether the CI ELFs are bit-identical to local builds has **not** been compared.

Uncommitted work tree:

| Path | Content |
|---|---|
| `patches/vvvvvv/0001-physfs-ps2-platform.patch` | fixes 1–3 (now also covers `physfs_platform_posix.c`) |
| `patches/vvvvvv/0003-ps2-platform-layer-and-hooks.patch` | IODIAG mode (`iodiag.txt`, asset tracing), fix 5, file name in the stat error |
| `patches/sdl2/0010-ps2-pad-analog-mode.patch` (new) | fix 4 |
| `scripts/regen-vvvvvv-patches.sh` | `physfs_platform_posix.c` added to patch 0001 |
| `tests/ps2/CMakeLists.txt`, `tests/ps2/{io_bisect,sdl_bisect,stat_probe,physfs_probe,write_probe}/` | the hardware probes that found fixes 1–3 |

### 2.2 The hardware-playable build `P3.4.3-SAVEPAD`

Source:
- `fb61092` plus three patch files that existed only in the work tree at build time. `0001` and `0003` were then overwritten by the `P3.4.4` changes.
- They were reconstructed exactly. The reconstructed files hash to the values recorded in `build/p343/out/SOURCE.txt` (`0001` `4b18b8d2…`, `0003` `0e874fe3…`, `0010` `79083681…`).
- Copies are kept in `build/p343/patches-reconstructed/` with `SHA256SUMS`. Like all of `build/`, that folder is local and not in git.

Rebuild:
- A fresh `git clone` of this repository, checked out at `fb61092` with those three files, gave **bit-identical** ELFs.
- The check ran on the same host, with the same cached VVVVVV/SDL source tarballs and the same unpacked toolchain image. Reproducibility on another machine has not been tested.

| File | SHA-256 |
|---|---|
| `build/p343/out/VVVVVV.elf` (unstripped) | `66f0568ece3134ea4e7414f80ec80c333c4779e8dd881f7b46b31a5e568c70df` |
| `build/p343/out/VVVVVV-nomusic.elf` (unstripped) | `c58a236cbb00c3e858b38c621867e15b20218b26babddb0091974f921e292976` |
| `VVVVVV-SAVEPAD/VVVVVV.elf` (stripped, the file run on the console) | `c414e47c5802cd9468daac8267cf3f2aabfc39bb8916a91e1b232747cbbd0fb6` |
| `VVVVVV-SAVEPAD/VVVVVV-nomusic.elf` (stripped) | `763ce43d0acca0d3296ddd4dab8d9f5349713328271d709a3841a3237ce4c34e` |

Toolchain:
- ps2dev image `sha256:5476fdfb…`: revision `168ba29`, created 2026-10-13.
- The ps2sdk inside is master of that date. Its exact commit is not recorded in the image; the nearest upstream commit is `329e2bb`.
- GCC 15.2.0. Host tools from `apk` (not pinned): cmake 4.2.3, make 4.4.1, python 3.14.8.
- VVVVVV upstream `79395fa` (2.4-updates); SDL 2.32.10.

The build ID line in `SOURCE.txt` reads `repo H2-TEST1-6-gfb6109296cc8-dirty`. Repository git info is deliberately kept out of the ELF, so the dirty marker does not affect the binary. No linker map is produced for the game.

`P3.4.4-VRAM` adds fixes 3 and 5 (`build/p344/out/VVVVVV.elf` `979f9aea…`). It has only been checked in PCSX2.

### 2.3 Hardware evidence (`logs/hw/2026-10-26-s1` … `s6`)

| Session | Build | Result |
|---|---|---|
| s1 | `fs_smoke`, H1-RC2, P3.4 | `fs_smoke` all reads/CRCs/write OK (608 KB/s sequential); RC2 hangs after "SDL started"; P3.4 exception (NULL surface after a bad read) |
| s2 | P3.4.1-IODIAG | hang at `ASSET open saves/qsave.vvv` with a synchronous log: not the log thread |
| s3 | `io_bisect` | IOP audio, audsrv playback, pad driver, 8 polled pads: 35 rounds, all CRCs OK |
| s4 | `sdl_bisect`, `stat_probe` | hang in the first PhysicsFS open of a missing file before `SDL_Init`; `open()` of a missing file → errno 4 |
| s5 | P3.4.2-EINTR, `write_probe` | **playable**; saves 0 bytes; plain `write`+`close` correct in every variant, only PhysicsFS writes 0 bytes |
| s6 | P3.4.3-SAVEPAD | saves written (qsave 2508 B, unlock 2214 B, settings 1675 B) and loaded at the next start ("Continue" restored room 112,104); analog stick works |
| s7 (2026-10-27) | **H4-RC1** (commit `a435397`, zip `15c94a35…`, stick folder `VVVVVV-H4RC1` = the zip's `usb/VVVVVV` prepared with `prepare-ps2-usb.py`) | log (last launch only): 0 WARN/ERROR lines; saves loaded at start (qsave 2521 B, unlock 2214 B, settings 1675 B); Continue restored room (112,103); `lang_set=2` in settings; 34.3 ms period, 17–18 ms work; 0 music underruns, 0 late audsrv submissions, minimum queue 74 ms; first frame 7.95 s. Tester report: saves survive a full power-off; no VRAM picture during loading |

Performance on the console, P3.4.3 (overlay photos and log):

| Metric | Value |
|---|---|
| TPS | 29.5, target 29.4; 10 s minimum 29.0 |
| Frame | 34.0 ms mean, 37.3 ms max (log: 34.2 ms mean over 300-frame windows) |
| Render | 17.5 ms |
| Music decode | 171 ms per second, 0 underruns |
| audsrv queue | minimum 74 ms; late submissions 0; dry estimate 0 ms |
| Heap free | 6.3 MiB |
| USB log writes | 29 OK, 0 failed |
| First frame | 8.4 s after boot |

Total play time on hardware is short: about 5 minutes in s5, a few minutes in s6 and s7. The s7 log covers only the last launch (about 30 s of play; the log file is rewritten at every start).

## 3. Evidence gaps and unresolved risks

1. `P3.4.4` (fixes 3 and 5) has not run on hardware.
2. **A false warning is still flagged on hardware** (found in this audit, §4.2). `mkdir` of an existing directory returns errno 8 on `mass:`, which is FatFs `FR_EXIST`, not `EEXIST`. The game logs `WARN mkdir(...) failed: errno 8` at every start. `PS2Diag_Message` stores warnings as `last_error`, so the PERF page will still show "ERROR: see the DIAG page" after the stat fix. PCSX2 does not show it.
3. Only one console, one stick, one controller and a short play time are covered.
4. Save persistence across a full power cycle is likely but not confirmed. Session s6 shows a reload by a later run of the ELF; the log cannot tell whether the console was switched off in between.
5. The tester flow is untested on hardware: public zip → `prepare-ps2-usb.py` on the tester's PC → stick.
6. The existing packaging (`make-public-test-release.sh`) puts the frozen **H1-RC2** in `usb/VVVVVV/`. That build hangs on USB.
7. User documentation says the port has never run on a real PS2 (README, `docs/installing.md` compatibility table).
8. ps2sdk `read()` and `write()` on `mass:` return **0, not an error**, on FatFs failure (`bdmfs_fatfs fs_read/fs_write`). A full stick or an I/O error looks like EOF or a short write. The game logs a warning and continues. A save interrupted this way is truncated: `O_TRUNC` already emptied the old one.
9. Host tools in the build image (`apk add`) are not version-pinned. This may break bit-reproducibility later (known since H1).
10. Licensing for distributing binaries (T6). Added after this plan was written: `libps2_drivers` (fjtrujy/ps2_drivers 2.0.0, linked into the ELF through SDL's PS2 backend) has a GPL-3.0 `LICENSE` file but LGPL v2 source headers; this is unresolved and blocks binary release. pthread-embedded is partly LGPL 2.1. See [license-compliance-matrix.md](../license-compliance-matrix.md).

## 4. Investigation results

### 4.1 The startup ERROR (task phase 4)

- **Path:** `lang/en`. `FILESYSTEM_enumerateLanguageCodes` calls `FILESYSTEM_isDirectory("lang/en")` → `PHYSFS_stat`. The `P3.4.4` build logs the name: `Could not stat file lang/en: permission denied`.
- **Expected to exist?** Yes. It is the real `lang/en` directory, mounted from `mass:/…/lang/` (`mount_pre_datazip(mainLangDir, "lang", …)`).
- **Why it fails:**
  1. The write directory `savedata/` is mounted first in the search path.
  2. `PHYSFS_stat` (`physfs.c`, the search loop) stops at the first archive whose error is not `PHYSFS_ERR_NOT_FOUND`.
  3. `stat("…/savedata/lang/en")` fails with errno 1, which `errcodeFromErrno` maps to `PHYSFS_ERR_PERMISSION`. The walk never reaches the `lang/` mount.
- **Effect:** the English language entry is not enumerated. Every `PHYSFS_stat`/`isDirectory`/`isFile` of a path that lives in a later mount fails the same way. `PHYSFS_openRead` is not affected, because its loop does not stop on errors.
- **Why ERROR:** `FILESYSTEM_isFileType` logs every stat failure with `vlog_error`. Upstream behaviour; unchanged.
- **Root cause:** errno translation in ps2sdk (§4.3), not a missing file.
- **Smallest correct fix:** done in `P3.4.4`, PS2 only, in `__PHYSFS_platformStat`: a failed stat reports `PHYSFS_ERR_NOT_FOUND`. This is not a blanket suppression. The ps2sdk stat helper itself reports every failure as `ENOENT` (§4.3), so "not found" is exactly what ps2sdk means. The mangling to `EPERM` happens afterwards. Errors from open, read, write and close are unchanged.

### 4.2 The `mkdir` warning (new)

`ps2_mkdir` in `FileSystemUtils.cpp` (added by patch 0003) warns when `errno != EEXIST`. On `mass:`, an existing directory gives errno 8 (`-FR_EXIST` passed through). Smallest fix: when `mkdir` fails, `stat` the path, and stay silent if it is a directory. This does not depend on what the error number means. Warnings stay warnings. Optionally, the PERF footer could say "WARNING" when `last_error` came from a warning (P2).

### 4.3 ps2sdk / FatFs error semantics (task phase 5)

Traced in ps2sdk source at `329e2bb` (2026-10-12, the last commit before the image date). The installed binaries were not compared with this source.

| Layer | open | getstat (stat) | mkdir | read / write | close | fsync |
|---|---|---|---|---|---|---|
| IOP `bdmfs_fatfs` (`iop/fs/bdmfs_fatfs/src/fs_driver.c`) | `-FRESULT` | `-FRESULT` | `-FRESULT` | **0** bytes on error (`(ret == FR_OK) ? br : 0`) | `-FRESULT` | — |
| EE fileXio helper (`ee/rpc/filexio/src/fileXio_ps2sdk.c`) | passes the result | **sets `errno = ENOENT`, returns -1** | passes | passes | passes | — |
| EE libcglue (`ee/libcglue/src/glue.c`) | `__transform_errno`: `errno = -res` | **`__transform_errno(-1)` → errno = 1** | `errno = -res` | `errno = -res` | `errno = -res` | **always `ENOSYS`** |

Results:

- **errno after open/mkdir/remove/rename on `mass:` is a raw FatFs `FRESULT`.** FatFs `ff.h` gives `FR_NO_FILE` = 4 and `FR_EXIST` = 8, which match what we observed. Further codes: `FR_NO_PATH` = 5, `FR_DENIED` = 7, `FR_WRITE_PROTECTED` = 10. The exact FatFs revision ps2sdk fetches was not opened; the enum has been stable for years.
- **errno 1 after stat is a ps2sdk libcglue bug.** The fileXio helper sets `ENOENT` and returns -1, and `_stat` then calls `__transform_errno(-1)`, which overwrites errno with 1. This happens on every device, including `host:`. It is not FatFs.
- **On `host:` (PCSX2) the codes are different.** A missing file gives errno 5 from open. `mkdir` of an existing directory does not warn. So FatFs semantics hold only for FatFs devices.
- **`close()` on `mass:` returned 2 and 3.** That is positive, not 0. `fs_close` returns `-FR_OK` = 0, so the value comes from a higher layer. The cause was not traced; it is harmless because nothing treats it as an error.
- **Is a PhysicsFS translation layer enough?** Yes for this release:
  - EINTR retries are off on PS2. This holds whatever the device returns, so fix 1 cannot regress through a new error value.
  - Stat failures are "not found" (§4.1).
  - Flush no longer calls `fsync` (fix 2).
  - The remaining wrong numbers (open → 4 or 5) only affect the text of error messages. `PHYSFS_openRead` does not depend on them.
- **Do not map every PS2 errno as FatFs.** A full FRESULT→PhysFS mapping keyed on `mass:`/`usb`/`ata` path prefixes is possible. It is optional (P2) and must exclude `host:`, `mc0:` and `cdrom0:`.
- **Upstream (P2, the user decides; outward-facing).** Report or fix in ps2sdk:
  - the `_stat` double transform (set errno once);
  - `bdmfs_fatfs` exposing `FRESULT` as negative errno (map to POSIX codes in `fs_driver.c`);
  - `fs_read`/`fs_write` returning 0 on error.

  A local fix stays necessary regardless, because released toolchain images will carry the current behaviour for a long time.
- **Regression tests:** `tests/ps2/stat_probe` and `write_probe` already print the raw errno and the write-back result per variant. `physfs_probe` traces every libc call made by PhysicsFS. They run in PCSX2 (`host:`); on hardware they are run by hand from the stick. Their PCSX2 output becomes a reference (§9).

### 4.4 Controller (task phase 6)

The patch is `0010`, in `PS2_JoystickUpdate`:
- once per connection, when the pad state is `STABLE`, it reads the mode table;
- if the pad supports DualShock mode and is not already in it, it calls `padSetMainMode(DUALSHOCK, LOCK)`;
- a disconnect clears the flag.

`LOCK` disables the ANALOG button, so a press cannot turn the sticks off. D-pad and buttons report in both modes. SDL still opens all 8 pad slots and reports 8 controllers; that behaviour is unchanged since H1.

| Case | Status |
|---|---|
| D-pad, face buttons, Start | hardware ✅ (s5, s6) |
| Left stick after the fix | hardware ✅ (s6, user report) |
| L3+R3 overlay toggle | hardware ✅ (s6 photos) |
| Unplug and replug while running | untested (code path exists) |
| Third-party / non-DualShock pads, multitap | untested |
| "Twitchy" D-pad for small steps (s5 report) | open. Logic runs at the game's native 34 ms tick, and the log shows no input lag source. The user should re-test with the stick. No change is proposed without a reproducible difference from the PC version. |

## 5. Recommended release scope

**In scope:**
- one gameplay build of the current code (`P3.4.4` + §4.2);
- the existing dormant diagnostics: overlay via L3+R3, `bench.txt`, `fs_smoke`, the exception screen;
- the music and no-music ELFs;
- asset-free packaging with `prepare-ps2-usb.py`;
- updated documentation.

The release is marked **pre-release**, "tested on one SCPH-7500x".

**Out of scope:** see §12.

**Build choice.** Use a normal build from a clean tagged commit, not the `P3.4-HW-DIAG` or `IODIAG` packages. The diagnostics code is the same and stays inactive unless its flag file exists. Ship only `optional/overlay.txt` and `optional/bench.txt` to users. The test-only switches (`crashtest.txt`, `audiostall.txt`, `iodiag.txt`, `music_soak.txt`, `storage_real.txt`, `snapshots.txt`) are not shipped and not mentioned in user documents; they stay documented in the developer docs. Removing them from the code is not needed: they do nothing unless a user creates such a file by hand.

## 6. Task breakdown

Each task lists: priority, rationale, approach, acceptance criteria, validation, and whether hardware is needed (HW).

### T1 — Commit and tag the hardware-playable baseline (P0)

- **Rationale:** the only hardware-tested source exists as uncommitted files plus reconstructions in `build/`.
- **Approach:** commit the exact P3.4.3 state first, then the later changes on top. Commits are listed in §8, C1–C4.
  1. Before C1, put the reconstructed `0001`/`0003` (from `build/p343/patches-reconstructed/`) into `patches/` and keep the current versions aside. Restore them for C5.
  2. After C4, run a clean-clone build with `PS2_BUILD_ID=P3.4.3-SAVEPAD`.
  3. Create a local annotated tag `H3-FIRST-HARDWARE-PLAYABLE` on C4. The tag message holds the ELF hashes, the session list and the toolchain digest.
- **Acceptance:** a clean clone at the tag rebuilds `66f0568e…` and `c58a236c…`.
- **Validate:** `sha256sum` against §2.2. Archive the following in `release/H3-FIRST-HARDWARE-PLAYABLE/` (read-only, `SHA256SUMS`, not in git): the `VVVVVV-SAVEPAD` stick folder minus game data, the unstripped ELFs, `SOURCE.txt`, and `logs/hw/2026-10-26-s1…s6` without save files and game screenshots.
- **HW:** no.

### T2 — No false warning for existing directories (P0)

- **Rationale:** §4.2. A normal start-up must not raise ERROR/WARN.
- **Approach:** in `ps2_mkdir` (`FileSystemUtils.cpp`, patch 0003), when `mkdir` fails, check the path with `stat` + `S_ISDIR` and stay silent if it is a directory. Do not compare against 8.
- **Acceptance:** no `WARN mkdir` on `mass:` or `host:`. A read-only stick or a non-directory in the way still warns.
- **Validate:** PCSX2 smoke run; then the console (T7).
- **HW:** yes, for the final check.

### T3 — Hardware check of the P3.4.4 fixes (P0)

- **Rationale:** fixes 3 and 5 and T2 are only checked in PCSX2.
- **Approach:** build `P3.4.5` (P3.4.4 + T2). On the console:
  - no VRAM picture before loading;
  - DIAG `LAST ERR` empty after start-up;
  - no ERROR/WARN line in the log;
  - the language list contains English.
- **Acceptance:** all four observed. Log saved to `logs/hw/`.
- **HW:** yes.

### T4 — Public release packaging (P0)

- **Rationale:** the current public mode packages H1-RC2 (hangs), the P3.1 experimental build and the GS prototypes.
- **Approach:** add a release mode to `make-public-test-release.sh` (e.g. `--release <name>`). Do not write a new script.
  - **Single USB folder:** `usb/VVVVVV/` (`VVVVVV.elf`, `VVVVVV-nomusic.elf`, `fs_smoke.elf`, `fs_smoke_expect.txt`, `lang/`, `fonts/`, `README-PS2.txt`, `optional/overlay.txt`, `optional/bench.txt`).
  - **Also included:** `tools/prepare-ps2-usb.py`, `tools/repack-data-zip.py`, `README.txt`, `LICENSE-VVVVVV.md`, `THIRD-PARTY-NOTICES.txt`, `BUILD-INFO.txt`, `SHA256SUMS`.
  - **Not in the zip:** unstripped ELFs go to a separate `-symbols` folder.
  - **Refuse to build:** from a dirty tree, or when `SOURCE.txt` has a WARNING line.
  - **Checks:** `check-package.py` checks as today (no game data, no BIOS, no secrets).
- **Acceptance:**
  - the zip contains no `data.zip`, `data-ps2.zip` or asset files;
  - it contains no RC2/P31DEV/GSPOC folders;
  - `BUILD-INFO.txt` names the commit, the patch hashes and the toolchain digest;
  - the zip is deterministic: two runs give the same hash.
- **Validate:** run twice and compare. Then `check-package.py`. Then follow `README.txt` on a PC with a real `data.zip`.
- **HW:** no; T7 covers it.

### T5 — Documentation (P0 for user-facing claims, P1 for the rest)

See §10.

### T6 — Licensing decision (P0, user decision)

- **Rationale:**
  - The upstream `License exceptions.md` (at `79395fa`, updated 2024-01-23) lists no exception for a PS2 port.
  - The VVVVVV licence permits non-commercial redistribution of altered binaries if they are marked as altered and keep the notice. Game assets may not be distributed.
  - The upstream README says distributing a compiled version "might need permission first".
- **Approach:**
  - Ship binaries only, no assets (as H2-TEST1).
  - Keep the "altered, unofficial", non-commercial notices.
  - The user decides whether to publish without written permission. Contacting the author is the user's call; this project does not do it.
  - Record in `LICENSE.md` the one general exception that exists: a **Make and Play** build (`MAKEANDPLAY` define, no original levels) may ship the assets. That is a possible later product, not part of H4.
- **Acceptance:** the decision is written in the release notes draft; `LICENSE.md` matches the upstream file.
- **HW:** no.

### T7 — Release-candidate hardware session (P0)

- **Rationale:** gates G-HW in §11.
- **Approach:** checklist in §10.1, run with the final `H4-RC1` zip, laid out by `prepare-ps2-usb.py` on a PC as a tester would.
- **HW:** yes.

### T8 — CI status and bit-identity (P1)

- **Approach:**
  - Update `docs/ci.md` §8 with the two successful runs.
  - Download the `fb61092` run artifact (requires a GitHub login) and compare its unstripped ELF hashes with a local build of `fb61092`.
  - Add the release mode (T4) to CI as a non-publishing job that runs on tags. Upload the zip, symbols and metadata as artifacts. No `gh release` step.
- **Acceptance:** the hash comparison is recorded (equal or not, with the reason). The tag build artifact matches the local RC build.
- **HW:** no.

### T9 — Save-robustness checks (P1, mostly hardware)

- **Verified:**
  - creation, write, contents (valid XML, 2.5 KB qsave) and reload by the next run (s6);
  - missing files are handled (`tsave.vvv not found`; new files created);
  - empty or corrupt files do not crash: s5 started with 0-byte saves, logged XML errors and recreated the files.
- **Still needed:**

  | Check | Priority | HW |
  |---|---|---|
  | Persistence after a full power-off | P1 | yes |
  | Settings and unlocks survive (change a setting, unlock something, power-cycle) | P1 | yes |
  | Write-protected stick: start-up message, no hang | P2 | yes, if a stick with a switch is available |
  | Full stick: §3 item 8; documented, not fixed | P2 | — |
  | Power-off during a save: truncated file → the game resets that file; documented | P2 | — |

  No new save scheme is proposed: nothing release-blocking has been shown.

### T10 — ps2sdk error mapping and upstream reports (P2)

§4.3. Optional prefix-scoped FRESULT mapping. Draft upstream issues for the user.

### T11 — Controller follow-ups (P1/P2)

- P1: unplug/replug test (HW).
- P2: third-party pad, multitap, and re-check the D-pad feel with the stick.

### T12 — Small clean-ups (P2)

- The PCSX2-only "Could not stat file" filter in `PS2Diag_Message` becomes dead code after fix 3; remove it.
- Optional "WARNING" footer wording (§4.2).
- Pin the `apk` host tools in `docker/Dockerfile`, or at least record `/.apk-versions.txt` in `BUILD-INFO.txt`.

## 7. Dependencies

```
T1 (baseline) ─► C5/C6 committed ─► T2 ─► T3 (HW) ─► RC build ─► T4 ─► T7 (HW) ─► publish (user)
                                                       ▲                          ▲
T6 (licence decision) ─────────────────────────────────┘ (notes)                  │
T5 (docs) ─────────────────────────────────────────────────────────────────────────┤
T8 (CI) ── independent, before the RC tag ──────────────────────────────────────────┘
T9/T11 hardware checks run inside T3/T7 sessions
```

## 8. Files likely to change and proposed commits

Short messages, no co-author trailer (project rule).

| # | Commit | Files |
|---|---|---|
| C1 | PhysFS: no EINTR retry, no fsync on PS2 | `patches/vvvvvv/0001` (P3.4.3 version), `scripts/regen-vvvvvv-patches.sh` |
| C2 | SDL: DualShock analog mode | `patches/sdl2/0010-ps2-pad-analog-mode.patch` |
| C3 | IODIAG: asset open/load tracing | `patches/vvvvvv/0003` (P3.4.3 version) |
| C4 | Hardware I/O probes | `tests/ps2/CMakeLists.txt`, `tests/ps2/{io_bisect,sdl_bisect,stat_probe,physfs_probe,write_probe}/` |
| — | tag `H3-FIRST-HARDWARE-PLAYABLE` (local) | — |
| C5 | PhysFS: failed stat is not found on PS2 | `patches/vvvvvv/0001` (current) |
| C6 | Black frames before loading; stat error names the file | `patches/vvvvvv/0003` (current) |
| C7 | No warning for existing directories (T2) | `patches/vvvvvv/0003` |
| C8 | Release packaging mode (T4) | `scripts/make-public-test-release.sh`, `scripts/check-package.py`, `docs/tester/README.txt` |
| C9 | CI: release-mode artifact job (T8) | `.github/workflows/ci.yml`, `docs/ci.md` |
| C10 | Docs: first hardware results (T5) | `README.md`, `docs/installing.md`, `docs/ps2-hardware-test-plan.md`, `docs/technical-overview.md`, `docs/README.md`, new `docs/ps2-h3-hardware-report.md`, `LICENSE.md` |
| — | tag `H4-RC1` (local), after T7 passes | — |

C4 does not change the game ELF, so the tag after C4 still rebuilds the P3.4.3 hashes. C1–C3 must be checked against those hashes before tagging.

## 9. CI and packaging design

Three separate stages; nothing is published automatically:

1. **CI builds** (exists, ran green twice):
   - every push/PR builds `ci-<sha12>` in the pinned image, runs the host tests and `check-package.py`, and uploads an asset-free zip, symbols and metadata;
   - add: run `stat_probe`/`write_probe`/`physfs_probe` builds as compile checks.
2. **Release-candidate build** (new, T4/T8):
   - on a tag `H4-RC*`, the same workflow builds with `PS2_BUILD_ID=<tag>` and `--release`, and uploads the zip, symbols and `BUILD-INFO.txt`;
   - the maintainer compares the hashes with a local build. The RC becomes "hardware-tested" only after T7, recorded in the hardware report.
3. **Public GitHub pre-release:** manual, by the user, attaching the exact zip that passed T7. Its SHA-256 is written in the notes.

What CI cannot test:
- anything needing `data.zip` (gameplay, golden frames, music);
- anything needing a BIOS (PCSX2 runs);
- anything needing hardware: USB/FatFs semantics, timing, controller modes, VRAM leftovers.

The FatFs-specific failures (fixes 1–3 and T2) are invisible to PCSX2's `host:` and therefore to any emulator-based CI.

No second build system: CI calls the same `scripts/ps2-env.sh`, `build-*.sh` and `make-public-test-release.sh`.

## 10. Documentation plan

| Document | Change |
|---|---|
| `README.md` | "Runs on a real PS2 (tested: one SCPH-7500x, USB, FMCB)" instead of "not yet tested"; link to the hardware report; keep the AI-assisted development note |
| `docs/installing.md` | Compatibility table gets a third column, "SCPH-7500x (1 console)", with ✅ for boot, gameplay, music, saves, analog stick and overlay; ❓ for other models, PAL/NTSC variants, HDD/MC boot and multitap. Known issues: one tested console; full stick or power loss while saving can truncate a save; D-pad feel under review. Troubleshooting: the tested stick was FAT32 on an MBR partition (32 KB clusters); other layouts, such as an unpartitioned "superfloppy", are untested; run `fs_smoke` first |
| `docs/ps2-h3-hardware-report.md` (new) | Sessions s1–s6, the five bugs with root causes (§1, §4.3), the performance table (§2.3), the bisection method and probes |
| `docs/technical-overview.md` | Short section "ps2sdk seams": errno = FatFs FRESULT on `mass:`, the libcglue `_stat` errno bug, no `fsync`, read/write return 0 on error, DualShock analog mode |
| `docs/ps2-hardware-test-plan.md` | Mark sessions done; the RC checklist (§10.1) |
| `docs/ci.md` | Status: ran green on GitHub; release-mode job; the bit-identity result |
| `LICENSE.md` | Mention the upstream `License exceptions.md` and the Make and Play exception; restate that no PS2 exception exists |
| `docs/README.md` | Add H3/H4 rows to the history table |

### 10.1 Release-candidate hardware checklist (T7)

Use the `H4-RC1` zip and a freshly formatted stick (MBR + FAT32), prepared on a PC with `tools/prepare-ps2-usb.py` and the tester's own `data.zip`.

1. `fs_smoke.elf`: all checks pass.
2. `VVVVVV.elf` from the FMCB browser (wLaunchELF):
   - the boot console appears;
   - no VRAM picture;
   - the title screen appears in ≤ 10 s.
3. Overlay:
   - DIAG `LAST ERR` empty;
   - `STORAGE USB log ok N fail 0`.
4. New game, play ≥ 30 min across several areas (music changes, a few room transitions per minute):
   - TPS ≥ 29.0;
   - `PCM UR 0`, `late 0`, `DRY EST 0`.
5. D-pad and left stick both move the player. Unplug the pad for 5 s and replug it; the stick still works.
6. Save at a checkpoint/teleporter, change one setting, return to the menu, **power the console off**, boot again:
   - Continue restores the room;
   - the setting is kept.
7. `VVVVVV-nomusic.elf` boots and plays for 2 min.
8. Keep `vvvvvv_ps2.log`. Copy `savedata/` sizes, not contents. Take photos of the PERF and DIAG pages.

**Status after s7 (H4-RC1, 2026-10-27):**

| Step | Status | Evidence |
|---|---|---|
| Prepared from the zip with `prepare-ps2-usb.py` | PASS | prepared and checked by the developer; the stick was the existing test stick, not freshly formatted |
| 1. `fs_smoke.elf` | PENDING | no `fs_smoke.log` in the s7 folder |
| 2. boot, no VRAM picture, title ≤ 10 s | PASS | first frame 7.95 s (log); no VRAM picture (tester report) |
| 3. overlay `LAST ERR` empty | PASS (by log) | no WARN/ERROR line in the log; no overlay photo for s7 |
| 4. ≥ 30 min play, audio counters | PENDING | the log covers about 30 s: 0 underruns, 0 late, period 34.3 ms in that window |
| 5. D-pad, stick, unplug/replug | PENDING | stick and D-pad worked in s6 (P3.4.3); replug never tested |
| 6. save, setting, full power-off, Continue | PASS | saves loaded and room restored (log); power-off (tester report); setting kept (`lang_set=2`) |
| 7. `VVVVVV-nomusic.elf` | PENDING | not run |
| 8. logs and photos kept | PASS (partial) | `logs/hw/2026-10-27-s7/`; no PERF/DIAG photos for s7 |

## 11. Release gates

| Gate | Criterion | Priority |
|---|---|---|
| G-SRC source preservation | `H3-FIRST-HARDWARE-PLAYABLE` tag rebuilds the P3.4.3 hashes; RC built from a clean tagged commit | P0 |
| G-BUILD clean build | clean clone → RC ELFs; `SOURCE.txt` without WARNING or `-dirty` | P0 |
| G-FS filesystem | no hang on missing files; no ERROR/WARN on a normal start (T2, T3) | P0 |
| G-SAVE saves | write, reload after power-off, settings kept (§10.1 step 6) | P0 |
| G-PAD controller | D-pad, buttons, left stick, replug (step 5) | P0 (replug: P1) |
| G-PLAY stability | ≥ 30 min without exception or hang (step 4) | P0 |
| G-AUDIO audio | 0 underruns, 0 late, dry estimate 0 in that session | P0 |
| G-PERF performance | TPS ≥ 29.0 (10 s minimum). This is what the game needs and what s6 already shows; no stricter target | P0 |
| G-PKG packaging | asset-free, single folder, deterministic, `check-package.py` OK, tester flow works (§10.1 preparation) | P0 |
| G-LIC licensing | user decision recorded; notices present | P0 |
| G-DOC documentation | no "never tested on hardware" claims; one-console scope stated; known issues listed | P0 |
| G-CI | CI green on the RC commit; CI-vs-local hash comparison recorded | P1 |
| G-HW hardware verification | §10.1 passed on the exact zip that will be published | P0 |

## 12. Explicit non-goals

- New save system, A/B saves, memory-card saves.
- GS hardware renderer, renderer or audio rewrites, performance work: the console already runs at the game's native speed.
- Full errno remapping for every PS2 device; upstream ps2sdk patches as a release dependency.
- Input redesign.
- PAL 50 Hz mode, HDD/OPL/CD boot support.
- Make and Play edition with bundled assets.
- Any asset redistribution.
- Pushing, tagging on the remote, or publishing without the user.

## 13. Recommended execution order

1. T1 — commits C1–C4, rebuild check, local tag, archive.
2. Commits C5–C6; T2 (C7); PCSX2 smoke tests.
3. T3 on the console (build `P3.4.5`).
4. T6 — the user's licensing decision (parallel).
5. T4 (C8), T8 (C9), T5 (C10).
6. Build `H4-RC1` from a clean tagged commit (local tag); check the CI artifact hashes.
7. T7 on the console with the exact zip.
8. If every P0 gate passes: the user publishes a GitHub pre-release.

## 14. Recommendation

**Ready for a public pre-release after a small cleanup.**

The evidence for "ready":
- On real hardware the game holds 29.5 TPS against a 29.4 target, with a 37.3 ms worst frame.
- 0 audio underruns and 0 late submissions; a 74 ms minimum audsrv queue.
- 6.3 MiB of free heap.
- Saves written and reloaded.
- The analog stick works.
- Every hardware failure seen so far traced to a specific root cause in ps2sdk source, fixed in a few lines, and confirmed on the console (fixes 1, 2, 4) or in PCSX2 (fixes 3, 5).

None of the remaining blockers needs new engineering:
- one more `mkdir` warning fix;
- packaging that excludes the hanging RC2;
- documentation;
- the licensing decision;
- two hardware sessions.

The pre-release must state the evidence limits plainly:
- one console model, one stick, one controller;
- under half an hour of play in total;
- untested behaviour on full or write-protected sticks.

A non-pre-release ("stable") label should wait for reports from other consoles and longer play sessions.
