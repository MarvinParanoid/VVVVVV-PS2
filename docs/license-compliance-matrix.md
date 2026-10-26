# Licence compliance matrix for binary releases

Status: draft, 2026-10-26. This is not legal advice. It records what each component's licence permits for a **non-commercial, asset-free release of the PS2 ELF files**, and what would need the author's approval.

The binary in question is `VVVVVV.elf`: statically linked, built in ps2dev image `sha256:5476fdfb…` (2026-10-13). Linkage was checked with `nm` on the H3 build (`66f0568e…`).

## Matrix

| Component (how it gets into the ELF) | Licence (source checked) | Permits the asset-free binary release? | Obligations | Needs approval / open |
|---|---|---|---|---|
| **VVVVVV game code** with this project's patches 0002–0005 | VVVVVV Source Code License v1.0 (`LICENSE.md` at upstream `79395fa`) | **Yes, by the licence text**: "permission is granted … to alter it and redistribute it freely", non-commercial only | Non-commercial; acknowledge Terry Cavanagh and that it is based on the VVVVVV source; mark as altered/unofficial; keep the notice | The upstream README says distributing a compiled version "might need permission first". `License exceptions.md` (2024-01-23) lists no PS2 port. Written permission is not required by the licence text, but asking is recommended **(user decision)** |
| **Original game assets** (`data.zip`: graphics, sound, music) | Proprietary; licence clause "must not distribute any materials from the game which are not included in this repo unless approved by us in writing" | **No** | Never shipped; users supply their own `data.zip` | Shipping assets needs written approval from Terry Cavanagh. Exception: a **Make and Play** build (`MAKEANDPLAY` define, no original levels) may ship the assets (`License exceptions.md`) |
| `lang/`, `fonts/` from the VVVVVV repository | Part of the VVVVVV repository (same licence as the code) | Yes (shipped since H2-TEST1) | Same as the game code | — |
| **SDL 2.32.10** + patches 0001–0010 | zlib (`LICENSE.txt`) | Yes | Do not misrepresent the origin; mark altered source | — |
| **PhysicsFS** + patch 0001 | zlib | Yes | As above | — |
| FAudio, tinyxml2, lodepng | zlib | Yes | Keep the notices (already in `THIRD-PARTY-NOTICES.txt`) | — |
| c-hashmap | BSD-3-Clause | Yes | Notice and disclaimer in the documentation | — |
| SheenBidi | Apache-2.0 | Yes | Licence text; NOTICE file if present | Check for an upstream `NOTICE` file |
| stb_vorbis (in FAudio) | Public domain or MIT | Yes | — | — |
| **ps2sdk** EE libraries (libkernel, libcglue, fileXio, audsrv, libpadx, libmtap, libdebug/libeedebug, libpatches) and the **IRX modules** embedded in the ELF (USB, BDM, FAT, audio, pad, memory card, HDD) | Academic Free License 2.0 (`ps2sdk/LICENSE`); some modules carry other licences | Yes | Attribution notice; AFL text | Per-module licences were not audited one by one |
| **FatFs** (inside `bdmfs_fatfs.irx`) | ChaN FatFs licence (1-clause BSD style; `LICENSE.txt` of fjtrujy/FatFs `iop-r0.16`, the branch ps2sdk fetches) | Yes | None for binaries: the licence says binary redistribution does not need to mention FatFs. The text is included anyway (`LICENSES/FatFs.txt`) | — |
| **gsKit** (libgskit, libdmakit) | AFL 2.0 | Yes | As ps2sdk | — |
| **ps2_drivers 2.0.0** (`libps2_drivers.a`; `init_ps2_filesystem_driver`, `init_audio_driver`, `init_joystick_driver`, `waitUntilDeviceIsReady` are in the ELF; pulled in by SDL's PS2 backend) | **Contradictory:** the `LICENSE` file at tag `2.0.0` is **GPL-3.0**; the README and the source headers say **LGPL v2** ("GNU Library General Public License version 2") | **Unresolved; blocker.** If it is GPL-3.0, the combined work would have to be GPL-3.0, which conflicts with the VVVVVV licence's non-commercial clause: the binary cannot be distributed. If it is LGPL v2, static linking is allowed with relinking obligations (below) | LGPL: licence text; source of the library; let users relink (provide the complete source and build scripts, which this repository already does) | **Ask the author (fjtrujy) which licence applies (user's action).** Alternative: replace the small amount of glue we use (IRX loading and device wait) with this project's own code on ps2sdk (AFL) and drop `libps2_drivers`. Not yet assessed |
| **pthread-embedded** (`libpthread.a`; `pte_*` and `pthread_mutex_*` in the ELF, used by PhysicsFS) | **LGPL 2.1.** The branch the toolchain builds (`platform_agnostic`, per ps2toolchain-ee's config) has only `COPYING` (pthreads-win32, LGPL 2.1); the MIT `LICENSE` exists only on `master` | Yes, with the LGPL 2.1 obligations | Licence text and source pointer (in the `--release` archive: `LICENSES/LGPL-2.1.txt`, `LICENSES/pthread-embedded-COPYING.txt`, `THIRD-PARTY-NOTICES.txt`); relinking via the public source of this port and its build scripts | — |
| newlib (libc, libm) | Collection of BSD-style licences (`COPYING.NEWLIB` of `ps2dev/newlib` `ee-v4.6.0`, the toolchain's default ref; the repository also contains GPL-2.0 tools that are not linked) | Yes | Notices: the full `COPYING.NEWLIB` is included in the `--release` archive | Whether the image's newlib is exactly `ee-v4.6.0` was not verified |
| GCC runtime (libgcc, libstdc++) | GPL-3.0 with the GCC Runtime Library Exception 3.1 | Yes (eligible compilation with GCC) | None beyond the exception's conditions | — |
| This project's tooling (`scripts/`, `tests/`, `docker/`, docs) | MIT | Yes | Notice | Copyright holder is still the placeholder "VVVVVV-PS2 contributors" (user decision) |

## ps2_drivers 2.0.0 investigation (2026-10-26, read-only)

What reaches the ELF:
- `libps2_drivers.a` is a merged archive (`ar -M` in its CMakeLists). It contains:
  - the project's own driver objects: `init_*`/`deinit_*` for the file system, audio, joystick, USB, BDM, fileXio, memory card, sio2man, cdfs, dev9, HDD, mx4sio and poweroff, plus `waitUntilDeviceIsReady`;
  - the IRX module descriptors;
  - 20 embedded IRX blobs (381,600 bytes) converted from `${PS2SDK}/iop/irx/*.irx` with ps2sdk's `bin2c`, so ps2sdk binaries;
  - ps2sdk client libraries merged in: fileXio, mtap, padx, audsrv, poweroff and others.
- The blobs are ps2sdk binaries, not ps2_drivers code.

Licence evidence (fjtrujy/ps2_drivers):
- **`LICENSE` (GPL-3.0):** added in commit `87f3cab`, "Initial commit", 2022-06-16. This is GitHub's repository-creation template commit. The file is unchanged since.
- **Source headers:** "Licenced under GNU Library General Public License version 2 … Review ps2sdk README & LICENSE", in every driver `.c`/`.h` since the first code commit `3911696` (2022-06-17). The text is byte-identical to the header of ps2sdk's audsrv. Newer files (IRX descriptors, CMake, tools) have no header.
- **README:** the line "Licensed under GNU Library General Public License version 2. See LICENSE file for details." was added in `18a3150`, 2025-12-12. It points at the GPL-3.0 file.
- **Tag:** `2.0.0` = `ec605e2` (2026-09-28). Main author: GitHub user `fjtrujy` (ps2dev organisation member); 4 other contributors.
- **Existing discussion:** no licence issue exists.
- **Inference, not proven:** the GPL-3.0 file was chosen in GitHub's template when the repository was created, and the code headers were copied from ps2sdk's audsrv. Until the author confirms, the licence is **unresolved**, and no LGPL permission is assumed.
- **Asked:** issue [fjtrujy/ps2_drivers#27](https://github.com/fjtrujy/ps2_drivers/issues/27), filed 2026-10-26; no answer yet.
- **Where to ask:** an issue on github.com/fjtrujy/ps2_drivers, asking which licence applies to the 2.0.0 sources (or for a relicence to AFL-2.0/LGPL to match ps2sdk). Secondary: the ps2dev Discord. This is the user's action.

**Side finding:** ps2sdk's own **audsrv** (`audsrv_rpc` on the EE and `audsrv.irx`, both in the ELF) is labelled LGPL v2, not AFL. Its relinking obligations apply whatever happens with ps2_drivers.

**Replacement estimate (inference):**
- SDL and this project only call `init/deinit_audio_driver`, `init/deinit_joystick_driver`, `init/deinit_ps2_filesystem_driver` and `waitUntilDeviceIsReady`.
- A clean reimplementation on ps2sdk means:
  - loading the needed IRX from embedded buffers (`SifExecModuleBuffer`, our own `bin2c` objects) in the right order;
  - calling `padInit`/`mtapInit`/`audsrv_init`/`fileXioInit`;
  - waiting for the boot device.
- Size and time: about 300–450 lines of C plus CMake, 1–2 days to write and 2–4 days to validate in PCSX2 and on hardware. SDL must then be rebuilt against it: either a drop-in library with the same symbols, or an SDL patch.
- Clean-room: the implementer should not use ps2_drivers' `src/` as a reference.
- Risks: module order and arguments, boot-device timing on hardware, poweroff behaviour.

## What this means for H4

1. **Blocker (new):** the `ps2_drivers` licence must be clarified before any binary is published. H2-TEST1 was never published, so nothing has been distributed yet.
2. **Done in the `--release` packaging (2026-10-26):** `THIRD-PARTY-NOTICES.txt` and `LICENSES/` carry LGPL 2.0 (audsrv) and 2.1 (pthread-embedded) texts with source pointers, the FatFs licence and `COPYING.NEWLIB`, and state the ps2_drivers item as unresolved.
3. **Existing permission:** the VVVVVV licence text already permits a non-commercial, altered, asset-free binary release with the required notices. Approval is needed only for the assets, or if the user wants explicit consent as the upstream README suggests.
4. **Never without written approval:** game assets (except a future Make and Play build under its general exception).

## Sources checked

- VVVVVV `LICENSE.md`, `README.md`, `License exceptions.md` at `79395fa` (local pristine export).
- ps2sdk `LICENSE` in the toolchain image.
- `ps2sdk-ports/build-cmakelibs.sh`: fetches `fjtrujy/ps2_drivers` tag `2.0.0`.
- `fjtrujy/ps2_drivers` `LICENSE`, `README.md` and `src/ps2_filesystem_driver.c` at `2.0.0`.
- `ps2dev/pthread-embedded` `LICENSE` and `COPYING` (master).
- `nm` of the H3 ELF.

Not checked:
- per-module headers of every IRX;
- the exact newlib parts linked;
- SheenBidi `NOTICE`.
