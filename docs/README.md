# Documentation

## Using the port

- [installing.md](installing.md): compatibility, limitations, game data, USB stick, launching, controls, reporting results.
- [ps2-hardware-first-boot.md](ps2-hardware-first-boot.md): printable checklist for a first session on a console.
- [ps2-hardware-test-plan.md](ps2-hardware-test-plan.md): the developer's multi-session hardware test plan.
- [tester/](tester/): `README.txt` and `TESTING.txt` for the tester release zip; `README-CI.txt` for CI build archives.
- [ps2-usb-readme.txt](ps2-usb-readme.txt): the README placed in each USB folder.

## Development

- [building.md](building.md): requirements, build, PCSX2.
- [ci.md](ci.md): GitHub Actions build, artifacts, checksums and build IDs, release candidates, release workflow design.
- [releasing.md](releasing.md): the end-user release-candidate archive (`--release`), its rules, local reproduction and CI.
- [h4-first-public-release-plan.md](h4-first-public-release-plan.md), [license-compliance-matrix.md](license-compliance-matrix.md): first public release plan and licence matrix.
- [../CONTRIBUTING.md](../CONTRIBUTING.md): patch workflow and ground rules.
- [technical-overview.md](technical-overview.md): a short tour of the engineering work.
- [ps2-hardware-diagnostics.md](ps2-hardware-diagnostics.md): the diagnostics overlay, its metrics and its cost.

## Development history

Reports for each phase, with methods and measurements. All numbers come from PCSX2 unless a report says otherwise.

| Phase | Report |
|---|---|
| Feasibility study | [ps2-feasibility.md](ps2-feasibility.md) |
| P1: SDL2 environment and present path | [ps2-p1-report.md](ps2-p1-report.md) |
| P1c: renderer profile | [ps2-p1c-render-profile.md](ps2-p1c-render-profile.md) |
| P2a: first playable build | [ps2-p2a-report.md](ps2-p2a-report.md) |
| Port comparison: other constrained ports and the music plan | [vvvvvv-cross-platform-port-analysis.md](vvvvvv-cross-platform-port-analysis.md), [dreamcast-port-analysis.md](dreamcast-port-analysis.md) |
| P2b: music streaming | [ps2-p2b-report.md](ps2-p2b-report.md) |
| P3: rendering optimisation | [ps2-p3-report.md](ps2-p3-report.md) |
| H1: hardware readiness | [ps2-h1-hardware-readiness.md](ps2-h1-hardware-readiness.md) |
| H1.1: release candidate `H1-RC2` | [ps2-h1-release-candidate.md](ps2-h1-release-candidate.md) |
| G1: GS renderer feasibility (deferred) | [ps2-gs-renderer-feasibility.md](ps2-gs-renderer-feasibility.md) |
| P3.1: software renderer edge cases (Flip Mode) | [ps2-p3.1-software-renderer-edge-cases.md](ps2-p3.1-software-renderer-edge-cases.md) |
| P3.2: final review and hardware test preparation | [ps2-p3.2-final-review.md](ps2-p3.2-final-review.md) |
| H2: tester release `H2-TEST1` (prepared, not published) | [ps2-h2-tester-release.md](ps2-h2-tester-release.md) |
| P3.3: diagnostics overlay | [ps2-hardware-diagnostics.md](ps2-hardware-diagnostics.md) |

The reports refer to `logs/`, `release/` and `build/` folders. Those are local outputs of the scripts and are not part of the repository.
