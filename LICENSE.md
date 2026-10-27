# Licensing

This repository does **not** have a single license. Different parts are
derived from different upstream projects, and each part keeps the terms of
the project it modifies. The full license texts are in [`LICENSES/`](LICENSES/).

| Path | What it is | License |
|---|---|---|
| `patches/vvvvvv/0002-*` … `0005-*` | Changes to the VVVVVV source code, including the new PS2 files they add (`PS2Platform.*`, `PS2Bench.*`, `PS2Diag.*`, music streaming) | **VVVVVV Source Code License v1.0** ([copy](LICENSES/VVVVVV-Source-Code-License.md)). Non-commercial; see below |
| `patches/vvvvvv/0001-physfs-ps2-platform.patch` | Changes to PhysicsFS (`third_party/physfs` in the VVVVVV repository) | zlib ([text](LICENSES/Zlib.txt)), as PhysicsFS |
| `patches/sdl2/` | Changes to SDL 2.32.10 | zlib ([text](LICENSES/Zlib.txt)), as SDL |
| `scripts/`, `tests/`, `docker/`, `docs/`, repository files | Original build, test and packaging tooling and documentation | MIT ([text](LICENSES/MIT.txt)) |

## VVVVVV

VVVVVV is copyright © 2010-2020 Terry Cavanagh. This project is **based on the
VVVVVV source code** (<https://github.com/TerryCavanagh/VVVVVV>) and is an
**altered, unofficial version**. It is not affiliated with or endorsed by
Terry Cavanagh.

The VVVVVV Source Code License v1.0 applies to the VVVVVV patches. In short:
- no commercial use;
- keep the origin and the notice;
- mark altered versions as altered;
- **do not distribute the game's assets.**

The patches contain no game assets.

**VVVVVV is a commercial game.** The graphics, sounds and music (`data.zip`)
are proprietary and are **not** included here. You must supply your own
`data.zip`; see [docs/installing.md](docs/installing.md#game-data).

## Third-party components used by the build (not included here)

The build downloads these at pinned versions. None of their source is stored
in this repository. They are linked into the PS2 ELF files, so their notices
apply to anyone who **distributes built binaries**:

| Component | License |
|---|---|
| SDL 2.32.10 | zlib |
| PhysicsFS, FAudio, tinyxml2, lodepng (VVVVVV `third_party/`) | zlib |
| c-hashmap (VVVVVV `third_party/`) | BSD-3-Clause |
| SheenBidi (VVVVVV `third_party/`) | Apache-2.0 |
| stb_vorbis (inside FAudio) | public domain / MIT |
| ps2sdk, gsKit (ps2dev toolchain image) | Academic Free License 2.0, with some components under other licenses (e.g. newlib, FatFs) |
| GCC runtime (libgcc, libstdc++) | GPL-3.0 with the GCC Runtime Library Exception |

PCSX2 (GPL-3.0) and the ps2dev Docker image are only **used** as tools; they are
not part of the build output.

## Tester releases (binaries)

Tester releases (`scripts/make-public-test-release.sh`) ship **compiled PS2 ELF files and no game data**:
- **No game assets.** `data.zip`, `data-ps2.zip`, graphics, sounds and music are never included. Each tester converts their own `data.zip` locally with `tools/prepare-ps2-usb.py`.
- **What the release does include:**
  - the VVVVVV licence text (`LICENSE-VVVVVV.md`) and the acknowledgement that VVVVVV is © Terry Cavanagh and that this is an altered, unofficial version;
  - the notices of the libraries linked into the ELFs (`THIRD-PARTY-NOTICES.txt`);
  - the open-source `lang/` and `fonts/` files from the VVVVVV repository.
- **Basis.** The VVVVVV Source Code License v1.0 permits non-commercial redistribution of altered versions, under the conditions above. The VVVVVV README adds that distributing a compiled version "might need permission first". This project has **no** written permission or licence exception from Terry Cavanagh. An informal discussion about a general exception for non-commercial ports is **not** an enacted exception and is not relied on here. Releases are non-commercial and clearly marked unofficial. **Review this before publishing a release.**
- **Notices.** `THIRD-PARTY-NOTICES.txt` and `LICENSES/` in each archive carry the full texts for newlib, FatFs and the LGPL components (pthread-embedded, audsrv); the GCC runtime is referenced by link. Open compliance items are listed in part B of `THIRD-PARTY-NOTICES.txt` and in [docs/license-compliance-matrix.md](docs/license-compliance-matrix.md).

"PlayStation" and "PS2" are trademarks of Sony Interactive Entertainment. This
project is not affiliated with or endorsed by Sony.
