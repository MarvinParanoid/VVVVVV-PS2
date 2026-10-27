# Archive

Historical reports and plans, kept as evidence of how the port was built and tested. **They describe the project at the time they were written and are not updated.** Statements such as "not yet tested on a real PS2" were true then.

For the current state, see:
- [known-issues.md](../known-issues.md) for what works and what is tested;
- [architecture.md](../architecture.md) for how the port works;
- [releasing.md](../releasing.md) for candidates and gates.

Unless a report says otherwise, its numbers come from PCSX2 v2.8.2.

## Phase reports

| Phase | Report |
|---|---|
| Feasibility | [ps2-feasibility.md](ps2-feasibility.md) |
| P1: SDL2 environment and present path | [ps2-p1-report.md](ps2-p1-report.md) |
| P1c: renderer profile | [ps2-p1c-render-profile.md](ps2-p1c-render-profile.md) |
| P2a: first playable build | [ps2-p2a-report.md](ps2-p2a-report.md) |
| Port comparison and the music plan | [vvvvvv-cross-platform-port-analysis.md](vvvvvv-cross-platform-port-analysis.md), [dreamcast-port-analysis.md](dreamcast-port-analysis.md) |
| P2b: music streaming | [ps2-p2b-report.md](ps2-p2b-report.md) |
| P3: rendering optimisation | [ps2-p3-report.md](ps2-p3-report.md) |
| H1: hardware readiness | [ps2-h1-hardware-readiness.md](ps2-h1-hardware-readiness.md) |
| H1.1: release candidate `H1-RC2` (hangs on a console, found later) | [ps2-h1-release-candidate.md](ps2-h1-release-candidate.md) |
| G1: GS renderer feasibility (deferred) | [ps2-gs-renderer-feasibility.md](ps2-gs-renderer-feasibility.md) |
| P3.1: software renderer edge cases (Flip Mode) | [ps2-p3.1-software-renderer-edge-cases.md](ps2-p3.1-software-renderer-edge-cases.md) |
| P3.2: final review before hardware tests | [ps2-p3.2-final-review.md](ps2-p3.2-final-review.md) |
| H2: tester release `H2-TEST1` (never published) | [ps2-h2-tester-release.md](ps2-h2-tester-release.md) |
| P3.3/P3.4: diagnostics overlay, audio metrics, exception stack | [ps2-hardware-diagnostics.md](ps2-hardware-diagnostics.md) |
| Other open-source PS2 ports | [ps2-open-source-ports-comparison.md](ps2-open-source-ports-comparison.md) |

## Hardware testing

| Document | Content |
|---|---|
| [ps2-hardware-first-boot.md](ps2-hardware-first-boot.md) | first-session checklist written for `H1-RC2` |
| [ps2-hardware-test-plan.md](ps2-hardware-test-plan.md) | multi-session plan for `H1-RC2`, `P3.2-T1` and the G1 prototypes |
| [h4-first-public-release-plan.md](h4-first-public-release-plan.md) | H4 audit and plan (2026-10-26). Its §1, §2.3 and §4 record the first console sessions s1–s7, the five ps2sdk-related bugs and the error-code analysis. Its tasks were carried out in later commits; the release gates now live in [releasing.md](../releasing.md#release-gates) |

The raw console logs (`logs/hw/`) are kept locally by the maintainer and are not part of the repository.
