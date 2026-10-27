# VVVVVV → PlayStation 2: Technical Feasibility Audit (Phase 1)

Status: **audit only.** No game code was modified. Nothing was compiled or run.
Date: 2026-10-21

---

## 0. Executive summary

**Verdict: feasible, but not as a plain recompile.** A PS2 ELF of VVVVVV can be built with
the stock PS2DEV toolchain and the SDL2 port in `ps2sdk-ports`. Getting it to *run correctly*
needs three non-trivial pieces of work:

1. **Rendering.** The game builds every frame out of render-target textures. The SDL PS2
   renderer advertises render-target support but implements `SetRenderTarget` as an empty
   function, in both SDL2 and SDL3. Unpatched, the game will render garbage. The recommended
   workaround needs no changes to the game renderer: use SDL's *software* renderer, which
   implements targets, and present through SDL's built-in "texture framebuffer" fallback on
   top of the gsKit renderer. Its CPU cost on the 294 MHz EE has not been measured.
2. **RAM (32 MB).** The music blob is held in RAM for the whole run, and every track is
   copied a second time. On top of that, 2.4.4 has an audio-buffer bug that inflates RAM use.
   The music loader needs a small, isolated change (lazy or streamed track loading), and the
   port should be based on `2.4-updates`, not on the 2.4.4 tag.
3. **Platform glue.**
   - PhysicsFS has no PS2 platform and hits `#error Unknown platform`.
   - `FileSystemUtils.cpp` does not compile without `__unix__`.
   - Save and data paths, controller glyphs, the keyboard-only editor, and display scaling
     need PS2 defaults.

   Each of these is a small, isolatable change.

**Licensing is a separate open issue.**
- Personal builds are clearly allowed.
- Distributing a binary looks possible only as a **Make and Play** build, or with explicit
  permission from Terry Cavanagh. PS2 is not on the exceptions list.
- SDL2-PS2 statically links `ps2_drivers`, whose `LICENSE` file is **GPL-3.0** while its
  README says LGPL-2. GPL-3.0 may be incompatible with VVVVVV's non-commercial source license.

---

## 1. Scope, method and evidence levels

### 1.1 How this audit was done
- **Source only.** The host machine (Arch Linux) has **no Docker, no PS2DEV toolchain and no
  PCSX2 installed**, so nothing was compiled or executed. Every statement about
  compatibility comes from reading source code.
- **The project directory was empty** (it held no source).
  The upstream repositories were cloned into a temporary scratch directory and audited there.
  Nothing was vendored into this repo.

### 1.2 Evidence labels used throughout
- **[V] Verified.** Read directly in source at the commits listed in §1.3.
- **[I] Inference.** Reasoned from verified code but not executed. Must be confirmed in PCSX2
  and then on hardware.
- **[A] Assumption.** Typically asset dimensions or sizes. The assets are proprietary, are not
  in the repo, and were not inspected.

> No statement in this document claims runtime compatibility. Even "Supported" in the API
> matrix means only that the code path exists in source.

### 1.3 Audited revisions

| Component | Ref | Commit |
|---|---|---|
| VVVVVV tag `2.4.4` (SDL2) | tag | `ea811e1` |
| VVVVVV branch `2.4-updates` (SDL2) | branch head | `79395fa` |
| VVVVVV `master` (SDL3) | branch head | `40e1ec3` (2026-10-19) |
| SDL2 as packaged by ps2sdk-ports | `release-2.32.10` | `5d24957` |
| SDL2 branch head (2.33 dev) | `SDL2` | `8acf7cf` |
| SDL3 head | `main` | `d0183f0` |
| ps2sdk | master | `329e2bb` |
| ps2sdk-ports | master | `d7b2d8a` |
| ps2dev (meta/Docker) | master | `594e2b2` |
| gsKit | master | `8ef73d0`; ports pins `v1.5.1` |
| ps2_drivers | `2.0.0`, as pinned by ports | — |
| EE toolchain (ps2toolchain-ee config) | — | GCC `ee-v15.2.0`, binutils `ee-v2.45.1`, newlib `ee-v4.6.0`, pthread-embedded |

---

## 2. Repository structure and build system

### 2.1 Layout (VVVVVV upstream) [V]
- **`desktop_version/`**
  - CMake project, about 82k lines of C/C++ in `src/`.
  - `fonts/`: free, OFL-licensed CJK sheets plus the Latin font.
  - `lang/`: XML localisation files.
- **`third_party/`** (git submodules): PhysicsFS 3.2.0, tinyxml2 9.0, lodepng, FAudio 24.02,
  c-hashmap, SheenBidi.
- **`mobile_version/`**: legacy, not relevant.
- **Assets are not included.** Graphics, sounds and music come from a proprietary `data.zip`.

### 2.2 Build system [V]
- **Single `CMakeLists.txt`.** `BUNDLE_DEPENDENCIES=ON` (the default) builds all third-party
  libraries statically from `third_party/`.
- **Options:** `STEAM`, `GOG` (default OFF), `MAKEANDPLAY`, `OFFICIAL_BUILD`,
  `REMOVE_ABSOLUTE_PATHS`.
- **SDL discovery:** pre-set `SDL2_INCLUDE_DIRS`/`SDL2_LIBRARIES`, or
  `find_package(SDL2 CONFIG)`.
- **Platform branches:** Darwin, iOS, Win32, Android, Haiku, Emscripten. **No console
  branches.**
- **Non-WIN32 targets** get `-Wl,--disable-new-dtags` and RPATH settings. These are harmless
  for a static ELF [I].
- **`InterimVersion`** is generated at build time with git. It needs `git` inside the build
  container, or `-DOFFICIAL_BUILD`-style handling.
- **Flag bug** (`CMakeLists.txt:305-313` in 2.4.4) [V]. `set_source_files_properties(...
  COMPILE_FLAGS ...)` is called three times on the C++ sources, and each call replaces the
  previous one. As a result only `-fno-rtti` is applied; `-std=c++98` and `-fno-exceptions`
  are silently dropped. This is harmless for correctness, but worth knowing: binary size goes
  up with exceptions enabled.

### 2.3 Platform abstraction in the game [V]
There is no platform layer. Platform differences are inline `#ifdef` blocks:
- **`FileSystemUtils.cpp`**
  - `_WIN32` / `__EMSCRIPTEN__` / `__linux__…__unix__` select headers and `MAX_PATH`
    (lines 23-42).
  - `__ANDROID__` handles APK mounting.
  - Emscripten `syncfs` after saves.
- **`Screen.cpp`:** `__APPLE__` icon; `__ANDROID__` forced fullscreen.
- **`ButtonGlyphs.cpp`, `KeyPoll.cpp`, `Vlogging.c`, `main.cpp`:** Emscripten main loop and
  Win32 console.

Upstream already supports console-like behaviour via **SDL hints** that need no code changes:
- `SteamTenfoot`: forced fullscreen.
- `SteamDeck`: the keyboard is reported as unavailable, which disables the level editor at
  runtime.

A PS2 port can reuse both.

### 2.4 Which upstream base? [V]
- **`master` migrated to SDL3** in `a8cc0ec` ("Update to SDL3 (#1283)", 2026-08-22). That
  commit touches 68 files, including physfs → 3.3.0 and FAudio → SDL3 platform.
- **`2.4-updates` is still SDL2.** It is 2.4.4 plus six commits, including:
  - `be29c31`: FAudio `nBlockAlign` fix. 2.4.4 sizes decode buffers 8× too large: 282 KB per
    stereo track instead of about 35 KB.
  - `08c3590`: `FAUDIO_1024_QUANTUM`.
  - FAudio bumped to 26.05.
- **SDL3 has a PS2 backend** (render, video, audio, joystick, thread, timer, filesystem,
  main), but `ps2sdk-ports` does not package SDL3. It also has the same render-target stub
  (§4.1).

**Recommendation: port from `2.4-updates` (SDL2).** It matches the toolchain's packaged SDL
(2.32.10), and the API surface is frozen. Revisit master/SDL3 only if ps2sdk-ports adopts SDL3.

---

## 3. Dependency matrix

### 3.1 Third-party libraries

| Dependency | Version (bundled) | Used for | PS2 availability | Status | Notes |
|---|---|---|---|---|---|
| **SDL2** | needs ≥ 2.26 [I] | everything | `ps2sdk-ports` builds **2.32.10** with PS2 render/video/audio/joystick/thread/timer/filesystem/main backends [V] | ✅ available | Render-target stub, colour-range issue, audio thread priority (§4) |
| **PhysicsFS** | 3.2.0 | data.zip, saves, enumeration | **Not in ps2sdk-ports.** No PS2 entry in `physfs_platforms.h` → `#error Unknown platform` [V]. Upstream main (`c30aa90`) also has none [V] | ⚠️ needs patch | Small platform shim (§7). The toolchain defines `__ps2sdk__`, `PS2`, `__PS2__`, `_EE`, not `__unix__` [V: `gcc/config/mips/ps2sdk.h`, `ps2dev.cmake`] |
| **FAudio** | 24.02 (2.4.4) / 26.05 (2.4-updates) | audio mixing | Ports builds FAudio 26.01 with `-DBUILD_SDL3=OFF` [V]. The bundled copy is used anyway | ✅ compiles [I] | SIMD paths gated to x86-64/AArch64, scalar fallback on MIPS [V]. Opens an F32 device and SDL converts to S16 [V] |
| **stb_vorbis** | inside FAudio | OGG music decode | header-only | ✅ [I] | Double-precision maths at setup; `alloca` in decode on the audio thread [V] |
| **tinyxml2** | 9.0 | saves, levels, lang, fontmeta | Ports builds 11.0.0; bundled works too | ✅ [I] | Pure C++98 |
| **lodepng** | 20220717 | PNG decode | header-ish C | ✅ [I] | `LODEPNG_NO_COMPILE_DISK` |
| **c-hashmap** | — | lookups | portable C99 | ✅ [I] | |
| **SheenBidi** | — | RTL text | Ports builds 3.0.0 | ✅ [I] | Only needed for Arabic/Hebrew; still always compiled |
| **zlib** | not used directly | PhysFS ZIP uses its own miniz-style inflate | n/a | ✅ | |
| **Steam/GOG** | — | achievements | off by default; `Network.c` becomes no-ops [V] | ✅ disabled | `SDL_LoadObject` still linked: dummy on PS2 [V] |

### 3.2 Toolchain and runtime

| Item | Source | Status | Notes |
|---|---|---|---|
| EE compiler | `mips64r5900el-ps2-elf-gcc/g++` 15.2 | ✅ [V config] | Hard float, **single precision only**; doubles are soft-float [V `config.gcc`] |
| C library | newlib 4.6 + ps2sdk `libcglue` | ✅ | `getcwd`, `mkdir`, `stat`, `opendir`/`readdir`, `rename`, `unlink`, `nanosleep`, `getpwuid`/`getuid` stubs present in libcglue [V grep] |
| pthreads | pthread-embedded, linked by default (`-lpthread` in LIB_SPEC) | ✅ [V] | Used by PhysFS posix mutexes |
| gsKit | `v1.5.1` | ✅ | Required by SDL2 PS2 renderer |
| ps2_drivers | `2.0.0` | ✅ functional / ⚠️ **license** | Loads poweroff, fileXio, **memcard**, **USB (BDM)**, MX4SIO, CDFS, DEV9, HDD at SDL main [V `ps2_filesystem_driver.c:116-131`] |
| SDL2 link line | SDL2 CMake | [V] | `-lSDL2main -lSDL2 -lpatches -lgskit -ldmakit -lps2_drivers -latomic` (+ pad/audsrv libs pulled transitively [I]) |
| CMake toolchain file | `$PS2DEV/share/ps2dev.cmake` | ✅ [V] | `CMAKE_SYSTEM_NAME Generic`; sets `PS2`, `PLATFORM_PS2` CMake vars |
| Docker image | `ghcr.io/ps2dev/ps2dev` (naming per ps2dev CI config) | [V naming] / not pulled | Pin by digest |

---

## 4. SDL2 API usage vs. SDL2-PS2 (release 2.32.10)

The game uses **156 distinct `SDL_*` symbols** [V]. Most are stdinc helpers (`SDL_malloc`,
`SDL_snprintf`, …), which are portable. The table below covers only the platform-dependent
APIs.

| API (as used by VVVVVV) | SDL2-PS2 status | Evidence | Impact on VVVVVV |
|---|---|---|---|
| `SDL_CreateRenderer(ACCELERATED\|TARGETTEXTURE)` | gsKit renderer, flags claim `TARGETTEXTURE` | `render/ps2/SDL_render_ps2.c:720-731` [V] | Selected by default |
| `SDL_TEXTUREACCESS_TARGET` + `SDL_SetRenderTarget` | **❌ Stub:** `PS2_SetRenderTarget` returns 0 and does nothing | `SDL_render_ps2.c:188-191` [V]; same in SDL3 | **Critical**: all 11 game render targets are broken |
| `SDL_RenderCopy` / `RenderCopyEx` (flip) | ✅ via core geometry path | `SDL_render.c` fallback [V] | Fine |
| `SDL_SetTextureColorMod` / `AlphaMod` | ⚠️ Partial: 0-255 passed to GS unscaled; GS treats 0x80 = 1.0. SDL3 rescales | SDL2 `:229,278`; SDL3 `:106-123` [V] | **Likely 2× brightness / over-blend [I]**. Test first in PCSX2 |
| `SDL_SetTextureBlendMode` (BLEND, NONE) | ✅ (MOD/MUL approximate, unused) | `:388-424` [V] | Fine |
| `SDL_SetTextureScaleMode` | ✅ nearest/linear | `:173-186` [V] | Fine |
| `SDL_RenderReadPixels` | ❌ `SDL_Unsupported()` | `:527-531` [V] | Breaks "bad signal" filter and F6 screenshot. Disable both |
| `SDL_RenderDrawPoint(s)/Line/Rect/FillRect` | ✅ | [V] | Fine |
| `SDL_UpdateTexture`, `CreateTextureFromSurface` | ✅; native ABGR8888/ABGR1555, max **1024×1024** | `:724-730` [V] | CJK font sheets exceed 1024 (§6.1) |
| `SDL_RenderSetVSync` | ✅; plain vsync **busy-waits** at main-thread priority | `:533-557`; gsKit `gsCore.c:170` [V] | Can starve audio (§6.4). Use `SDL_HINT_PS2_DYNAMIC_VSYNC` |
| `SDL_GetRendererOutputSize`, `GetWindowSizeInPixels` | ⚠️ returns *window* size, not GS size | `SDL_render.c:1253` [V] | Window size must match the 640×448 GS buffer |
| `SDL_CreateWindow`, `SetWindowFullscreen/Size/Position/Icon/Title/MinimumSize`, `ShowWindow` | Core-only no-ops | `video/ps2/SDL_ps2video.c:51-124` [V] | Harmless |
| `SDL_GetDesktopDisplayMode` | ✅ 640×**480** in 2.32.10 (448 on branch head) | `SDL_ps2video.c:69-93` [V] | |
| `SDL_GameController*` | ✅ libpad + multitap, built-in mapping "PS2 Controller" | `SDL_gamecontrollerdb.h:993-995` [V] | Works. `GetType` = UNKNOWN and vendor = 0, so the game shows **Xbox glyphs** |
| `SDL_StartTextInput` / clipboard | Stub (no OSK) | [V] | Editor and level-name entry unusable. Hide the editor |
| `SDL_OpenURL`, `ShowSimpleMessageBox`, `LoadObject` | Return errors | [V] | Harmless; missing-data.zip message box will not show |
| `SDL_GetBasePath` | `getcwd()+"/"`; cwd = `dirname(argv[0])` (e.g. `mass:/APPS/VVVVVV/`) | `filesystem/ps2/SDL_sysfilesystem.c:34-46`; `libcglue/cwd.c:239-261` [V] | Works when launched from USB/HDD; read-only on CD |
| `SDL_OpenAudioDevice` (F32, 44100, stereo) | ✅ with conversion; device is S16, 512 frames | `audio/ps2/SDL_ps2audio.c:51-68` [V] | EE CPU cost for conversion |
| `SDL_GetTicks64`, `SDL_Delay` | ✅ ms ticks; `Delay` = blocking `nanosleep` (yields) | `timer/ps2/SDL_systimer.c` [V] | Fine |
| `SDL_LoadWAV_RW`, `SDL_RWFromFile/ConstMem` | ✅ generic | [V] | |
| Threads (audio callback thread) | EE kernel threads, **6 KiB default stack**, strict priority | `thread/ps2/SDL_systhread.c:69-73` [V] | Set `SDL_HINT_THREAD_STACK_SIZE`; fix priorities |

**SDL2 branch head vs 2.32.10:** head adds GS resolution/progressive hints, a 448-line mode,
and analog-mode pad init. Head also adds a pad-ready wait loop that may hang on empty ports [I].
**Pin 2.32.10**, as ports does, and cherry-pick features deliberately.

---

## 5. Smallest viable build configuration

**Build settings** (no game-code changes) [V unless noted]:
- `BUNDLE_DEPENDENCIES=ON`, `STEAM=OFF`, `GOG=OFF`, `CMAKE_BUILD_TYPE=Release`, `-Os` or `-O2`.
- **`MAKEANDPLAY=ON`** removes the main campaign room tables (about 1.08 MB of rodata [I]) and
  is also the only build type whose assets may be redistributed (§8).
- **Do not ship** `font_ja/ko/sc/tc.png` or the non-English `lang/` folders. Fonts are loaded
  by directory enumeration, so leaving files out removes them with no code change.

**Not removable without code changes:** the editor, LevelDebugger, LocalizationMaint,
RoomnameTranslator, IME and bidi are always compiled; upstream has no `NO_EDITOR` option. They
can be **hidden at runtime** via the `SteamDeck` hint. They cost code size only.

**`data.zip` must still provide** [V `GraphicsResources.cpp:409-430`, `Music.cpp:747-774`]:
- 19 `graphics/*.png` sheets.
- `VVVVVV.png`.
- 28 `sounds/*.wav`.
- `vvvvvvmusic.vvv` or `music/*.ogg`.

A missing or wrongly sized sheet aborts startup.

---

## 6. Platform requirements by subsystem

### 6.1 Graphics

**Game pipeline** [V]:
- Window 640×480, then `SDL_CreateRenderer`.
- All drawing goes into **11 ARGB8888 render targets**:
  - 6 × 320×240 (game, gameplay, menu, ghost, tempShake, foreground).
  - 4 × 336×336 (background, tempScrolling, towerbg, titlebg).
  - 1 × 240×180 (custom minimap).
- Final upscale copy of `gameTexture` to the window.
- Total target size: 3,822,336 B linear; about **4.33 MB in GS page-aligned layout** [I].

**PS2 constraints:**
- GS VRAM is **4 MiB** and must also hold the framebuffers.
- A 640×448 double-buffered 32 bpp framebuffer is about 2.3 MB [I].
- Max texture size is 1024×1024 [V].
- gsKit keeps textures in EE RAM and uploads them through a VRAM cache on bind [V].

**Consequences:**
- Fixing render targets inside the SDL PS2 renderer would require VRAM-resident targets. Those
  **do not fit** at 32 bpp next to the framebuffer. It is only plausible with 16 bpp targets
  plus a smaller or single framebuffer, and that is a large, risky SDL patch.
- **CJK fonts are a hard blocker** if shipped:
  - `font_sc` and `font_tc` are 1644×1656; `font_ko` is 1400×1420. All exceed 1024 [V `file`].
  - All fonts at 32 bpp total 33.1 MB [V arithmetic]. Latin plus button glyphs total 341 KB [V].
- **Default scaling** is integer mode. On a 448-line output that gives a 1× image, i.e. a small
  320×240 picture. The PS2 default should be letterbox or stretch [I].
- **"Bad signal" filter:** uses `ReadPixels` every frame plus double-precision maths. Disable it.

**Recommended render strategy, Option A: software renderer + texture framebuffer** [V code
path, runtime unverified]:
- Set `SDL_HINT_RENDER_DRIVER=software` before the renderer is created. In SDL2 2.32.10 the
  hint path selects the driver by name and ignores the `ACCELERATED` flag [V
  `SDL_render.c` `SDL_CreateRenderer`].
- The software renderer fully implements targets, colour/alpha mod, flips and `ReadPixels` [V
  `SDL_render_sw.c:177,1070`].
- Because the PS2 video driver has no `CreateWindowFramebuffer`, SDL automatically presents the
  window surface through a hardware-renderer texture: `SDL_CreateWindowTexture`,
  `SDL_video.c:2708-2740` [V].
- **Costs:**
  - All targets and image surfaces live in **EE RAM** (about 3.8 MB of targets plus image
    data).
  - Per-pixel blitting runs on the EE.
- **Benefits:**
  - Zero renderer changes in the game.
  - At most a small SDL fix: the colour-range fix, if that bug is confirmed.

**Option B (later or optional):** implement real GS render-to-texture in `SDL_render_ps2.c`,
using 16 bpp targets and a 320×240 or 16 bpp framebuffer. Do this only if Option A cannot hold
30 fps.

**Option C:** rewrite the game renderer. **Not recommended**, as it violates the
"no rendering rewrite" rule.

### 6.2 Filesystem
- **Data:** PhysFS mounts `basePath/data.zip` (`basePath = SDL_GetBasePath()`). It also mounts
  `basePath/lang/` and `fonts/` as `graphics/` [V `FileSystemUtils.cpp:173-358`].
- **Write directory:** `PHYSFS_getPrefDir()` on Unix uses `$HOME`/`getpwuid` [V]. This is
  meaningless on PS2, so the PS2 build must pass an explicit base directory.
- **Directories:** `mkdir()` is called directly, outside PhysFS, for `saves/`, `levels/` and
  `screenshots/1x|2x` [V].
- **Save files:** `unlock.vvv`, `settings.vvv`, `tsave.vvv`, `qsave.vvv`, `levelstats.vvv`
  and `saves/*.vvv`. They are small XML files [V]. `savestatsandsettings()` is called often.
- **PS2 specifics** [I]:
  - **Read-only boot media.** CD and DVD are read-only.
  - **Memory card** (`mc0:`) writes are slow and the card is only 8 MB.
  - **USB.** SCPH-7500x USB is 1.1, so data.zip loading will be slow.
  - **Suggested layout:** data on USB or HDD next to the ELF; saves either beside the ELF
    (simplest) or under `mc0:/VVVVVV/` (proper). The memcard driver is already loaded by SDL
    main [V].
- **PhysFS on PS2** needs the following [V `physfs_platform_posix.c`/`unix.c`]:
  - pthread mutexes;
  - `open`/`lseek`/`fstat`/`fsync`/`opendir`/`readdir`;
  - user/home lookup, `readlink("/proc/…")` and mntent (CD-ROM).
  
  Cleanest fix: a ~150-line `physfs_platform_ps2.c` that delegates to the posix file functions
  and stubs out base/pref dir and CD-ROM detection.

### 6.3 Audio [V unless noted]
- **Engine:** FAudio, 44100 Hz stereo float; 8 SFX voices plus 1 music voice.
- **Device:** SDL2-PS2 forces S16 with 512-frame buffers through `audsrv`. SDL converts
  F32→S16 on the EE.
- **SFX:** 28 WAVs fully decoded to PCM, resident for the whole run.
- **Music:**
  - `vvvvvvmusic.vvv` is read **entirely into RAM**, one `SDL_malloc` per track
    (`FileSystemUtils.cpp:954-1050`).
  - Each `MusicTrack` then **copies** its OGG data again (`Music.cpp:378-379`).
  - An `stb_vorbis` decoder stays open for **every** track at once.
- **Decoding:** happens in FAudio buffer callbacks on the SDL audio thread.
- **Blob size:** not inspected. It is commonly reported as tens of MB [A]. **Measure it with
  your own copy** before planning (see plan step P0).

### 6.4 Input, threading, timing

**Input** [V]:
- Only the GameController API is used. The PS2 pad has a built-in mapping, so it should work
  as is [I]:
  - Cross = A (flip), Circle = B (back), Triangle = Y (map), Square = X.
  - Start and Select map normally; L/R map to shoulders.
- Glyphs are chosen by vendor ID. PS2 reports 0, so the game falls back to Xbox glyphs; force
  `LAYOUT_PLAYSTATION`.
- No keyboard exists. Use the `SteamDeck` hint to hide the editor and text entry.

**Threading** [V]:
- The game has no threads. The only extra thread is the SDL audio callback thread, with a
  6 KiB stack, plus FAudio and SDL mutexes.
- **The EE scheduler is strict-priority** [V]. The main thread runs at priority 1, the audio
  thread at 15/16, and plain vsync busy-spins.
- **Risk: audio starvation and stack overflow during vorbis decode** [I].
- **Mitigation:**
  - `SDL_HINT_PS2_DYNAMIC_VSYNC=1` (vsync waits on a semaphore).
  - `SDL_HINT_THREAD_STACK_SIZE` of 64-128 KiB, or raise the audio thread priority.

**Timing** [V]:
- 34 ms fixed timestep, accumulator-based.
- `over30mode`, which renders uncapped with interpolation, is on by default. **Default it off
  on PS2** so frames render at the logic rate (about 30 fps).
- The 50 Hz PAL display has no effect on logic speed (fixed timestep) [I].

### 6.5 Memory budget (EE, 32 MB)

| Item | Estimate | Basis |
|---|---|---|
| EE kernel / reserved low memory | ~1 MB | [I] standard EE layout |
| ELF code + rodata (game, SDL, gsKit, FAudio, physfs, tinyxml2, embedded IRX images) | 3-5 MB | [A] until first build |
| `customlevelclass` static `contents[40*30*400]` | 1.92 MB | [V] `CustomLevels.h:156` |
| Main-campaign room tables (removed by `MAKEANDPLAY`) | ~1.08 MB | [I] |
| Software render targets (Option A) | 3.82 MB | [V] |
| Image sheets as surfaces (19 PNGs, several duplicated as white/grey variants) | 5-8 MB | [A] dims not in repo |
| Sprite surfaces kept for collision (sheet + per-32×32 copies, ×2 sheets) | ~3 MB | [V mechanism] / [A] dims |
| Fonts (Latin + buttons only) | 0.34 MB + glyph pages | [V] |
| Window texture (texture framebuffer) | 0.3-1.2 MB | [I] depends on window size |
| SFX PCM (28 WAVs) | 1-3 MB | [A] |
| Music: blob + per-track copy + 16 vorbis decoders (+ 4.5 MB of bad buffers in 2.4.4) | **2 × blob + ~2-3 MB** | [V mechanism] / [A] size |
| Heap fragmentation, transient PNG decode peaks (file + RGBA + surface) | 2-4 MB | [I] |

**Without music** the total is about 21-29 MB. Music as written does not fit; with a lazy or
streamed loader it costs about one track plus decoder state [I]. These are **planning
estimates**; the first PCSX2 run must log real `mallinfo()` numbers.

### 6.6 Floating point [V]
- Gameplay physics uses `float`.
- Doubles appear only in:
  - load-time greyscale conversion;
  - the bad-signal filter;
  - small UI maths;
  - stb_vorbis setup and FAudio resampler setup.
- No NaN/Inf checks.
- The R5900 FPU is non-IEEE and single-precision, and doubles are soft-float. **Low risk** for
  logic [I]. Watch for soft-float hot spots in profiling.

---

## 7. Risk register

Severity levels:
- **Critical:** the game cannot run correctly.
- **High:** blocks building, or causes a major functional or performance failure.
- **Medium:** degraded UX, or a workaround is required.
- **Low:** cosmetic.

| # | Risk | Sev. | Evidence | Mitigation |
|---|---|---|---|---|
| R1 | SDL PS2 renderer render targets are a no-op | **Critical** | [V] | Option A (software renderer + texture framebuffer); Option B later |
| R2 | Music blob resident twice + all decoders open → exceeds RAM | **Critical** | [V] mechanism, [A] size | Lazy-load only the current track (isolated `Music.cpp` change); base on `2.4-updates` (buffer fix) |
| R3 | VRAM (4 MiB) cannot hold the 11 targets at 32 bpp + framebuffers | **Critical for Option B** / n/a for A | [V]/[I] | Choose Option A |
| R4 | PhysFS `#error Unknown platform` | High (build) | [V] | `physfs_platform_ps2.c` + `physfs_platforms.h` entry (isolated patch) |
| R5 | `FileSystemUtils.cpp` lacks `MAX_PATH`/`mkdir` headers without `__unix__` | High (build) | [V] | Add `__PS2__` to the include branch |
| R6 | CJK fonts: > 1024 px and 33 MB at 32 bpp | High (if shipped) | [V] | Don't ship them (packaging rule) |
| R7 | SDL2-PS2 colour/alpha range not scaled to GS 0x80 | High [I] | [V] code, [I] effect | Verify in PCSX2; backport SDL3's scaling as a small SDL patch |
| R8 | Audio starvation from busy vsync + strict priorities; 6 KiB audio stack with vorbis `alloca` | High [I] | [V] code | Dynamic-vsync hint, stack-size hint, priority adjust |
| R9 | Software-rendering + F32→S16 + vorbis CPU cost on 294 MHz EE | High (unknown) | [I] | Disable `over30mode`; profile; Option B if needed |
| R10 | **Licensing:** binary distribution requires MAKEANDPLAY or permission; `ps2_drivers` LICENSE is GPL-3.0 (README says LGPL-2) | High (distribution only) | [V] texts | §8; ask upstreams; personal use unaffected |
| R11 | Display scaling defaults to integer → 1× image on 448-line output | Medium | [V]/[I] | PS2 default = letterbox/stretch; window size = GS size |
| R12 | Saves: `getPrefDir` meaningless; CD read-only; MC slow and small | Medium | [V]/[I] | Explicit base dir; saves beside ELF or `mc0:/VVVVVV/` |
| R13 | Xbox button glyphs; no keyboard → editor/text entry unusable | Medium | [V] | Force PlayStation layout; `SteamDeck` hint |
| R14 | `ReadPixels` unsupported → bad-signal filter and screenshots fail | Medium | [V] | Disable on PS2 (works under Option A, but too slow) |
| R15 | SDL2 head pad-init may hang on empty port; head ≠ ports version | Medium | [V]/[I] | Pin `release-2.32.10` |
| R16 | Slow USB 1.1 load of data.zip | Medium | [I] | Recommend HDD/MX4SIO or a smaller re-packed data.zip (user-side) |
| R17 | Message box / OpenURL / LoadObject / window ops are stubs | Low | [V] | None needed; log to `printf` |
| R18 | `%li`/`%lli` vs `size_t` format warnings; dropped `-std=c++98`/`-fno-exceptions` flags | Low | [V] | Optional upstream CMake fix |
| R19 | PCSX2 vs SCPH-7500x behaviour differences (GS timing, USB, IOP) | Medium | [I] | Hardware test gate before calling any milestone done |

---

## 8. Licensing (source vs. assets)

### 8.1 Game source: VVVVVV Source Code License v1.0 [V `LICENSE.md`]
- Custom and **non-commercial**. Altered builds may be redistributed if they meet all of these:
  - they are not commercial;
  - they are plainly marked as altered;
  - they credit Terry Cavanagh and the VVVVVV source;
  - the notice is kept.
- "You must not distribute any materials from the game which are not included in this repo
  unless approved by us in writing."

### 8.2 Game assets [V `LICENSE.md`, `License exceptions.md`]
- Graphics, sounds and music (music by Magnus Pålsson) are **proprietary** and are not in the
  repo.
- **General exception:** builds compiled with `MAKEANDPLAY` may freely distribute the Make and
  Play `data.zip`, but must not distribute the original levels.
- Per-port exceptions exist for Dreamcast, Wii, PSP and others. **PS2 is not listed**, on
  either 2.4.4 or master.
- **This project must not commit or ship `data.zip`.** The user supplies their own.

### 8.3 Repo-included content [V]
- **`desktop_version/lang/*`:** part of the repo, under the VVVVVV license.
- **`fonts/font_ko.png`:** SIL OFL 1.1, modified version (Minseo Lee). Not needed for an
  English-only build.
- **`fonts/font_sc.png`, `font_tc.png`:** SIL OFL 1.1, modified "Fusion Pixel" by TakWolf.
  Not needed for an English-only build.

### 8.4 Third party and toolchain [V]

| Component | License | Obligation |
|---|---|---|
| SDL2, PhysicsFS, lodepng, tinyxml2, FAudio | zlib | Attribution optional; mark modified versions (PhysFS/SDL patches) |
| c-hashmap | BSD-3-Clause | Keep notice in docs |
| SheenBidi | Apache-2.0 | Keep notice / NOTICE |
| stb_vorbis (in FAudio) | Public domain / MIT | — |
| ps2sdk, gsKit | Academic Free License 2.0 | Attribution |
| newlib, libgcc / libstdc++ | Various BSD-style / GPL with runtime exception | OK for static linking |
| **ps2_drivers 2.0.0** | **`LICENSE` = GPL-3.0, README = "GNU Library GPL v2"** | **Unresolved.** If GPL-3.0 applies, a statically linked ELF must be distributed under GPL-3.0, which conflicts with VVVVVV's non-commercial restriction. Personal builds are unaffected. Clarify with the maintainer, or avoid it by building SDL2 without ps2_drivers (non-trivial). |

**Bottom line:**
- Personal builds and publishing source patches are fine.
- A **distributable ELF** needs `MAKEANDPLAY`, no bundled assets unless permission is granted,
  and the ps2_drivers question settled.

*This is a technical reading of the licence texts, not legal advice.*

---

## 9. Proposed build strategy

1. **Container.**
   - Use the official `ghcr.io/ps2dev/ps2dev` image (toolchain + ps2sdk + ports, including
     SDL2 2.32.10 and gsKit), **pinned by digest**.
   - Add `git`, `cmake` and `make` if they are missing.
   - A thin `docker/Dockerfile` in this repo layers on the patched SDL2 (R7) when needed.
     Mount the repo; build into `build-ps2/`.
2. **Source layout** (keeps upstream mergeable):
   - `VVVVVV/`: git submodule or subtree of upstream `2.4-updates`.
   - `patches/`: small, reviewable patches against upstream, or a fork branch with isolated
     commits.
   - `platform/ps2/`: new files only (PhysFS platform shim, PS2 init: hints, paths, IOP
     modules).
   - `cmake/`: `ps2.cmake` glue (toolchain file pass-through, link libraries).
   - `docker/`, `docs/`.
3. **CMake.**
   - Configure with `-DCMAKE_TOOLCHAIN_FILE=$PS2DEV/share/ps2dev.cmake -DMAKEANDPLAY=ON
     -DCMAKE_BUILD_TYPE=Release`.
   - Pass `SDL2_INCLUDE_DIRS`/`SDL2_LIBRARIES` explicitly, including the full PS2 link line
     from §3.2, rather than relying on `find_package`.
   - Add a guarded `if(PS2)` block in `CMakeLists.txt` that:
     - swaps PhysFS platform sources for `physfs_platform_ps2.c`;
     - skips RPATH;
     - adds `-DPHYSFS_NO_CDROM_SUPPORT`.
4. **Output.** `VVVVVV.elf`, optionally packed with `ps2-packer`. Run layout:
   `mass:/APPS/VVVVVV/{VVVVVV.elf, data.zip, lang/en, fonts/(latin only)}`.
5. **Test loop.**
   1. PCSX2 with host filesystem enabled (`host:`) for fast iteration.
   2. PCSX2 with a USB-image setup.
   3. SCPH-7500x via USB stick (uLaunchELF / OPL / wLaunchELF).

   Each milestone is accepted **only after the hardware run**.

---

## 10. Minimum changes needed to produce a PS2 ELF

"Produce" means a linkable ELF, not a working game.

| # | Change | Where | Size |
|---|---|---|---|
| M1 | PS2 entry in `physfs_platforms.h` + new `physfs_platform_ps2.c` (posix file ops, stub base/pref dir, no CD-ROM) | `third_party/physfs` (patch) + `platform/ps2/` | ~150 LOC |
| M2 | Add `__PS2__` to the `<limits.h>/<sys/stat.h>/MAX_PATH` branch | `FileSystemUtils.cpp:38` | 1 line |
| M3 | `if(PS2)` CMake block: PhysFS sources, no RPATH, SDL2 link line, `SDL2main` | `desktop_version/CMakeLists.txt` | ~20 lines |
| M4 | Docker build script | new | — |

**Changes needed for a playable build** (beyond the ELF):

| # | Change | Where | Size |
|---|---|---|---|
| M5 | PS2 init before `SDL_Init`. Hints: `RENDER_DRIVER=software`, `PS2_DYNAMIC_VSYNC=1`, `THREAD_STACK_SIZE`, `SteamTenfoot=1`, `SteamDeck=1`. Explicit base/save dir. `over30mode` off, scaling letterbox, bad-signal off | `main.cpp` / `platform/ps2/` | ~40 LOC under `#ifdef __PS2__` |
| M6 | Lazy music loading: keep only the active track's OGG and decoder | `Music.cpp` (+ `BinaryBlob`) | ~100 LOC, upstreamable |
| M7 | Force `LAYOUT_PLAYSTATION` | `ButtonGlyphs.cpp:205` | few lines |
| M8 | Window size = 640×448; check the texture-framebuffer path scales correctly | `Screen.cpp` | few lines |
| M9 | (If R7 is confirmed) backport SDL3 colour/alpha scaling to SDL2 PS2 renderer | SDL2 patch | ~30 LOC |

---

## 11. Prioritized implementation plan

Each step ends with an explicit verification gate. "Builds" is never accepted as "works".

| Pri | Step | Exit criterion |
|---|---|---|
| **P0** | **Environment + measurements.** Install Docker and PCSX2; pull and pin the ps2dev image; `echo \| ee-gcc -dM -E -` to confirm predefined macros. Measure the user's own `data.zip`: PNG dimensions, WAV sizes, `vvvvvvmusic.vvv` size. Assets stay local and are never committed. | Real numbers replace every [A] in §6.5 |
| **P1** | **SDL2-PS2 smoke test** (separate tiny ELF, no game): software renderer + texture framebuffer, a render target, colour mod 255/128, alpha blend, pad input, F32 audio callback with a sine wave. | Correct colours, input and audio in PCSX2 **and** on SCPH-7500x. Settles R1/R7/R8 |
| **P2** | **Build skeleton.** Repo layout, Docker script, upstream `2.4-updates` submodule, M1-M4. | `VVVVVV.elf` links; size recorded |
| **P3** | **Boot to title.** M5, M7, M8; data on `host:`. | Title screen renders correctly in PCSX2; no crash on missing pad; log heap usage |
| **P4** | **Memory fix.** M6 lazy music. | Free heap ≥ 3 MB after entering gameplay with music playing; no audio dropouts over a 10-min session |
| **P5** | **Gameplay + saves.** Play through a Make and Play level; save, reload and restart; decide the save location (beside ELF vs `mc0:`). | Saves persist across power cycle on hardware |
| **P6** | **Performance pass.** Profile EE time per frame (render vs logic vs audio). | Stable 30 fps logic with no audio underruns on hardware; else open Option B (GS render targets) as a separate project |
| **P7** | **Hardening.** PAL/NTSC, multitap absent, USB vs HDD/MX4SIO load times, power-off handling, M9 if needed. | Hardware test checklist passes |
| **P8** | **Licensing before any release.** Resolve the ps2_drivers licence; confirm MAKEANDPLAY packaging; optionally ask Terry Cavanagh for a PS2 exception. | Written answers on file |

**Do not start P2 before P0/P1:** P1 decides whether Option A is viable at all.

---

## 12. Open questions

1. Does `mips64r5900el-ps2-elf-gcc` predefine `__unix__`? **Resolved in P0: no.** `-dM -E`
   shows `__ps2sdk__`, `__mips_single_float`, `__mips_hard_float`, and no `__unix__`/`__linux__`
   (see [ps2-p1-report.md](ps2-p1-report.md)).
2. Is the SDL2-PS2 colour range actually wrong on screen (R7), or does gsKit compensate
   somewhere not traced?
3. Real asset sizes, which drive the RAM budget.
4. Does the SDL software renderer reach 30 fps on the EE with VVVVVV's draw load?
5. The ps2_drivers licence (GPL-3.0 vs LGPL-2).
6. Target save location: beside the ELF or the memory card. Product decision for the
   maintainer.
