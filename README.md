# VVVVVV for PlayStation 2

An unofficial, non-commercial homebrew port of Terry Cavanagh's
[VVVVVV](https://github.com/TerryCavanagh/VVVVVV) to the PlayStation 2. It is
built from the open-source VVVVVV code with SDL2 and the
[ps2dev](https://github.com/ps2dev) toolchain, and runs from a USB stick.

**No GitHub Release has been published yet.** The current release candidate is
`H4-RC1`; see [docs/releasing.md](docs/releasing.md) for its status.

## What works

- The full game in English: main game, time trials, Secret Lab, Super Gravitron.
- Streamed music and sound effects.
- DualShock 2: D-pad, buttons and the left analog stick.
- Saves on the USB stick, including Continue.
- Diagnostics for testers: a start-up screen, error and exception screens, a performance overlay (L3+R3), a benchmark and a USB self-test.

## Tested hardware

One console so far: a **PS2 Slim SCPH-7500x** (EU) with Free McBoot, an 8 GB
FAT32 USB stick, an official DualShock 2 and component video. On it:
- **Speed:** the game runs at full speed (29.5 of 29.4 logic ticks per second in normal rooms).
- **Audio:** no audio dropouts.
- **Saves:** written, restored by Continue, kept after a full power-off.

Not yet checked on the console:
- the heaviest scenes;
- long sessions;
- controller reconnection.

Other models are untested. Details: [docs/known-issues.md](docs/known-issues.md).

Builds before tag `H3-FIRST-HARDWARE-PLAYABLE`, including `H1-RC2` and
`H2-TEST1`, hang on a real PS2.

## Running it

1. Get a game folder: either from a release archive (none published yet) plus your own `data.zip`, or by building it from source.
2. Copy the folder to a FAT32 (MBR) USB stick.
3. Start `mass:/VVVVVV/VVVVVV.elf` from wLaunchELF on a PS2 that runs homebrew (FMCB or PS2BBL).

Step-by-step instructions, controls and troubleshooting:
[docs/installing.md](docs/installing.md).

Building from source needs Linux x86-64 with bubblewrap or Docker:

```sh
git clone https://github.com/MarvinParanoid/VVVVVV-PS2.git
cd VVVVVV-PS2
scripts/build-sdl2-ps2.sh
scripts/build-vvvvvv-ps2.sh --refetch            # -> out/vvvvvv-ps2/VVVVVV.elf
scripts/make-usb-package.sh /path/to/data.zip    # -> build/usb-package/VVVVVV/
```

More: [docs/building.md](docs/building.md).

## Game data

The game's graphics, sounds and music are **not** included. Use `data.zip`
from your own copy of VVVVVV:
- from the game's install folder (Steam, GOG, itch.io and similar);
- or, for personal use, from the free [Make and Play Edition](https://thelettervsixtim.es/makeandplay/).

If you enjoy the game, please buy it at [thelettervsixtim.es](https://thelettervsixtim.es).
Never share `data.zip` or a prepared game folder.

## Limitations

- NTSC 60 Hz output only, also on PAL consoles.
- USB storage only; no memory-card saves.
- English only.
- The Tower and the Panic Room run at about 85% speed in PCSX2; not yet measured on the console.

The full list: [docs/known-issues.md](docs/known-issues.md).

## Documentation

| Document | For |
|---|---|
| [docs/installing.md](docs/installing.md) | putting the game on a stick, launching, controls, reporting results |
| [docs/known-issues.md](docs/known-issues.md) | compatibility, limitations, open questions |
| [docs/building.md](docs/building.md) | building, tests, PCSX2, reproducibility |
| [docs/architecture.md](docs/architecture.md) | how the port works |
| [docs/releasing.md](docs/releasing.md) | release candidates, CI, release gates |
| [CONTRIBUTING.md](CONTRIBUTING.md) | hardware reports and code changes |
| [docs/archive/](docs/archive/README.md) | phase reports and earlier plans (historical) |

## License and credits

This is an altered, unofficial version of VVVVVV. It is not affiliated with
or endorsed by Terry Cavanagh or Sony, and it is non-commercial.

The repository has mixed licensing: the VVVVVV patches use the VVVVVV Source
Code License (non-commercial), the SDL and PhysicsFS patches use zlib, and the
tooling and documentation use MIT. See [LICENSE.md](LICENSE.md), including the
terms for built binaries. No game assets are included, and `data.zip` must not
be redistributed. Open licensing questions for binary releases are tracked in
[docs/license-compliance-matrix.md](docs/license-compliance-matrix.md).

Credits:
- **VVVVVV** by [Terry Cavanagh](https://distractionware.com), with music by Magnus Pålsson (SoulEye). The VVVVVV source code is © Terry Cavanagh.
- **[SDL](https://libsdl.org)** and its PS2 port by the SDL and ps2dev contributors.
- **[ps2dev](https://github.com/ps2dev)**: the toolchain, ps2sdk, gsKit and ps2sdk-ports.
- **VVVVVV's libraries:** [PhysicsFS](https://icculus.org/physfs/), [FAudio](https://github.com/FNA-XNA/FAudio), [stb_vorbis](https://github.com/nothings/stb), tinyxml2, lodepng, SheenBidi and c-hashmap.
- **Tools:** [PCSX2](https://pcsx2.net), the reference emulator for this work; [Play!](https://purei.org) for supplementary checks.
- **Prior art:** the VVVVVV ports to Dreamcast ([VVVVVVDC](https://github.com/gusarba/VVVVVVDC)) and other constrained platforms were studied for design ideas. No code was taken from them.

Development uses AI-assisted coding tools, with builds and behavior validated
through automated tests, PCSX2 and sessions on a real PS2.

"PlayStation" and "PS2" are trademarks of Sony Interactive Entertainment.
