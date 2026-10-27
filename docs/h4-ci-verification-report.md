# H4.3: CI verification and release readiness

Status: 2026-10-27, draft for review. No remote write operation has been made in this task: no push, tag, workflow dispatch or artifact upload.

Statuses: **PASS** = verified with evidence (the evidence is named). **PENDING** = not tested yet. **BLOCKED** = cannot proceed until something outside this task is resolved.

## 1. Executive summary

- `H4-RC1` runs on the tested console. Session s7: no warnings or errors in the log, saves restored, a full power-off survived (tester report), no VRAM artefact. It was rebuilt from a clean checkout of `a435397`, bit-identical to the first candidate.
- **The new release-candidate workflow has not run on GitHub.** Its commits are local only; `origin/main` is `742a7ce`.
- The **normal CI workflow ran successfully on GitHub for `742a7ce`** (run `40411437388`): host tests including `fs_semantics`, the PS2 build and CI packaging. Its ELF checksums could not be read: logs and artifacts need a GitHub login, and none is available here. A local rebuild of the same commit is ready for the comparison.
- **New finding (distribution):** every CI run on `main` uploads the CI archive and the unstripped ELFs as artifacts. The repository is public, so any signed-in GitHub user can download them. Three runs' executables (`1711c68`, `fb61092`, `742a7ce`) are live until 2026-11-22/25. They link ps2_drivers, whose licence is unresolved (issue #27). A local, uncommitted workflow change now uploads executables only when the repository variable `UPLOAD_EXECUTABLES` is `true`.
- **Recommendation: NO-GO for a public pre-release now** (§14). Blockers:
  - the ps2_drivers licence;
  - the CI-vs-local ELF comparison;
  - four hardware checks still open.

## 2. Source revision

| | |
|---|---|
| Hardware-tested candidate | `a435397af429ced339beda7ab89752b36cd57682` (build ID `H4-RC1`) |
| Local `main` now | `67e0e97` = `a435397` + `79a88c0` (docs: H4-RC1 hardware check) + `67e0e97` (licence audit, notices text). No game, SDL or patch change after `a435397` |
| `origin/main` | `742a7ce` (C1–C8; pushed by the user on 2026-10-26) |
| Local-only commits | `784581b`, `f087689`, `7b71f82`, `0b1e3d1`, `a435397`, `79a88c0`, `67e0e97` |
| Frozen baseline | local tag `H3-FIRST-HARDWARE-PLAYABLE` → `b1e087c` (unchanged; not on the remote) |

## 3. Hardware-tested artifact

| | |
|---|---|
| Archive | `build/public/VVVVVV-PS2-H4-RC1.zip`, SHA-256 `15c94a35735a66081afa948900b9ec69441157b4cbdab97dfe28895da69d94ce` (unchanged in this task) |
| Stripped ELFs in it | `VVVVVV.elf` `d86d940d090608cf6dcac4efaa9275271aa0bff14044c95740c4b9372f6cb611`, `VVVVVV-nomusic.elf` `e0f98df071fa6594e94e914541b9fb364128e714b4f2d71f1c566ee01eaf8ddc`, `fs_smoke.elf` `f900140be6ff73cc2fe6faad3fd7bbb5736cd5f6bfb7d4cad04547dba7c03ed2` |
| Unstripped | `VVVVVV.elf` `3fac528427e252580eeaa663ce4d9fa0bbfbe1615a2892b70c1ed38f69e6b3fd`, `VVVVVV-nomusic.elf` `760dbdbef646e73acdf1f75d1f8d9589dfe89a826fb5a97d434653f9fe55f2f0`, `fs_smoke.elf` `4a5bcb14f1cfbcbe6bf5de4a5dd065f1806eda129760a1288342b727e26800aa` |
| On the console | stick folder `VVVVVV-H4RC1`: the zip's `usb/VVVVVV` prepared with `tools/prepare-ps2-usb.py`; `MANIFEST.sha256` verified after a remount; `VVVVVV.elf` on the stick `d86d940d…` |
| Logs | `logs/hw/2026-10-27-s7/VVVVVV-H4RC1/` (`vvvvvv_ps2.log`, `savedata/`) |

## 4. Local validation (all on `a435397` unless noted)

| Check | Status | Evidence |
|---|---|---|
| Clean-checkout rebuild bit-identical to the earlier candidate (`784581b`) | PASS | all three unstripped ELFs equal (§3) |
| Release packaging deterministic | PASS | two runs → `15c94a35…` |
| Provenance: replaced game ELF / `fs_smoke` rejected | PASS | both tamper tests stopped the script |
| `check-package --release` rejects a test switch, a test ELF, a second folder, game data, a missing LGPL text | PASS | 5/5 rejected |
| Host tests (12 incl. `fs_semantics` 21/21) | PASS | `run-host-tests.sh` exit 0 (only the 3 known informational `sdl_smoke` failures) |
| User path: unzip, `SHA256SUMS`, `prepare-ps2-usb.py` with a real `data.zip` | PASS | `SHA256SUMS OK`, `ready:` |
| FAT32 stick copy | PASS | `MANIFEST.sha256` OK after remount |
| PCSX2: `fs_smoke` ALL OK; game to first frame with 0 WARN/ERROR | PASS | `logs/pcsx2/h4rc1b-*` |
| CI steps on a committed scratch clone (bubblewrap): host tests, `--ci`, `--release` | PASS | run on 2026-10-26 before the four H4.2 commits, with the same content except the provenance stamps |

## 5. GitHub Actions runs

| Run | Commit | Event | Result | URL |
|---|---|---|---|---|
| 40411437388 | `742a7ce` | push to main | success (both jobs, every step) | https://github.com/MarvinParanoid/VVVVVV-PS2/actions/runs/40411437388 |
| 40035274946 | `fb61092` | push | success | https://github.com/MarvinParanoid/VVVVVV-PS2/actions/runs/40035274946 |
| 40023102518 | `1711c68` | push | success | https://github.com/MarvinParanoid/VVVVVV-PS2/actions/runs/40023102518 |

Read through the public API (no login): the step list and conclusions. Not read: job logs (HTTP 403 without login) and artifacts (HTTP 401). No run exists yet for the release-candidate mode or for `a435397`.

## 6. Host tests in CI

| | Status | Evidence |
|---|---|---|
| Host job on `742a7ce`, including the VVVVVV fetch and `fs_semantics` | PASS | steps "Fetch VVVVVV…", "Build…", "Run the tests…" all success; `run-host-tests.sh` exits non-zero if `fs_semantics` fails or prints no RESULT. The log text itself was not read |
| Host job on the H4.2 commits | PENDING | not pushed |

## 7. CI ELF checksum comparison

| | Status |
|---|---|
| CI `742a7ce` vs local rebuild of `742a7ce` (`PS2_BUILD_ID=ci-742a7ce61a7f`, clean worktree, bubblewrap) | **PENDING**. Local values: `VVVVVV.elf` `39ffed921a296c2a1d73f2d9e4fda1f62e8656891ec35af4a073e742eb7979b3`, `VVVVVV-nomusic.elf` `4b5d6bd8ce9ffa36a571f4ab888588ee4be3f7564f4315458477a783509d8730`, `fs_smoke.elf` `4a5bcb14…`. The CI values are in the metadata artifact `metadata-ci-742a7ce61a7f` (`BUILD-INFO.txt`, "Unstripped ELFs"), which needs a login to download |
| CI release build `H4-RC1` vs the hardware-tested unstripped ELFs (§3) | **PENDING**: needs a release-mode run |

How to compare the files:
- **Unstripped ELF:** must be equal for the same commit and build ID.
- **Stripped ELF:** follows from the unstripped one.
- **Package contents:** `SHA256SUMS` per file.
- **Metadata:** `BUILD-INFO.txt` differs by design (workflow URL, packager line).
- **ZIP:** differs whenever any text file differs, so a different ZIP SHA-256 alone says nothing about the executables.

The `742a7ce` ELFs differ from `H4-RC1` by design: other source (no P3.4.4/P3.4.5 changes) and another build ID.

## 8. ZIP checksum comparison

PENDING (no CI release-mode run). The expected outcome is not byte-identity:
- `BUILD-INFO.txt` names the workflow run;
- a CI build of the current `main` (`67e0e97`) also has the updated `THIRD-PARTY-NOTICES.txt` text.

A rebuild of `a435397` in CI would differ only in `BUILD-INFO.txt`. The test is the per-file `SHA256SUMS` comparison.

## 9. Packaging verification

| | Status | Evidence |
|---|---|---|
| Release mode cannot include H1-RC2 | PASS | `--release` has no code path to `release/`; that folder is not in git, so a CI checkout cannot even see it; `check-package --release` allows only `usb/VVVVVV` |
| No silent fallback to another ELF | PASS (local) | build ID, commit and recorded checksums are checked (§4) |
| Release mode in CI (tag `H*-RC*` or manual run with a release name) | PENDING | never run on GitHub |
| SHA-256 generation (`SHA256SUMS`, `.zip.sha256`, BUILD-INFO checksums) | PASS (local) | §4; CI produced `.zip.sha256` for `742a7ce` (artifact listed) |

## 10. Workflow audit

| Item | Finding |
|---|---|
| Triggers | pull requests; pushes to `main`; tags `H*-RC*`; manual runs (`workflow_dispatch`, optional input `release`) |
| A chosen commit without a tag | Not directly. A manual run builds the head of the selected branch or tag; there is no commit input. A temporary branch pointing at the commit works, but needs a push |
| Checkout | the pushed commit (PR: head commit), `persist-credentials: false`; shallow (no tags), so `SOURCE.txt` shows a bare hash, which the release checks accept |
| Toolchain | ps2dev image pinned by digest (`5476fdfb…`); actions pinned by commit SHA; host tools in the image added by `apk add` **unpinned** (drift risk); runner `ubuntu-24.04` is a floating image (host tests only) |
| Downloads | VVVVVV by pinned commit (git); SDL 2.32.10 tarball SHA-256-checked (host and PS2); no `actions/cache`, so every run starts clean |
| Host tests | built and run on the runner; `fs_semantics` needs the VVVVVV fetch step (present since `742a7ce`) |
| Failure propagation | GitHub's default `bash -e -o pipefail` for `run:`; `if-no-files-found: error` on required uploads; the package checks exit non-zero |
| Token | `permissions: contents: read`; no release step |
| Artifacts | names follow the build ID; the archive and unstripped ELFs are public to signed-in users (see §12); metadata contains the checksums |
| Concurrency | cancels only superseded pull-request runs |
| YAML | parses; `actionlint` was **not** run in this task (running a downloaded binary was refused by the local safety policy). It last passed on 2026-10-23, before the release-mode changes |

Local, uncommitted change for review: steps "Upload the tester archive" and "Upload unstripped ELFs" run only `if: vars.UPLOAD_EXECUTABLES == 'true'`; otherwise a step notes it in the job summary. Metadata is still uploaded.

## 11. Toolchain and dependency provenance

| | Local (bubblewrap) | CI (Docker) |
|---|---|---|
| ps2dev image | `sha256:5476fdfb…` (ps2dev `168ba29`, 2026-10-13) | same digest (Dockerfile `FROM`) |
| EE compiler / linker | GCC 15.2.0, GNU ld 2.45.1 | same image |
| Host tools in the container (apk) | Alpine 3.24.2: cmake 4.2.3-r0, make 4.4.1-r4, gcc 15.2.0-r5, binutils 2.45.1-r1, musl 1.2.6-r3, git 2.54.0-r0, pkgconf 2.5.1-r0, python3 3.14.8-r0 (installed once, cached in `~/.cache/vvvvvv-ps2`) | installed on every run, unpinned; listed in the run's `BUILD-INFO.txt` (not read yet) |
| ps2sdk | inside the image; exact commit not recorded (nearest upstream commit at the image date: `329e2bb`) | same |
| VVVVVV | `79395fa47d35e1de5332e6b4a342249c46ac6fb9` + submodules (`SOURCE.txt`) | same, fetched fresh |
| SDL | 2.32.10 tarball, SHA-256 `5f5993c5…`, + `patches/sdl2` 0001–0010 | same |
| Build flags | CMake `Release`, `PS2_BUILD_ID`, repository git info kept out of the ELF | same scripts |
| Caches | local: `~/.cache/vvvvvv-ps2` (VVVVVV git, toolchain rootfs); `build/` outputs not reused by `--refetch` builds | none |

Reproducibility:
- **PASS on this host:** clean worktree and clean clone builds, repeated, bit-identical (H3 and H4-RC1).
- **PENDING across machines and backends:** CI (Docker, fresh runner) against local (bubblewrap); §7 is the test.

No undocumented local file is known to enter the build. The unpinned `apk` tools are a documented drift risk, not a known difference.

## 12. Licensing and distribution

| Item | Status |
|---|---|
| ps2_drivers licence (GPL-3.0 `LICENSE` vs LGPL v2 headers), issue #27 | **BLOCKED**: no reply as of 2026-10-27 |
| Executables already exposed as public CI artifacts (3 runs, live until 2026-11-22/25) | **open decision for the user**: delete them on the run pages (a remote write) or let them expire |
| Future CI runs uploading executables | local fix PENDING review (`UPLOAD_EXECUTABLES` switch) |
| LGPL relinking (pthread-embedded, audsrv) | notices in the archive; legal sufficiency not reviewed (THIRD-PARTY-NOTICES part B) |
| VVVVVV binary without written permission | user decision |
| Placeholder copyright holder "VVVVVV-PS2 contributors" | user decision |

## 13. Hardware acceptance checklist (H4-RC1)

| Check | Status | Evidence |
|---|---|---|
| Boots from USB, first frame ≤ 10 s | PASS | 7.95 s (s7 log) |
| No WARN/ERROR in a normal start | PASS | s7 log |
| No VRAM artefact during loading | PASS | tester report (s7) |
| Save restoration (Continue) | PASS | s7 log: saves read, room (112,103) restored |
| Persistence across a full power-off | PASS | tester report ("yes, I switched it off"); the log cannot show it |
| Setting persisted (language) | PASS | `lang_set=2` in `settings.vvv`; no language menu in the s7 log |
| Performance and audio, short window | PASS | ~30 s logged: 34.3 ms period, 0 underruns, 0 late, min queue 74 ms |
| ≥ 30 min gameplay | PENDING | |
| Controller unplug/replug | PENDING | |
| Left stick and D-pad | PASS on P3.4.3 (s6); not re-observed in s7's log | the input code has not changed since `a435397`'s parent builds |
| `fs_smoke.elf` from the release folder | PENDING | no `fs_smoke.log` in the s7 folder. Not to be confused with s1, where the `H1-RC2` folder's `fs_smoke` ran: all checks passed except `opendir(".")`, so it ended with PROBLEMS; the check is unchanged in `H4-RC1` |
| `VVVVVV-nomusic.elf` | PENDING | |

## 14. Remaining tasks and recommendation

**Required user actions:**
1. Review the uncommitted changes: the workflow upload switch, the plan update (§10.1 status), this report.
2. Read-only, no push needed: download the artifact `metadata-ci-742a7ce61a7f` (text files only, no executables) from run 40411437388 and put it somewhere I can read it. This completes the CI-vs-local comparison for `742a7ce` (§7).
3. Decide about the executables already uploaded by the three earlier runs.
4. Authorize the remote steps:
   - (a) commit the reviewed changes and push `main` (CI runs, uploading only metadata while `UPLOAD_EXECUTABLES` is unset);
   - (b) start a manual run of the workflow on `main` with `release` = `H4-RC1` (no tag, no GitHub release);
   - (c) download its metadata for the comparison with §3.

   A tag push would also work, but creates a remote tag; that is not needed.
5. Hardware: 30+ minutes of play, pad unplug/replug, `fs_smoke.elf` and `VVVVVV-nomusic.elf` from `VVVVVV-H4RC1`.
6. Licence: wait for issue #27; then the binary-permission and copyright-holder decisions.

**Is H4-RC1 still valid?** Yes. No source, patch or ELF change since `a435397`; the archive is unchanged. A CI build of today's `main` would differ in the notices text only.

**Recommendation: NO-GO** for a public pre-release now.
- **BLOCKED:** licensing (ps2_drivers).
- **PENDING:** CI-vs-local ELF identity and the release-mode CI run; four hardware checks.

None of these needs code changes to the game. Once they pass, the next step is a GO review of `H4-RC1`, or of an `H4-RC2` that differs only in the updated notices.
