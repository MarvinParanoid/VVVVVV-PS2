# VVVVVV for PlayStation 2

An unofficial, non-commercial homebrew port of Terry Cavanagh's
[VVVVVV](https://github.com/TerryCavanagh/VVVVVV) to the PlayStation 2, built
from the open-source VVVVVV code with SDL2 and the [ps2dev](https://github.com/ps2dev) toolchain.
It is playable in the PCSX2 emulator. **It has not yet been tested on a real PS2.**

The game's graphics, sounds and music are not included. You need `data.zip`
from your own copy of VVVVVV; if you enjoy the game, please buy it at
[thelettervsixtim.es](https://thelettervsixtim.es).

## Status

Working in PCSX2:
- the full game in English, including time trials, the Secret Lab and the Super Gravitron;
- music and sound effects;
- DualShock 2 controls;
- saves to the USB stick;
- diagnostic tools for hardware testing: start-up and error screens, a performance overlay, a benchmark and a USB self-test.

Main limitations:
- Nothing has been checked on a console yet. PCSX2 does not emulate the PS2's caches, so a real PS2 may be slower.
- The most demanding scenes (the Tower, the Panic Room) run at about 85% speed in PCSX2.
- Video is NTSC (60 Hz) only, also on PAL consoles.
- Saves go to the USB stick; there are no memory-card saves.

The full compatibility table and the other limitations are in [docs/installing.md](docs/installing.md#compatibility).

## Getting started

No tester release has been published yet. Once one is, it will appear on the
[Releases](https://github.com/MarvinParanoid/VVVVVV-PS2/releases) page as a zip
without game data, together with a small Python script that adds the data from
your own `data.zip`. No development tools will be needed. Until then, the game
has to be [built from source](#building-from-source).

In short:
1. Take `data.zip` from your copy of VVVVVV (Steam, GOG, itch.io; or the free Make and Play edition for personal use).
2. Prepare the game folder from it and copy the folder to the root of a FAT32 (MBR) USB stick.
3. On a PS2 that runs homebrew (FMCB or PS2BBL with wLaunchELF), run `mass:/VVVVVV/fs_smoke.elf` first, then `mass:/VVVVVV/VVVVVV.elf`.

Step-by-step instructions, controls and troubleshooting: [docs/installing.md](docs/installing.md).
A printable checklist for a first session: [docs/ps2-hardware-first-boot.md](docs/ps2-hardware-first-boot.md).

### Testing on real hardware

A report from a real console is the most useful contribution right now, and
failures are as useful as successes. Please open an issue with the
**Hardware test report** template. Never attach game data; see
[CONTRIBUTING.md](CONTRIBUTING.md).

## Building from source

On Linux x86-64 with [bubblewrap](https://github.com/containers/bubblewrap)
(or Docker, untested). The scripts download a pinned ps2dev toolchain image,
SDL 2.32.10 and VVVVVV, and apply this repository's patches:

```sh
git clone https://github.com/MarvinParanoid/VVVVVV-PS2.git
cd VVVVVV-PS2
scripts/build-sdl2-ps2.sh
scripts/build-vvvvvv-ps2.sh --refetch            # -> out/vvvvvv-ps2/VVVVVV.elf
scripts/make-usb-package.sh /path/to/data.zip    # -> build/usb-package/VVVVVV/
```

Requirements, reproducible builds and running in PCSX2: [docs/building.md](docs/building.md).
Changing the patches: [CONTRIBUTING.md](CONTRIBUTING.md).

## Documentation

- [Installing and running on a PS2](docs/installing.md)
- [Hardware test plan](docs/ps2-hardware-test-plan.md)
- [Building from source](docs/building.md)
- [Technical overview](docs/technical-overview.md): how the port works
- [Diagnostics overlay](docs/ps2-hardware-diagnostics.md)
- [All documents and the development history](docs/README.md)

## License and credits

This is an altered, unofficial version of VVVVVV. It is not affiliated with or
endorsed by Terry Cavanagh or Sony, and it is non-commercial.

The repository has mixed licensing: the VVVVVV patches use the VVVVVV Source
Code License (non-commercial), the SDL and PhysicsFS patches use zlib, and the
tooling and documentation use MIT. See [LICENSE.md](LICENSE.md) for details,
including the terms for built binaries. No game assets are included, and
`data.zip` must not be redistributed.

Credits:
- **VVVVVV** by [Terry Cavanagh](https://distractionware.com), with music by Magnus Pålsson (SoulEye). The VVVVVV source code is © Terry Cavanagh.
- **[SDL](https://libsdl.org)** and its PS2 port by the SDL and ps2dev contributors.
- **[ps2dev](https://github.com/ps2dev)**: the toolchain, ps2sdk, gsKit and ps2sdk-ports.
- **VVVVVV's libraries:** [PhysicsFS](https://icculus.org/physfs/), [FAudio](https://github.com/FNA-XNA/FAudio), [stb_vorbis](https://github.com/nothings/stb), tinyxml2, lodepng, SheenBidi and c-hashmap.
- **Tools:** [PCSX2](https://pcsx2.net), the reference emulator for this work; [Play!](https://purei.org) for supplementary checks.
- **Prior art:** the VVVVVV ports to Dreamcast ([VVVVVVDC](https://github.com/gusarba/VVVVVVDC)) and other constrained platforms were studied for design ideas. No code was taken from them.

Development uses AI-assisted coding tools, with builds and behavior validated
through automated tests and PCSX2.

"PlayStation" and "PS2" are trademarks of Sony Interactive Entertainment.
