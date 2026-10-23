# Continuous integration and releases

How the GitHub Actions build works, what it checks, what it produces, and how release candidates and releases are made.

> **Status (2026-10-23):** the workflow has **not yet run on GitHub**. Until the first run succeeds, treat this page as the design. What was verified:
> - `actionlint` 1.7.12 reports no problems. Shellcheck was not installed, so the shell code inside `run:` steps was not linted.
> - The same steps ran locally with the bubblewrap backend, from a clean clone with these changes committed: build, host tests, packaging and checks all passed.
> - The CI archive was 31 files and 3.7 MiB. Packaging it twice gave the same SHA-256.
>
> What was not verified: the Docker backend that CI uses has not run anywhere yet.

## 1. What runs, and when

The workflow is [`.github/workflows/ci.yml`](../.github/workflows/ci.yml). It runs:
- on every pull request (the PR's head commit is built, not GitHub's merge commit, so the build ID names a real commit);
- on every push to `main`;
- by hand: **Actions → CI → Run workflow**.

It has read-only permissions (`contents: read`), uses no secrets, and the third-party actions are pinned by commit. Every step fails the job on error. Two jobs run in parallel on `ubuntu-24.04`:

| Job | Steps | Takes about |
|---|---|---|
| **Host reference tests** | `scripts/build-host-tests.sh` (SDL 2.32.10 stock and patched, built for Linux), then `scripts/run-host-tests.sh` | about 1 min locally |
| **PS2 build and tester archive** | `build-sdl2-ps2.sh`, `build-vvvvvv-ps2.sh --refetch`, `build-ps2-tests.sh`, then `make-public-test-release.sh --ci`, which packages the build and checks the archive | under 1 min locally with the toolchain cached; on GitHub see §8 |

The PS2 job uses the same scripts as a local build, with `PS2ENV_BACKEND=docker`. The toolchain is the ps2dev image pinned by digest (`docker/Dockerfile`, `scripts/ps2-env.sh`). SDL is pinned by SHA-256 and VVVVVV by commit.

## 2. Build ID, checksums and metadata

**Build ID.** A CI build is `ci-<first 12 hex digits of the commit>`, for example `ci-8afab94c1d2e`. It is compiled into the ELFs (`PS2_BUILD_ID`) and appears on:
- the boot console;
- the fatal and exception screens;
- the overlay;
- the first log line.

A photo of any of these screens identifies the exact commit.

**Where each piece of identity is recorded:**

| File | Contents |
|---|---|
| `usb/VVVVVV-CI/SOURCE.txt` | `build` ID, `repo` commit (never `-dirty`: packaging refuses dirty builds), upstream VVVVVV commit and submodules, SHA-256 prefixes of every patch, and SHA-256 of the unstripped ELFs |
| `BUILD-INFO.txt` | Commit, link to the source tree at that commit, workflow run URL and attempt, toolchain image digest, versions of the build tools in the container (`cmake`, `gcc`, …), and SHA-256 of the stripped and unstripped ELFs |
| `SHA256SUMS` (in the archive) | Every file in the archive. Check with `sha256sum -c SHA256SUMS` |
| `VVVVVV-PS2-<id>.zip.sha256` | The archive itself. In the metadata artifact |

GitHub also shows a SHA-256 digest for each artifact on the run page.

## 3. Artifacts

Each successful run of the PS2 job uploads three artifacts, kept for 30 days:

| Artifact | Contents | For |
|---|---|---|
| `VVVVVV-PS2-ci-<sha>.zip` | The asset-free tester archive. Uploaded as-is (`archive: false`), so the download *is* the zip | Developers and testers |
| `metadata-ci-<sha>` | `BUILD-INFO.txt`, `SOURCE.txt`, `VVVVVV-PS2-ci-<sha>.zip.sha256` | Checking a download, bug reports |
| `symbols-ci-<sha>` | Unstripped `VVVVVV.elf`, `VVVVVV-nomusic.elf`, `fs_smoke.elf` | Symbolising crash addresses: `scripts/ps2-profile.py <elf> --addr <EPC> <RA>` |

The host job uploads `host-test-logs` (14 days).

**Contents of the archive:**

```
VVVVVV-PS2-ci-<sha>/
  README.txt                docs/tester/README-CI.txt
  BUILD-INFO.txt  SHA256SUMS  LICENSE-VVVVVV.md  THIRD-PARTY-NOTICES.txt
  tools/                    prepare-ps2-usb.py, repack-data-zip.py
  usb/VVVVVV-CI/            stripped VVVVVV.elf, VVVVVV-nomusic.elf, fs_smoke.elf,
                            open-source lang/en and Latin fonts, SOURCE.txt,
                            README-PS2.txt, optional/ (bench and overlay switches)
```

It is laid out by `make-usb-package.sh --no-data`, the same code as the developer stick and the H2 tester release. It contains **only binaries built in that run**:
- the frozen `H1-RC2` binaries live only in the maintainer's local `release/` folder;
- they are never rebuilt, renamed or included by CI;
- `check-package.py` fails the run if any folder's `SOURCE.txt` names a build other than the run's ID.

**Downloading:**
1. Open the run (Actions → CI → the run).
2. Scroll to **Artifacts** and click the name. You need to be signed in to GitHub.
3. Check the download: compare its SHA-256 with `VVVVVV-PS2-ci-<sha>.zip.sha256` from the metadata artifact. Inside the unzipped folder, run `sha256sum -c SHA256SUMS`.

`gh run download <run-id>` should also work, but has not been tried with the non-archived zip.

Artifacts are visible only to people who can see the repository's Actions runs. **A CI artifact is not a release.** It is a development build of one commit, and nobody has run it on hardware.

## 4. What is checked automatically, and what is not

| Check | Where | Automatic |
|---|---|---|
| SDL, VVVVVV and the PS2 test programs compile and link for the EE | PS2 job | yes |
| Patches apply to the pinned upstream sources | `fetch-vvvvvv.sh`, `build-sdl2-ps2.sh` (`patch` fails the step) | yes |
| The build tree matches `patches/` (no stale tree) and the repository is clean | `build-vvvvvv-ps2.sh` writes `WARNING`/`-dirty`; packaging refuses both | yes |
| Renderer correctness on the host: `rt_soft`, `rt_scene`, `rt_micro` (stock and patched SDL), `sw_exact` 6/6 and `flip_exact` 36/36 (bit-exact fast paths, patched SDL) | host job | yes |
| `sdl_smoke` on the host | host job, **informational**: it tests PS2 drivers and always reports 3 FAILs on Linux (no game controller, no PS2 render driver) | runs, does not gate |
| Archive: no game data (`.zip .vvv .wav .ogg .png` outside `fonts/` …), BIOS or memory card images, tokens or private keys, local home paths, caches (`.git`, `__pycache__`, `CMakeCache.txt`), symlinks, files over 8 MiB | `scripts/check-package.py` | yes |
| Archive: safe entry paths, one top-level folder, `SHA256SUMS` matches every entry, required files present, tools compile | `check-package.py zip` | yes |
| Every ELF is a 32-bit little-endian MIPS executable with an entry point and loadable segments inside the file, above the EE kernel area | `check-package.py zip` | yes |
| Build ID and commit in `SOURCE.txt` and `BUILD-INFO.txt` match the run | `check-package.py zip --build --commit` | yes |
| The game boots, renders, plays music; golden frames; benchmarks; `fs_smoke` on a USB image | PCSX2 with a user-supplied BIOS and `data.zip`: `scripts/run-*.sh`, see [building.md](building.md) and [CONTRIBUTING.md](../CONTRIBUTING.md) | **no**, needs a BIOS and game data, which CI must never have |
| PS2 test programs on the EE (`sw_exact`, `flip_exact` on PS2, `rt_*`) | PCSX2 | **no** |
| Anything on a real console | [ps2-hardware-test-plan.md](ps2-hardware-test-plan.md) | **no**: nothing has run on hardware yet |
| Bit-identity between a CI build and a local build | §5 | **no**, manual comparison |

## 5. Reproducing a CI build locally

From a clean checkout of the same commit:

```sh
id=ci-$(git rev-parse --short=12 HEAD)
scripts/build-sdl2-ps2.sh
PS2_BUILD_ID=$id scripts/build-vvvvvv-ps2.sh --refetch
scripts/build-ps2-tests.sh
EXP_ID=$id scripts/make-public-test-release.sh --ci "$id"   # -> build/public/VVVVVV-PS2-$id.zip
scripts/build-host-tests.sh && scripts/run-host-tests.sh     # the host job
```

**Comparing with CI.** Compare the ELF hashes in your `out/vvvvvv-ps2/SOURCE.txt` with the "Unstripped ELFs" lines of the run's `BUILD-INFO.txt`. Equal hashes show that the bubblewrap and Docker backends produce the same binaries. **This is unverified.** Both start from the same pinned image, but they install `cmake`, `make` and the other host tools with `apk add` when the environment is first created:
- the bubblewrap backend does this once and caches the result;
- Docker does it on each image build.

So the tool versions can drift. `BUILD-INFO.txt` lists them, so a difference can be traced.

The archive itself is deterministic: sorted entries, fixed timestamps and modes.

## 6. Release candidates

Release candidates are made locally, as for H1-RC2 ([ps2-h1-release-candidate.md](ps2-h1-release-candidate.md)):

```sh
scripts/build-sdl2-ps2.sh
PS2_BUILD_ID=H1-RC3 scripts/build-vvvvvv-ps2.sh --refetch
scripts/make-usb-package.sh --no-data         # builds fs_smoke as well
scripts/freeze-release.sh H1-RC3              # -> release/VVVVVV-PS2-H1-RC3/, read-only
```

`freeze-release.sh` refuses to touch an existing release folder. A frozen release candidate is never rebuilt or modified. Its `SHA256SUMS` is verified each time it is packaged, and `BUILD.md` inside it gives the command that reproduces its ELFs bit for bit. CI does not create or package release candidates.

The H2-style tester archive (frozen RC plus experimental build) is still made locally with `scripts/make-public-test-release.sh <name>`. Since this change, the H2 mode also runs `check-package.py` on the finished zip.

## 7. Release workflow (design only; not implemented)

There is deliberately **no release workflow yet**. Three conditions must hold first:
- the first hardware test is complete;
- the upstream VVVVVV licensing question is settled ([ps2-h2-tester-release.md](ps2-h2-tester-release.md));
- at least one CI run has succeeded on GitHub.

When it is added, it should be a separate `release.yml` with these properties:

| Requirement | Design |
|---|---|
| No release on push | `on: workflow_dispatch` only. Inputs: `run_id` (a successful CI run on `main`) and `name` (e.g. `H3-TEST1`) |
| Manual approval | The job runs in a GitHub environment `release` with a required reviewer |
| Assets come from the validated CI build, never a rebuild | `actions/download-artifact` with `run-id: ${{ inputs.run_id }}` (needs `actions: read`). No build steps in the workflow at all |
| Never a different source revision | Read the run's `head_sha` from the API. Require `conclusion == success`, event `push` on `main`, and an artifact named `VVVVVV-PS2-ci-<head_sha[:12]>.zip`. Re-run `check-package.py zip --build ci-<sha12> --commit <head_sha>` on the downloaded file before upload. The tag targets `head_sha` explicitly (`gh release create --target <head_sha>`) |
| Pre-release by default | `--prerelease --draft`. A human edits the notes and publishes the draft. A non-pre-release needs a separate, deliberate input once hardware results exist |
| Minimal permissions | Workflow default `contents: read`; only the release job gets `contents: write` and `actions: read` |
| No game assets | The asset is the asset-free tester zip. `check-package.py` refuses game data. **Never add a data-bearing package** unless the licensing conditions are verified and documented |
| Traceability | Release notes quote `BUILD-INFO.txt` (commit, run URL, ELF hashes) and attach the `.zip.sha256` and `symbols` artifact |

If a release is ever built under a release-candidate name (e.g. `H1-RC3`) rather than `ci-<sha>`, that build ID has to be produced by a CI run made for it, using a `workflow_dispatch` input for the build ID. A frozen local RC is never re-labelled or replaced by a CI build.

## 8. First run on GitHub

1. Review and commit the changes, then push `main` (or open a PR from a branch).
   - The workflow starts by itself on the push or PR.
   - Or start it by hand: **Actions → CI → Run workflow → Branch: main → Run workflow**.
   - Actions must be enabled for the repository: Settings → Actions → General → "Allow all actions" or "Allow GitHub actions" (this workflow uses only `actions/checkout` and `actions/upload-artifact`).
2. Expected duration: the PS2 job pulls the ps2dev image (about 1 GB) and builds everything from source. Allow 15–30 minutes the first time; the job is capped at 60 minutes. The host job takes a few minutes.
3. Check the results:
   - both jobs are green;
   - the run summary shows `BUILD-INFO.txt`;
   - the three PS2 artifacts and `host-test-logs` are listed.
4. Download `VVVVVV-PS2-ci-<sha>.zip` and verify it:
   - check it against `metadata-ci-<sha>`;
   - `unzip` it and run `sha256sum -c SHA256SUMS`;
   - optionally: `python3 scripts/check-package.py zip VVVVVV-PS2-ci-<sha>.zip --build ci-<sha> --commit <full sha>`.
5. Compare the ELF hashes with a local build of the same commit (§5) and note the result in this document.

**Likely first-run failures,** since the Docker backend has not been tested before:
- the `docker build` step (`apk add` from Alpine's network mirror);
- file ownership between the container user (`-u $(id -u):$(id -g)`) and the checkout;
- a missing tool on the runner.

The failing step's log names the command. All of them can be reproduced locally with `PS2ENV_BACKEND=docker`.
