# VVVVVV Dreamcast port: technical analysis for the PS2 port

**Date:** 2026-10-22

**Status:** research only; no PS2 code was changed.

**Companion report:** [vvvvvv-cross-platform-port-analysis.md](vvvvvv-cross-platform-port-analysis.md), which covers the cross-platform comparison and the P2b recommendation.

**Evidence labels:**
- **[V] verified** in source at the commit below.
- **[A] assumption.**

Permalink prefix **R/** = `https://github.com/gusarba/VVVVVVDC/blob/47222b05f763b6b31e960035fb9e1c5d5fe52890/`

## 1. Repository and status [V]

| Item | Value |
|---|---|
| Repository | https://github.com/gusarba/VVVVVVDC, branch `dreamcast` (default) |
| Revision examined | `47222b05f763b6b31e960035fb9e1c5d5fe52890` (2020-12-24) |
| Maintenance | 14 commits by the author (2020-08-11 to 2020-12-24); no activity since |
| Upstream base | TerryCavanagh/VVVVVV `4511ea1` (2020-04-06, pre-2.3). This is much older than our 2.4 base: no FAudio, no `SDL_Renderer` targets, no over30mode |
| Shape | Full fork. Platform code lives in `*_dreamcast.cpp` files plus `#ifdef DREAMCAST` blocks. It vendors SDL 1.2.13 (BERO/Chui DC SDL, 1,212 files) under `desktop_version/dc/SDL-1.2.13/` |
| History caveat | The real code arrived in two squashed commits (`0be4392`, `a032a72`). Earlier experiments are only visible as commented-out or `#if 0` code |
| License | VVVVVV Source Code License v1.0 (unchanged). Vendored SDL 1.2 is LGPL 2.1; DreamHAL mem* routines are public domain; the PhysFS DC platform is zlib (inside PhysFS) |
| Upstream permission | `License exceptions.md` "Dreamcast Port": "a ready-to-use CD image … containing the data.zip assets for non commercial use only" (added upstream in `76b326a`). **There is no PS2 entry.** |

## 2. Architecture overview [V]

**Main changes from upstream:**
- SDL2 API calls are backported to SDL 1.2 through `src/SDL2_stub.h`.
- Rendering is 16-bit RGB565 software blitting with colour keys instead of alpha.
- Music plays from CD-DA audio tracks.
- SFX are KOS `snd_sfx` samples in AICA sound RAM.
- PhysicsFS uses a KOS platform layer.
- Saves go to the VMU.
- `Music.cpp` is **not compiled**; the Makefile links `Music_dreamcast.o` instead ([R/desktop_version/dc/Makefile#L83](https://github.com/gusarba/VVVVVVDC/blob/47222b05f763b6b31e960035fb9e1c5d5fe52890/desktop_version/dc/Makefile#L83)).

## 3. Music and memory

| Question | Answer |
|---|---|
| Is the original 61.7 MB music archive supported at runtime? | **No.** The blob is never loaded. Music was moved to CD-DA tracks [V] |
| Streamed or in RAM? | Streamed by the GD-ROM drive as CD-DA. **Zero main RAM, zero CPU decode** [V] |
| Format and decoder | Offline: `dc/ogg/vvvvvv_music_unpacker.py` splits the blob by scanning `OggS` pages, decodes each track with PyOgg `VorbisFile`, and writes headerless 16-bit PCM `0.raw`…`15.raw` ([R/desktop_version/dc/ogg/vvvvvv_music_unpacker.py#L27](https://github.com/gusarba/VVVVVVDC/blob/47222b05f763b6b31e960035fb9e1c5d5fe52890/desktop_version/dc/ogg/vvvvvv_music_unpacker.py#L27)). Then `mds4dc` puts them on the disc as 16 audio tracks before the data session ([Makefile#L164](https://github.com/gusarba/VVVVVVDC/blob/47222b05f763b6b31e960035fb9e1c5d5fe52890/desktop_version/dc/Makefile#L164)) [V] |
| Playback API | `cdrom_cdda_play(track+1, track+1, repeat, CDDA_TRACKS)` with song→track mapping `t % 16`. Volume uses `spu_cdda_volume(l,r)`, which has 16 steps ([R/desktop_version/src/Music_dreamcast.cpp#L335](https://github.com/gusarba/VVVVVVDC/blob/47222b05f763b6b31e960035fb9e1c5d5fe52890/desktop_version/src/Music_dreamcast.cpp#L335), [#L315](https://github.com/gusarba/VVVVVVDC/blob/47222b05f763b6b31e960035fb9e1c5d5fe52890/desktop_version/src/Music_dreamcast.cpp#L315)) [V] |
| Looping | CD-DA repeat count; no loop points (the stock tracks have none) [V] |
| Fades and track switching | Software fades over the 16 CD-DA volume steps in `processmusic()`, assuming 33 ms/frame. Fade-out ends with `cdrom_cdda_pause()`, then the queued song starts (`nicefade` / `fadeoutqueuesong` state machine kept from upstream) ([#L431](https://github.com/gusarba/VVVVVVDC/blob/47222b05f763b6b31e960035fb9e1c5d5fe52890/desktop_version/src/Music_dreamcast.cpp#L431)) [V] |
| Why not Ogg? | README: "Numerous tests were performed streaming and decoding music from the blob, but it was too slow and choppy". **No Ogg or Vorbis decoding code exists in the history** (`git log -G vorbis\|ogg\|stb_`), so the test conditions (decoder, bitrate, buffering) are unknown [V the claim; A its cause] |
| SFX | Final: KOS `snd_sfx_load` (WAV into AICA sound RAM, not main RAM) plus `snd_sfx_play` ([R/desktop_version/src/SoundSystem_dreamcast.cpp#L184](https://github.com/gusarba/VVVVVVDC/blob/47222b05f763b6b31e960035fb9e1c5d5fe52890/desktop_version/src/SoundSystem_dreamcast.cpp#L184)). `rescue.wav` (769 KB PCM) is replaced by a pre-encoded Yamaha ADPCM `rd/rescueA.wav` (192 KB) on the romdisk. Earlier `snd_stream` and OpenAL (ALdc) SFX attempts are left as `#if 0` [V] |
| Audio threads and buffers | None in the final code. AICA plays SFX autonomously and the drive streams CD-DA. `SDL_INIT_AUDIO` is commented out [V] |
| Peak RAM | **Not measured.** `dc/memory_stats.cpp` prints `16 MB − (mallinfo().usmblks + image)`, but no numbers are recorded anywhere [V] |
| How it fits in 16 MB | Main contributors [V]: (1) no music blob in RAM; (2) SFX in sound RAM; (3) all images converted to RGB565 without alpha, half of ARGB8888 ([R/desktop_version/src/GraphicsResources_dreamcast.cpp#L83](https://github.com/gusarba/VVVVVVDC/blob/47222b05f763b6b31e960035fb9e1c5d5fe52890/desktop_version/src/GraphicsResources_dreamcast.cpp#L83)); (4) data.zip extracted onto the disc and mounted as a directory (no zip inflate state); (5) six 320×240×2 screen buffers (~900 KB) |

### Comparison with our PS2 music path

| | Dreamcast | PS2 (P2a) |
|---|---|---|
| Music source | CD-DA audio tracks, pre-decoded offline | `vvvvvvmusic.vvv` inside data.zip (deflated) |
| Decoder | Drive hardware | stb_vorbis (float) inside FAudio callbacks on the SDL audio thread |
| Music in RAM | 0 | Disabled in P2a: the whole blob plus a copy per track would exceed 32 MB |
| SFX | AICA RAM (`snd_sfx`) | 28 WAVs as PCM in EE RAM, mixed by FAudio (float), output via audsrv |
| Fades | CD-DA volume, 16 steps | FAudio voice volume (continuous) |

**Transferability:** CD-DA is not transferable. Our builds run from USB (`mass:`) or `host:`, not from a mixed-mode CD, and a PS2 DVD-ROM game has no CD-DA. The **principle** transfers: do not hold the music blob in RAM, and play one track at a time from storage.

## 4. Important code-level findings (secondary topics) [V]

| Area | Finding |
|---|---|
| **Rendering** | `SDL_SetVideoMode(320,240,16,HW\|DOUBLEBUF)`: native 320×240 RGB565, no CPU scaling; the RAM framebuffer is copied to VRAM by PVR DMA per flip ([R/desktop_version/src/Screen_dreamcast.cpp#L83](https://github.com/gusarba/VVVVVVDC/blob/47222b05f763b6b31e960035fb9e1c5d5fe52890/desktop_version/src/Screen_dreamcast.cpp#L83)) |
| **Alpha** | Replaced by colour keys plus SDL 1.2 RLE (README: "dramatic impact on performance"). Side effects: the translucent room-name bar becomes opaque, and **pixel-perfect collision (`Hitest`) degrades to rectangles**, because RGB565 has no alpha ([R/desktop_version/src/Graphics.cpp#L1250](https://github.com/gusarba/VVVVVVDC/blob/47222b05f763b6b31e960035fb9e1c5d5fe52890/desktop_version/src/Graphics.cpp#L1250)) |
| **Tinting** | No cache. Every tinted sprite or glyph allocates a temporary surface, recolours it pixel by pixel, blits it and frees it ([R/desktop_version/src/GraphicsUtil_dreamcast.cpp#L286](https://github.com/gusarba/VVVVVVDC/blob/47222b05f763b6b31e960035fb9e1c5d5fe52890/desktop_version/src/GraphicsUtil_dreamcast.cpp#L286)). This is a counter-example to copy |
| **memcpy** | DreamHAL SH4 memcpy is patched into SDL's blit copy and `SDL_memcpy`; game code does not use it directly. SH4-specific |
| **Filesystem** | PhysFS platform over KOS `fs_*` ([R/third_party/physfs/physfs_platform_dreamcast.c](https://github.com/gusarba/VVVVVVDC/blob/47222b05f763b6b31e960035fb9e1c5d5fe52890/third_party/physfs/physfs_platform_dreamcast.c)); base/pref/write dir is `/ram/`; assets come from `/cd/` |
| **Saves** | XML is written to ramfs (directory parts stripped), zlib-compressed into a fixed 1,536-byte buffer, and stored as one VMU file with an icon. **Bugs:** silent truncation and an unchecked `memcpy` ([R/desktop_version/src/FileSystemUtils_dreamcast.cpp#L78](https://github.com/gusarba/VVVVVVDC/blob/47222b05f763b6b31e960035fb9e1c5d5fe52890/desktop_version/src/FileSystemUtils_dreamcast.cpp#L78)) |
| **Input** | SDL 1.2 joystick events; GameController button names are macro-mapped to joystick buttons; the hat is mapped to the D-pad |
| **Main loop** | Upstream 2.2 fixed 34 ms step, no interpolation. README claims "30/40 FPS"; there are no measurements in the repo |
| **Build** | KOS Makefile, `-O3 -ffast-math -fomit-frame-pointer`. `fullmds` target: data ISO plus 16 CD-DA tracks (MDS/MDF) |

## 5. Reusable for PS2

Ranked by value.

1. **Never load the music blob into RAM; play one track at a time from storage.** Principle [V]; already our plan.
2. **Offline preprocessing of user-supplied assets is acceptable practice** for console ports (DC: PyOgg → CD-DA). For PS2 the analogue is the lossless container repack measured below. Note: the DC tool is Python 3 + PyOgg; we only borrow the idea, not the code.
3. **SFX in sound-chip RAM** (AICA ↔ SPU2) would remove FAudio mixing and EE RAM for SFX. On PS2, audsrv's ADPCM API supports exactly this for short samples. A medium-size change; out of scope for P2b.
4. 16-bit surfaces and colour keys: **do not copy**. Our 2.4 path keeps alpha and pixel-perfect collision and is already fast enough in PCSX2.
5. Per-call tinted-surface allocation: **do not copy**; our patch 0002 fast path is the better approach.

## 6. Does the Dreamcast approach change our P2b plan?

**No.** It confirms two things:
- the whole-blob approach must go;
- decoding from a **deflated** container is the wrong place to stream from.

The DC author's "too slow and choppy" agrees with our EE measurement (cross-platform report §5): reaching a track inside the deflated blob costs 2.6–7.6 s.

CD-DA itself is not available to us. Our measurements show the EE can afford Vorbis decoding (~9% of one core in PCSX2). So the plan becomes: stream the original Vorbis from a **stored** (uncompressed) blob entry. Details are in the companion report.
