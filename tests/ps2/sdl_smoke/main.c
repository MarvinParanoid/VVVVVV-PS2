/* sdl_smoke: SDL2 PS2 bring-up test (default "PS2 gsKit" renderer).
 *
 * Covers: subsystem init, renderer/display info, basic drawing, a visual
 * colour-mod range probe, a visual hardware render-target probe, game
 * controller discovery/events, F32 audio callback throughput, and frame
 * timing with vsync off / on / dynamic vsync.
 *
 * Screen layout during the "pattern" phases (640x448):
 *   row 1 (y=40):  reference FillRects  grey 128 | white 255
 *   row 2 (y=120): white texture copied with colour mod 128 | 255
 *   row 3 (y=200): white texture with alpha mod 128 over red | alpha 255 over red
 *   row 4 (y=280): moving square (frame pacing), pad buttons as small boxes
 * If the GS colour range is handled correctly, row 2 left matches row 1 left.
 *
 * Phase "hw-target-probe" (3 s): a 64x64 target texture is cleared to green
 * and copied to (288,192). Correct result: a green square on a dark blue
 * background. Any other result means render targets do not work. */
#include <SDL.h>
#include <math.h>
#include <stdio.h>

#include <stdlib.h>
#include <string.h>
#include <malloc.h>

#include "../common/gsread.h"
#include "../common/harness.h"

#define WIN_W 640
#define WIN_H 448
#define AUDIO_RATE 44100

static SDL_atomic_t audio_frames;
static SDL_atomic_t audio_callbacks;
static float phase_acc;

static void SDLCALL audio_cb(void *userdata, Uint8 *stream, int len)
{
    float *out = (float *)stream;
    int frames = len / (int)(2 * sizeof(float));
    int i;
    (void)userdata;
    for (i = 0; i < frames; i++)
    {
        float v = 0.15f * sinf(phase_acc);
        phase_acc += 2.0f * 3.14159265f * 440.0f / AUDIO_RATE;
        if (phase_acc > 2.0f * 3.14159265f)
            phase_acc -= 2.0f * 3.14159265f;
        out[2 * i] = v;
        out[2 * i + 1] = v;
    }
    SDL_AtomicAdd(&audio_frames, frames);
    SDL_AtomicAdd(&audio_callbacks, 1);
}

static SDL_GameController *pads[8];
static Uint32 buttons_down;

static void log_renderer(SDL_Renderer *r)
{
    SDL_RendererInfo info;
    int w = 0, h = 0;
    Uint32 i;
    if (SDL_GetRendererInfo(r, &info) != 0)
    {
        th_log("GetRendererInfo failed: %s", SDL_GetError());
        return;
    }
    th_log("renderer name=%s flags=0x%x (accel=%d vsync=%d target=%d) max_tex=%dx%d",
           info.name, info.flags,
           !!(info.flags & SDL_RENDERER_ACCELERATED),
           !!(info.flags & SDL_RENDERER_PRESENTVSYNC),
           !!(info.flags & SDL_RENDERER_TARGETTEXTURE),
           info.max_texture_width, info.max_texture_height);
    for (i = 0; i < info.num_texture_formats; i++)
        th_log("  texture format[%u]=%s", i, SDL_GetPixelFormatName(info.texture_formats[i]));
    SDL_GetRendererOutputSize(r, &w, &h);
    th_log("  output size=%dx%d", w, h);
}

static void handle_events(int *quit)
{
    SDL_Event e;
    while (SDL_PollEvent(&e))
    {
        switch (e.type)
        {
        case SDL_QUIT:
            *quit = 1;
            break;
        case SDL_CONTROLLERDEVICEADDED:
        {
            int idx = e.cdevice.which;
            if (idx >= 0 && idx < 8 && !pads[idx])
            {
                pads[idx] = SDL_GameControllerOpen(idx);
                th_log("EVENT controller added index=%d open=%s", idx, pads[idx] ? "ok" : SDL_GetError());
            }
            break;
        }
        case SDL_CONTROLLERBUTTONDOWN:
        case SDL_CONTROLLERBUTTONUP:
            th_log("EVENT button %s %s (instance %d)",
                   SDL_GameControllerGetStringForButton((SDL_GameControllerButton)e.cbutton.button),
                   e.type == SDL_CONTROLLERBUTTONDOWN ? "down" : "up",
                   (int)e.cbutton.which);
            if (e.cbutton.button < 32)
            {
                if (e.type == SDL_CONTROLLERBUTTONDOWN)
                    buttons_down |= 1u << e.cbutton.button;
                else
                    buttons_down &= ~(1u << e.cbutton.button);
            }
            break;
        case SDL_CONTROLLERAXISMOTION:
            if (e.caxis.value > 16000 || e.caxis.value < -16000)
                th_log("EVENT axis %s=%d",
                       SDL_GameControllerGetStringForAxis((SDL_GameControllerAxis)e.caxis.axis),
                       e.caxis.value);
            break;
        case SDL_JOYDEVICEADDED:
            th_log("EVENT joystick added index=%d", e.jdevice.which);
            break;
        default:
            break;
        }
    }
    if ((buttons_down & (1u << SDL_CONTROLLER_BUTTON_START)) &&
        (buttons_down & (1u << SDL_CONTROLLER_BUTTON_BACK)))
        *quit = 1;
}

static void draw_pattern(SDL_Renderer *r, SDL_Texture *white, int frame)
{
    SDL_Rect rc;
    int i;
    SDL_SetRenderDrawColor(r, 0, 0, 64, 255);
    SDL_RenderClear(r);

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    rc.x = 100; rc.y = 40; rc.w = 200; rc.h = 60;
    SDL_SetRenderDrawColor(r, 128, 128, 128, 255);
    SDL_RenderFillRect(r, &rc);
    rc.x = 340;
    SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
    SDL_RenderFillRect(r, &rc);

    rc.x = 100; rc.y = 120;
    SDL_SetTextureColorMod(white, 128, 128, 128);
    SDL_RenderCopy(r, white, NULL, &rc);
    rc.x = 340;
    SDL_SetTextureColorMod(white, 255, 255, 255);
    SDL_RenderCopy(r, white, NULL, &rc);

    rc.x = 100; rc.y = 200;
    SDL_SetRenderDrawColor(r, 255, 0, 0, 255);
    SDL_RenderFillRect(r, &rc);
    rc.x = 340;
    SDL_RenderFillRect(r, &rc);
    SDL_SetTextureBlendMode(white, SDL_BLENDMODE_BLEND);
    rc.x = 100;
    SDL_SetTextureAlphaMod(white, 128);
    SDL_RenderCopy(r, white, NULL, &rc);
    rc.x = 340;
    SDL_SetTextureAlphaMod(white, 255);
    SDL_RenderCopy(r, white, NULL, &rc);

    rc.x = (frame * 4) % (WIN_W - 32); rc.y = 280; rc.w = 32; rc.h = 32;
    SDL_SetRenderDrawColor(r, 255, 255, 0, 255);
    SDL_RenderFillRect(r, &rc);

    for (i = 0; i < 16; i++)
    {
        rc.x = 40 + i * 36; rc.y = 360; rc.w = 28; rc.h = 28;
        if (buttons_down & (1u << i))
            SDL_SetRenderDrawColor(r, 0, 255, 0, 255);
        else
            SDL_SetRenderDrawColor(r, 80, 80, 80, 255);
        SDL_RenderFillRect(r, &rc);
    }
    SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
    SDL_RenderDrawLine(r, 0, 340, WIN_W - 1, 340);
    SDL_RenderDrawPoint(r, 10, 10);
}

/* Runs `frames` frames, collecting frame time and audio throughput. */
static void run_phase(SDL_Renderer *r, SDL_Texture *white, const char *label, int frames, int delay_ms, int *quit)
{
    th_stats st = {{0}, 0};
    int f;
    int a0 = SDL_AtomicGet(&audio_frames), c0 = SDL_AtomicGet(&audio_callbacks);
    uint64_t t0 = th_now_us(), prev = t0, last_beat = t0;
    /* At least `frames` frames and at least 3 s, so the audio rate is measurable. */
    for (f = 0; (f < frames || prev - t0 < 3000000) && !*quit; f++)
    {
        uint64_t now;
        handle_events(quit);
        draw_pattern(r, white, f);
        SDL_RenderPresent(r);
        if (delay_ms)
            SDL_Delay(delay_ms);
        now = th_now_us();
        if (now - last_beat >= 1000000)
        {
            last_beat = now;
            th_log("  [%s] frame %d t=%lu us audio_cb=%d", label, f, (unsigned long)(now - t0),
                   SDL_AtomicGet(&audio_callbacks) - c0);
        }
        th_stats_add(&st, (uint32_t)(now - prev));
        prev = now;
    }
    {
        uint64_t dt = th_now_us() - t0;
        int af = SDL_AtomicGet(&audio_frames) - a0;
        int ac = SDL_AtomicGet(&audio_callbacks) - c0;
        double rate = dt ? (double)af * 1000000.0 / (double)dt : 0.0;
        th_stats_report(&st, label);
        th_log("AUDIO %-28s frames=%d callbacks=%d elapsed_us=%lu rate=%.0f Hz (expected %d)",
               label, af, ac, (unsigned long)dt, rate, AUDIO_RATE);
        th_check(label, rate > AUDIO_RATE * 0.95, "audio delivered %.1f%% of real time", rate * 100.0 / AUDIO_RATE);
    }
}

static void hw_target_probe(SDL_Renderer *r, int *quit)
{
    SDL_Texture *t = SDL_CreateTexture(r, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_TARGET, 64, 64);
    SDL_Rect dst = {288, 192, 64, 64};
    uint64_t until = th_now_us() + 3000000;
    int rc_set = -1, got_back = 0;
    th_check("hw: create target texture", t != NULL, "%s", t ? "" : SDL_GetError());
    if (!t)
        return;
    while (th_now_us() < until && !*quit)
    {
        handle_events(quit);
        SDL_SetRenderDrawColor(r, 0, 0, 64, 255);
        SDL_RenderClear(r);
        rc_set = SDL_SetRenderTarget(r, t);
        got_back = SDL_GetRenderTarget(r) == t;
        SDL_SetRenderDrawColor(r, 0, 255, 0, 255);
        SDL_RenderClear(r);
        SDL_SetRenderTarget(r, NULL);
        SDL_RenderCopy(r, t, NULL, &dst);
        SDL_RenderPresent(r);
    }
    th_log("hw target probe: SetRenderTarget rc=%d GetRenderTarget matches=%d (API-level only; "
           "correctness is visual: expect green 64x64 square at 288,192 on dark blue)", rc_set, got_back);
    {
        Uint32 px = 0;
        SDL_Rect one = {300, 200, 1, 1};
        int rp = SDL_RenderReadPixels(r, &one, SDL_PIXELFORMAT_ARGB8888, &px, 4);
        th_log("hw: RenderReadPixels rc=%d (%s)", rp, rp ? SDL_GetError() : "supported");
    }
    SDL_DestroyTexture(t);
}


/* ---------- GS output checks (read back from VRAM) ---------- */

#if defined(PS2_SDL_OPT) && defined(__ps2sdk__)
typedef struct
{
    uint64_t presents, update_us, queue_build_us, exec_us, finish_us, vsync_us;
    uint32_t os_queue_size, per_queue_size, os_queue_peak, per_queue_peak, early_flushes, oversize_drops;
} PS2_RenderStats;
extern PS2_RenderStats PS2_render_stats;
#endif

static uint8_t *gs_frame;

/* Present the same frame 3x so both GS buffers hold it, then read buffer 0. */
static int gs_capture(SDL_Renderer *r, void (*draw)(SDL_Renderer *, int), int param)
{
    int i;
    uint8_t *other;
    for (i = 0; i < 3; i++)
    {
        draw(r, param);
        SDL_RenderPresent(r);
    }
    if (!gs_frame)
        gs_frame = (uint8_t *)memalign(64, GSREAD_W * GSREAD_H * 3);
    other = (uint8_t *)memalign(64, GSREAD_W * GSREAD_H * 3);
    if (!gs_frame || !other || gsread_framebuffer(0, gs_frame) != 0 || gsread_framebuffer(1, other) != 0)
    {
        free(other);
        th_log("GS readback unavailable");
        return 0;
    }
    if (gsread_is_blank(gs_frame) && gsread_is_blank(other))
    {
        free(other);
        th_log("SKIP GS readback returned only zeros (no GS->EE transfer in this emulator?)");
        return 0;
    }
    th_log("GS readback: both buffers equal=%d", memcmp(gs_frame, other, GSREAD_W * GSREAD_H * 3) == 0);
    free(other);
    return 1;
}

static int gs_px_near(int x, int y, int r, int g, int b, int tol, int *got)
{
    const uint8_t *p = gs_frame + (y * GSREAD_W + x) * 3;
    got[0] = p[0];
    got[1] = p[1];
    got[2] = p[2];
    return abs(p[0] - r) <= tol && abs(p[1] - g) <= tol && abs(p[2] - b) <= tol;
}

static SDL_Texture *gs_white;

static void draw_colour_probe(SDL_Renderer *r, int unused)
{
    SDL_Rect rc = {100, 100, 64, 64};
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(r, 0, 0, 64, 255);
    SDL_RenderClear(r);
    SDL_SetRenderDrawColor(r, 128, 128, 128, 255);
    SDL_RenderFillRect(r, &rc);
    SDL_SetTextureBlendMode(gs_white, SDL_BLENDMODE_NONE);
    rc.x = 200;
    SDL_SetTextureColorMod(gs_white, 128, 128, 128);
    SDL_RenderCopy(r, gs_white, NULL, &rc);
    rc.x = 300;
    SDL_SetTextureColorMod(gs_white, 255, 255, 255);
    SDL_RenderCopy(r, gs_white, NULL, &rc);
    rc.x = 400;
    SDL_SetTextureColorMod(gs_white, 200, 100, 50);
    SDL_RenderCopy(r, gs_white, NULL, &rc);
    /* textured alpha blend: white at alpha 128 over red */
    rc.x = 500;
    SDL_SetRenderDrawColor(r, 255, 0, 0, 255);
    SDL_RenderFillRect(r, &rc);
    SDL_SetTextureColorMod(gs_white, 255, 255, 255);
    SDL_SetTextureBlendMode(gs_white, SDL_BLENDMODE_BLEND);
    SDL_SetTextureAlphaMod(gs_white, 128);
    SDL_RenderCopy(r, gs_white, NULL, &rc);
    SDL_SetTextureAlphaMod(gs_white, 255);
}

static Uint32 cell_colour(int i, int salt)
{
    Uint32 x = (Uint32)i * 2654435761u + (Uint32)salt * 40503u;
    x ^= x >> 13;
    x *= 0x5bd1e995u;
    x ^= x >> 15;
    return x;
}

/* cols x rows cells: untextured fill per cell plus a colour-modded textured
 * 4x4 quad in its centre -> 2*cols*rows draw commands in one frame. */
static void draw_grid(SDL_Renderer *r, int cells)
{
    int cols = cells, rows = cells * GSREAD_H / GSREAD_W, cw = GSREAD_W / cols, ch = GSREAD_H / rows, i;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    SDL_RenderClear(r);
    SDL_SetTextureBlendMode(gs_white, SDL_BLENDMODE_NONE);
    for (i = 0; i < cols * rows; i++)
    {
        Uint32 c = cell_colour(i, 1), t = cell_colour(i, 2);
        SDL_Rect rc;
        rc.x = (i % cols) * cw;
        rc.y = (i / cols) * ch;
        rc.w = cw;
        rc.h = ch;
        SDL_SetRenderDrawColor(r, c & 0xFF, (c >> 8) & 0xFF, (c >> 16) & 0xFF, 255);
        SDL_RenderFillRect(r, &rc);
        rc.x += cw / 2 - 2;
        rc.y += ch / 2 - 2;
        rc.w = 4;
        rc.h = 4;
        SDL_SetTextureColorMod(gs_white, t & 0xFF, (t >> 8) & 0xFF, (t >> 16) & 0xFF);
        SDL_RenderCopy(r, gs_white, NULL, &rc);
    }
    SDL_SetTextureColorMod(gs_white, 255, 255, 255);
}

static void check_grid(SDL_Renderer *r, int cells, const char *label)
{
    int cols = cells, rows = cells * GSREAD_H / GSREAD_W, cw = GSREAD_W / cols, ch = GSREAD_H / rows, i;
    int bad_fill = 0, bad_tex = 0, got[3];
    char name[96];
#if defined(PS2_SDL_OPT) && defined(__ps2sdk__)
    uint32_t flushes0 = PS2_render_stats.early_flushes;
#endif
    if (!gs_capture(r, draw_grid, cells))
    {
        /* frames were still drawn; report the queue behaviour anyway */
        th_log("GS grid [%s]: %d cells (%d draw calls/frame) drawn 3x; pixel check unavailable",
               label, cols * rows, 2 * cols * rows);
#if defined(PS2_SDL_OPT) && defined(__ps2sdk__)
        th_log("GS grid [%s]: queue %lu B x2, peak %lu B, early flushes during check %lu, oversize drops %lu", label,
               (unsigned long)PS2_render_stats.os_queue_size, (unsigned long)PS2_render_stats.os_queue_peak,
               (unsigned long)(PS2_render_stats.early_flushes - flushes0), (unsigned long)PS2_render_stats.oversize_drops);
#endif
        return;
    }
    for (i = 0; i < cols * rows; i++)
    {
        Uint32 c = cell_colour(i, 1), t = cell_colour(i, 2);
        int x0 = (i % cols) * cw, y0 = (i / cols) * ch;
        /* textured: texel 255 * mod via GS MODULATE; allow rounding */
        if (!gs_px_near(x0 + 1, y0 + 1, c & 0xFF, (c >> 8) & 0xFF, (c >> 16) & 0xFF, 0, got))
            bad_fill++;
        if (!gs_px_near(x0 + cw / 2, y0 + ch / 2, t & 0xFF, (t >> 8) & 0xFF, (t >> 16) & 0xFF, 3, got))
            bad_tex++;
    }
    th_log("GS grid [%s]: %d cells (%d draw calls/frame), wrong fills %d, wrong textured quads %d",
           label, cols * rows, 2 * cols * rows, bad_fill, bad_tex);
#if defined(PS2_SDL_OPT) && defined(__ps2sdk__)
    th_log("GS grid [%s]: queue %lu B x2, peak %lu B, early flushes during check %lu, oversize drops %lu", label,
           (unsigned long)PS2_render_stats.os_queue_size, (unsigned long)PS2_render_stats.os_queue_peak,
           (unsigned long)(PS2_render_stats.early_flushes - flushes0), (unsigned long)PS2_render_stats.oversize_drops);
#endif
    snprintf(name, sizeof(name), "GS grid fills exact [%s]", label);
    th_check(name, bad_fill == 0, "%d wrong", bad_fill);
    snprintf(name, sizeof(name), "GS grid textured colour [%s]", label);
    th_check(name, bad_tex == 0, "%d wrong", bad_tex);
}

static void gs_checks(SDL_Renderer *r)
{
    int got[3];
    Uint32 px[16 * 16];
    int i;
#ifndef __ps2sdk__
    th_log("GS checks skipped on host");
    return;
#endif
    for (i = 0; i < 16 * 16; i++)
        px[i] = 0xFFFFFFFFu;
    /* ABGR8888 is native to the gsKit renderer. (An ARGB8888 texture would be
     * converted by SDL to the first listed format, ABGR1555, and the GS then
     * expands 5-bit white to 248 - seen in the first PCSX2 run.) */
    gs_white = SDL_CreateTexture(r, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, 16, 16);
    SDL_UpdateTexture(gs_white, NULL, px, 16 * 4);
    SDL_RenderSetVSync(r, 0);

    if (gs_capture(r, draw_colour_probe, 0))
    {
        th_check("GS clear colour", gs_px_near(10, 10, 0, 0, 64, 0, got), "got (%d,%d,%d)", got[0], got[1], got[2]);
        th_check("GS untextured fill 128", gs_px_near(130, 130, 128, 128, 128, 0, got), "got (%d,%d,%d)", got[0], got[1], got[2]);
        th_check("GS textured colour mod 128", gs_px_near(230, 130, 128, 128, 128, 2, got), "got (%d,%d,%d) want 128", got[0], got[1], got[2]);
        th_check("GS textured colour mod 255", gs_px_near(330, 130, 255, 255, 255, 2, got), "got (%d,%d,%d) want 255", got[0], got[1], got[2]);
        th_check("GS textured colour mod (200,100,50)", gs_px_near(430, 130, 200, 100, 50, 2, got), "got (%d,%d,%d)", got[0], got[1], got[2]);
        th_check("GS textured alpha 128 over red", gs_px_near(530, 130, 255, 128, 128, 3, got), "got (%d,%d,%d) want (255,128,128)", got[0], got[1], got[2]);
    }
    /* 40x28 cells = 2240 draws: fits the stock 2 MiB queue */
    check_grid(r, 40, "40x28");
#ifdef PS2_SDL_OPT
    /* 80x56 cells = 8960 draws (~2.6 MB of GIF packets): would overrun the
     * stock 2 MiB queue (gsKit does not bounds-check), so opt build only */
    check_grid(r, 80, "80x56");
#endif
    SDL_DestroyTexture(gs_white);
    gs_white = NULL;
    free(gs_frame);
    gs_frame = NULL;
}

static void probe_stubs(SDL_Window *w)
{
    int rc;
    char *clip;
    rc = SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "t", "t", w);
    th_log("STUB ShowSimpleMessageBox rc=%d err=%s", rc, rc ? SDL_GetError() : "");
    rc = SDL_OpenURL("https://example.org");
    th_log("STUB OpenURL rc=%d err=%s", rc, rc ? SDL_GetError() : "");
    {
        void *so = SDL_LoadObject("libfoo.so");
        th_log("STUB LoadObject=%p err=%s", so, so ? "" : SDL_GetError());
    }
    rc = SDL_SetClipboardText("hello");
    clip = SDL_GetClipboardText();
    th_log("STUB Clipboard set rc=%d get=\"%s\"", rc, clip ? clip : "(null)");
    SDL_free(clip);
    SDL_StartTextInput();
    th_log("STUB TextInput active=%d", SDL_IsTextInputActive());
    SDL_StopTextInput();
    rc = SDL_ShowCursor(SDL_DISABLE);
    th_log("STUB ShowCursor rc=%d", rc);
    SDL_DisableScreenSaver();
    SDL_SetWindowTitle(w, "VVVVVV PS2 smoke");
    SDL_SetWindowMinimumSize(w, 320, 240);
    rc = SDL_SetWindowFullscreen(w, SDL_WINDOW_FULLSCREEN_DESKTOP);
    th_log("STUB SetWindowFullscreen rc=%d err=%s", rc, rc ? SDL_GetError() : "");
    {
        char *base = SDL_GetBasePath();
        char *pref = SDL_GetPrefPath("distractionware", "VVVVVV");
        th_log("PATH base=%s pref=%s", base ? base : "(null)", pref ? pref : "(null)");
        SDL_free(base);
        SDL_free(pref);
    }
}

int main(int argc, char **argv)
{
    SDL_Window *win;
    SDL_Renderer *ren;
    SDL_Texture *white;
    SDL_AudioSpec want, have;
    SDL_AudioDeviceID dev;
    SDL_DisplayMode dm;
    int quit = 0, i, rc;
    Uint32 pixels[16 * 16];

#ifdef PS2_SDL_OPT
    th_init("sdl_smoke_opt", argc, argv);
    /* small gsKit queues: exercises the patched queue guard (early flushes) */
    SDL_SetHint("SDL_PS2_RENDER_QUEUE_SIZE", "65536");
    SDL_SetHint("SDL_PS2_RENDER_PERSISTENT_QUEUE_SIZE", "16384");
#else
    th_init("sdl_smoke_stock", argc, argv);
#endif
    th_mem_report("start");
    th_log("SDL compiled %d.%d.%d", SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_PATCHLEVEL);

    /* Big enough for stb_vorbis-style decoding in the audio callback (audit R8). */
    SDL_SetHint(SDL_HINT_THREAD_STACK_SIZE, "131072");

    rc = SDL_Init(SDL_INIT_VIDEO);
    th_check("SDL_Init(VIDEO)", rc == 0, "%s", rc ? SDL_GetError() : "");
    rc = SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER);
    th_check("SDL_Init(GAMECONTROLLER)", rc == 0, "%s", rc ? SDL_GetError() : "");
    rc = SDL_InitSubSystem(SDL_INIT_AUDIO);
    th_check("SDL_Init(AUDIO)", rc == 0, "%s", rc ? SDL_GetError() : "");
    th_log("video driver=%s audio driver=%s",
           SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "(none)",
           SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "(none)");
    if (SDL_GetDesktopDisplayMode(0, &dm) == 0)
        th_log("desktop mode %dx%d@%d %s", dm.w, dm.h, dm.refresh_rate, SDL_GetPixelFormatName(dm.format));
    for (i = 0; i < SDL_GetNumRenderDrivers(); i++)
    {
        SDL_RendererInfo ri;
        SDL_GetRenderDriverInfo(i, &ri);
        th_log("render driver[%d]=%s", i, ri.name);
    }

    /* Same flags VVVVVV uses (Screen.cpp). */
    win = SDL_CreateWindow("smoke", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WIN_W, WIN_H,
                           SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!th_check("SDL_CreateWindow", win != NULL, "%s", win ? "" : SDL_GetError()))
        return th_finish();
    ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE);
#ifndef __ps2sdk__
    /* Host reference build has no accelerated driver; exercise the rest anyway. */
    if (!ren)
    {
        th_log("host: no accelerated renderer (%s), using software", SDL_GetError());
        ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_TARGETTEXTURE);
    }
#endif
    if (!th_check("SDL_CreateRenderer(ACCEL|TARGET)", ren != NULL, "%s", ren ? "" : SDL_GetError()))
        return th_finish();
    SDL_ShowWindow(win);
    log_renderer(ren);
    th_mem_report("after renderer");
    gs_checks(ren);

    for (i = 0; i < 16 * 16; i++)
        pixels[i] = 0xFFFFFFFFu;
    white = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, 16, 16);
    th_check("CreateTexture ARGB8888 static", white != NULL, "%s", white ? "" : SDL_GetError());
    rc = SDL_UpdateTexture(white, NULL, pixels, 16 * 4);
    th_check("UpdateTexture", rc == 0, "%s", rc ? SDL_GetError() : "");
    rc = SDL_SetTextureScaleMode(white, SDL_ScaleModeNearest);
    th_check("SetTextureScaleMode", rc == 0, "%s", rc ? SDL_GetError() : "");

    /* Controllers */
    th_log("joysticks=%d", SDL_NumJoysticks());
    for (i = 0; i < SDL_NumJoysticks() && i < 8; i++)
    {
        SDL_JoystickGUID guid = SDL_JoystickGetDeviceGUID(i);
        char gs[64];
        SDL_JoystickGetGUIDString(guid, gs, sizeof(gs));
        th_log("joystick[%d] name=%s guid=%s is_gamecontroller=%d", i,
               SDL_JoystickNameForIndex(i), gs, SDL_IsGameController(i));
        if (SDL_IsGameController(i) && !pads[i])
        {
            pads[i] = SDL_GameControllerOpen(i);
            if (pads[i])
            {
                char *map = SDL_GameControllerMapping(pads[i]);
                th_log("  controller name=%s type=%d vendor=0x%04x product=0x%04x instance=%d",
                       SDL_GameControllerName(pads[i]), (int)SDL_GameControllerGetType(pads[i]),
                       SDL_GameControllerGetVendor(pads[i]), SDL_GameControllerGetProduct(pads[i]),
                       (int)SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pads[i])));
                th_log("  mapping=%s", map ? map : "(none)");
                SDL_free(map);
            }
        }
    }
    th_check("at least one GameController", SDL_NumJoysticks() > 0 && pads[0] != NULL, NULL);

    /* Audio: same request shape as FAudio_platform_sdl2.c (F32, no allowed changes). */
    SDL_zero(want);
    want.freq = AUDIO_RATE;
    want.format = AUDIO_F32SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = audio_cb;
    dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (th_check("SDL_OpenAudioDevice(F32,44100,2)", dev != 0, "%s", dev ? "" : SDL_GetError()))
    {
        th_log("audio have freq=%d format=0x%x channels=%d samples=%d", have.freq, have.format, have.channels, have.samples);
        SDL_PauseAudioDevice(dev, 0);
    }
    th_mem_report("after audio");

    probe_stubs(win);

    /* Phase order: anything that might hang (busy-wait vsync) runs last. */
    rc = SDL_RenderSetVSync(ren, 0);
    th_log("RenderSetVSync(0) rc=%d", rc);
    run_phase(ren, white, "gsKit vsync=off", 300, 0, &quit);
    run_phase(ren, white, "gsKit vsync=off + Delay(1)", 300, 1, &quit);
    th_set_thread_priority(64);
    run_phase(ren, white, "gsKit vsync=off, main prio 64", 300, 0, &quit);
    th_set_thread_priority(1);

    hw_target_probe(ren, &quit);

    /* Dynamic vsync is read at renderer creation. */
    SDL_DestroyTexture(white);
    SDL_DestroyRenderer(ren);
    SDL_SetHint(SDL_HINT_PS2_DYNAMIC_VSYNC, "1");
    ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (th_check("recreate renderer with PS2_DYNAMIC_VSYNC", ren != NULL, "%s", ren ? "" : SDL_GetError()))
    {
        white = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, 16, 16);
        SDL_UpdateTexture(white, NULL, pixels, 16 * 4);
        run_phase(ren, white, "gsKit dynamic vsync", 300, 0, &quit);
        /* Interactive tail: 10 s to press buttons (START+SELECT quits). */
        th_log("input window: press pad buttons now (10 s)");
        run_phase(ren, white, "gsKit input window", 600, 0, &quit);
        SDL_DestroyTexture(white);
        SDL_DestroyRenderer(ren);
    }

    /* Plain vsync: gsKit_vsync_wait() busy-spins on GS_CSR at main-thread priority. */
    SDL_SetHint(SDL_HINT_PS2_DYNAMIC_VSYNC, "0");
    ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (th_check("recreate renderer with plain vsync", ren != NULL, "%s", ren ? "" : SDL_GetError()))
    {
        white = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, 16, 16);
        SDL_UpdateTexture(white, NULL, pixels, 16 * 4);
        th_log("starting plain vsync phase (busy-wait)");
        run_phase(ren, white, "gsKit vsync=on (busy-wait)", 300, 0, &quit);
        SDL_DestroyTexture(white);
        SDL_DestroyRenderer(ren);
    }

    th_mem_report("end");
    th_summary();
    if (dev)
        SDL_CloseAudioDevice(dev);
    th_log("teardown: audio closed");
    SDL_DestroyWindow(win);
    th_log("teardown: window destroyed");
    SDL_Quit();
    return th_finish();
}
