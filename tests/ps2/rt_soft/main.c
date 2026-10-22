/* rt_soft: validates the audit's "Option A" render strategy on PS2:
 * SDL's software renderer (which implements render targets) presented
 * through SDL's texture-framebuffer fallback on the PS2 gsKit renderer.
 *
 * Part 1 - correctness: the render-target operations VVVVVV relies on
 * (Graphics.cpp / GraphicsResources.cpp / Screen.cpp), each verified by
 * reading pixels back from the target.
 * Part 2 - performance: a VVVVVV-shaped frame (tile layers, sprites with
 * per-draw colour/alpha mod, text glyphs, nested target copies, final
 * scaled copy) timed per stage, with an F32 audio callback running.
 *
 * No game assets are used; all textures are generated procedurally. */
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
#define SCREEN_W 320
#define SCREEN_H 240
#define SCROLL_W (SCREEN_W + 16) /* CREATE_SCROLL_TEXTURE: 336x336 */
#define AUDIO_RATE 44100

/* ---------- audio (same request shape as FAudio's SDL2 platform) ---------- */

static SDL_atomic_t audio_frames;
static float phase_acc;

static void SDLCALL audio_cb(void *userdata, Uint8 *stream, int len)
{
    float *out = (float *)stream;
    int frames = len / (int)(2 * sizeof(float));
    int i;
    (void)userdata;
    for (i = 0; i < frames; i++)
    {
        float v = 0.1f * sinf(phase_acc);
        phase_acc += 2.0f * 3.14159265f * 330.0f / AUDIO_RATE;
        if (phase_acc > 2.0f * 3.14159265f)
            phase_acc -= 2.0f * 3.14159265f;
        out[2 * i] = v;
        out[2 * i + 1] = v;
    }
    SDL_AtomicAdd(&audio_frames, frames);
}

/* ---------- helpers ---------- */

static SDL_Texture *make_texture(SDL_Renderer *r, int w, int h, const Uint32 *argb)
{
    SDL_Texture *t = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, w, h);
    if (t)
    {
        SDL_UpdateTexture(t, NULL, argb, w * 4);
        SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    }
    return t;
}

static SDL_Texture *make_solid(SDL_Renderer *r, int w, int h, Uint32 argb)
{
    Uint32 *px = (Uint32 *)malloc((size_t)w * h * 4);
    SDL_Texture *t;
    int i;
    for (i = 0; i < w * h; i++)
        px[i] = argb;
    t = make_texture(r, w, h, px);
    free(px);
    return t;
}

static SDL_Texture *make_target(SDL_Renderer *r, int w, int h)
{
    /* Graphics.cpp CREATE_TEXTURE_WITH_DIMENSIONS */
    SDL_Texture *t = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, w, h);
    if (t)
        SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    return t;
}

static Uint32 read_px(SDL_Renderer *r, int x, int y)
{
    Uint32 px = 0;
    SDL_Rect rc;
    rc.x = x; rc.y = y; rc.w = 1; rc.h = 1;
    if (SDL_RenderReadPixels(r, &rc, SDL_PIXELFORMAT_ARGB8888, &px, 4) != 0)
        th_log("ReadPixels(%d,%d) failed: %s", x, y, SDL_GetError());
    return px;
}

static int chan_near(int a, int b, int tol)
{
    return abs(a - b) <= tol;
}

/* Compare ARGB pixel against expected (a<0 = ignore alpha). */
static int px_check(const char *name, Uint32 got, int a, int r, int g, int b, int tol)
{
    int ga = (got >> 24) & 0xFF, gr = (got >> 16) & 0xFF, gg = (got >> 8) & 0xFF, gb = got & 0xFF;
    int ok = chan_near(gr, r, tol) && chan_near(gg, g, tol) && chan_near(gb, b, tol) &&
             (a < 0 || chan_near(ga, a, tol));
    return th_check(name, ok, "got a=%d rgb=(%d,%d,%d) want a=%d rgb=(%d,%d,%d) tol=%d",
                    ga, gr, gg, gb, a, r, g, b, tol);
}

static void fill_rect(SDL_Renderer *r, int x, int y, int w, int h)
{
    SDL_Rect rc;
    rc.x = x; rc.y = y; rc.w = w; rc.h = h;
    SDL_RenderFillRect(r, &rc);
}

static void copy_to(SDL_Renderer *r, SDL_Texture *t, int x, int y, int w, int h)
{
    SDL_Rect rc;
    rc.x = x; rc.y = y; rc.w = w; rc.h = h;
    SDL_RenderCopy(r, t, NULL, &rc);
}

/* ---------- part 1: correctness ---------- */

static void correctness(SDL_Renderer *r, SDL_Window *win)
{
    SDL_Texture *t1 = make_target(r, SCREEN_W, SCREEN_H);  /* like gameTexture */
    SDL_Texture *t2 = make_target(r, SCROLL_W, SCROLL_W);  /* like backgroundTexture */
    SDL_Texture *t3 = make_target(r, 240, 180);            /* like im_image12 */
    SDL_Texture *solid = make_solid(r, 8, 8, 0xFFC864FAu); /* rgb (200,100,250) */
    SDL_Texture *half = make_solid(r, 8, 8, 0x80FFFFFFu);  /* white, alpha 128 */
    SDL_Texture *stream;
    Uint32 lr[2] = {0xFFFF0000u, 0xFF0000FFu};             /* left red, right blue */
    SDL_Texture *flipt = make_texture(r, 2, 1, lr);
    SDL_Texture *flipv = make_texture(r, 1, 2, lr);        /* top red, bottom blue */
    SDL_Texture *saved;
    int rc;

    th_log("--- correctness ---");
    th_check("create targets 320x240/336x336/240x180", t1 && t2 && t3, "%s", (t1 && t2 && t3) ? "" : SDL_GetError());
    if (!(t1 && t2 && t3 && solid && half && flipt && flipv))
    {
        th_log("setup failed: %s", SDL_GetError());
        return;
    }

    rc = SDL_SetRenderTarget(r, t1);
    th_check("SetRenderTarget(t1)", rc == 0, "%s", rc ? SDL_GetError() : "");
    th_check("GetRenderTarget == t1", SDL_GetRenderTarget(r) == t1, NULL);

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(r, 10, 20, 30, 255);
    SDL_RenderClear(r);
    px_check("clear target", read_px(r, 5, 5), 255, 10, 20, 30, 0);

    /* Graphics.cpp draw_grid_tile: colour mod set, draw, reset to 255 */
    SDL_SetTextureColorMod(solid, 128, 255, 64);
    copy_to(r, solid, 16, 16, 8, 8);
    SDL_SetTextureColorMod(solid, 255, 255, 255);
    px_check("colour mod (128,255,64)", read_px(r, 20, 20), 255, 100, 100, 63, 2);

    SDL_SetTextureAlphaMod(solid, 128);
    copy_to(r, solid, 32, 16, 8, 8);
    SDL_SetTextureAlphaMod(solid, 255);
    px_check("alpha mod 128 BLEND over bg", read_px(r, 36, 20), -1, 105, 60, 140, 3);

    copy_to(r, half, 48, 16, 8, 8);
    px_check("per-pixel alpha 128 BLEND", read_px(r, 52, 20), -1, 133, 138, 143, 3);

    SDL_SetTextureBlendMode(half, SDL_BLENDMODE_NONE);
    copy_to(r, half, 64, 16, 8, 8);
    SDL_SetTextureBlendMode(half, SDL_BLENDMODE_BLEND);
    px_check("BLENDMODE_NONE overwrites incl. alpha", read_px(r, 68, 20), 128, 255, 255, 255, 1);

    {
        SDL_Rect dst = {80, 16, 2, 1};
        SDL_RenderCopyEx(r, flipt, NULL, &dst, 0.0, NULL, SDL_FLIP_HORIZONTAL);
        px_check("RenderCopyEx FLIP_HORIZONTAL", read_px(r, 80, 16), 255, 0, 0, 255, 0);
        dst.x = 90; dst.w = 1; dst.h = 2;
        SDL_RenderCopyEx(r, flipv, NULL, &dst, 0.0, NULL, SDL_FLIP_VERTICAL);
        px_check("RenderCopyEx FLIP_VERTICAL", read_px(r, 90, 16), 255, 0, 0, 255, 0);
    }

    /* Draw primitives into a target */
    SDL_SetRenderDrawColor(r, 0, 255, 0, 255);
    SDL_RenderDrawPoint(r, 100, 16);
    px_check("DrawPoint", read_px(r, 100, 16), 255, 0, 255, 0, 0);
    SDL_RenderDrawLine(r, 110, 10, 110, 30);
    px_check("DrawLine", read_px(r, 110, 20), 255, 0, 255, 0, 0);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 255, 255, 255, 128);
    fill_rect(r, 120, 16, 8, 8);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    px_check("FillRect alpha 128 BLEND", read_px(r, 124, 20), -1, 133, 138, 143, 3);

    /* Nested targets: t2 -> t3 -> t1, with GetRenderTarget save/restore
     * (Graphics.cpp pattern: SDL_Texture* prev = SDL_GetRenderTarget(...)) */
    saved = SDL_GetRenderTarget(r);
    SDL_SetRenderTarget(r, t2);
    SDL_SetRenderDrawColor(r, 0, 200, 0, 255);
    SDL_RenderClear(r);
    SDL_SetRenderTarget(r, t3);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 0);
    SDL_RenderClear(r);
    copy_to(r, t2, 0, 0, 240, 180);
    SDL_SetRenderTarget(r, saved);
    th_check("GetRenderTarget restore", SDL_GetRenderTarget(r) == t1, NULL);
    copy_to(r, t3, 140, 16, 16, 16);
    px_check("nested targets t2->t3->t1", read_px(r, 148, 24), 255, 0, 200, 0, 0);
    px_check("t1 content survived target switches", read_px(r, 20, 20), 255, 100, 100, 63, 2);

    /* Streaming texture updated from CPU (GraphicsUtil.cpp ApplyFilter path) */
    stream = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, SCREEN_W, SCREEN_H);
    if (th_check("create streaming 320x240", stream != NULL, "%s", stream ? "" : SDL_GetError()))
    {
        Uint32 *buf = (Uint32 *)malloc(SCREEN_W * SCREEN_H * 4);
        int i;
        for (i = 0; i < SCREEN_W * SCREEN_H; i++)
            buf[i] = 0xFF112233u;
        rc = SDL_UpdateTexture(stream, NULL, buf, SCREEN_W * 4);
        th_check("UpdateTexture streaming", rc == 0, "%s", rc ? SDL_GetError() : "");
        SDL_SetTextureBlendMode(stream, SDL_BLENDMODE_NONE);
        {
            SDL_Rect src = {0, 0, 8, 8};
            SDL_Rect dst = {170, 16, 8, 8};
            SDL_RenderCopy(r, stream, &src, &dst);
        }
        px_check("streaming texture copy", read_px(r, 172, 18), 255, 0x11, 0x22, 0x33, 0);
        free(buf);
    }

    /* Texture from an ABGR8888 surface (GraphicsResources.cpp LoadImageRaw) */
    {
        Uint32 abgr[4] = {0xFF0080FFu, 0xFF0080FFu, 0xFF0080FFu, 0xFF0080FFu}; /* r=255 g=128 b=0 */
        SDL_Surface *s = SDL_CreateRGBSurfaceWithFormatFrom(abgr, 2, 2, 32, 8, SDL_PIXELFORMAT_ABGR8888);
        SDL_Texture *ts = s ? SDL_CreateTextureFromSurface(r, s) : NULL;
        th_check("CreateTextureFromSurface(ABGR8888)", ts != NULL, "%s", ts ? "" : SDL_GetError());
        if (ts)
        {
            copy_to(r, ts, 180, 16, 2, 2);
            px_check("ABGR surface texture colours", read_px(r, 180, 16), 255, 255, 128, 0, 0);
            SDL_DestroyTexture(ts);
        }
        SDL_FreeSurface(s);
    }

    rc = SDL_SetTextureScaleMode(t1, SDL_ScaleModeLinear);
    th_check("SetTextureScaleMode(linear) on target", rc == 0, "%s", rc ? SDL_GetError() : "");
    SDL_SetTextureScaleMode(t1, SDL_ScaleModeNearest);

    /* Final presentation path: game target -> window, vertical flip (flip mode) */
    SDL_SetRenderTarget(r, NULL);
    th_check("GetRenderTarget == NULL after reset", SDL_GetRenderTarget(r) == NULL, NULL);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    SDL_RenderClear(r);
    SDL_SetTextureBlendMode(t1, SDL_BLENDMODE_NONE);
    {
        int ow = 0, oh = 0;
        SDL_Rect dst = {0, 0, 0, 0};
        SDL_GetRendererOutputSize(r, &ow, &oh);
        dst.w = ow;
        dst.h = oh;
        th_log("software renderer output size %dx%d", ow, oh);
        SDL_RenderCopyEx(r, t1, NULL, &dst, 0.0, NULL, SDL_FLIP_NONE);
        /* t1 pixel (20,20) is inside the 8x8 colour-mod quad at 16..23 */
        px_check("window: copy of target", read_px(r, 20 * ow / SCREEN_W + 1, 20 * oh / SCREEN_H + 1), -1, 100, 100, 63, 8);
    }
    SDL_RenderPresent(r);
    {
        SDL_Surface *ws = SDL_GetWindowSurface(win);
        th_log("window surface: %s", ws ? SDL_GetPixelFormatName(ws->format->format) : SDL_GetError());
    }

    SDL_DestroyTexture(stream);
    SDL_DestroyTexture(flipv);
    SDL_DestroyTexture(flipt);
    SDL_DestroyTexture(half);
    SDL_DestroyTexture(solid);
    SDL_DestroyTexture(t3);
    SDL_DestroyTexture(t2);
    SDL_DestroyTexture(t1);
}

/* ---------- part 2: VVVVVV-shaped frame benchmark ---------- */

typedef struct
{
    SDL_Texture *targets[11];
    SDL_Texture *tiles, *sprites, *font, *stream;
    Uint32 *stream_buf;
} scene;

enum { T_GAME, T_GAMEPLAY, T_MENU, T_GHOST, T_SHAKE, T_FG, T_BG, T_TMPSCROLL, T_TOWERBG, T_TITLEBG, T_MINIMAP };

static Uint32 hash32(Uint32 x)
{
    x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
    return x;
}

static SDL_Texture *gen_sheet(SDL_Renderer *r, int w, int h, int cell, int transparent_border)
{
    Uint32 *px = (Uint32 *)malloc((size_t)w * h * 4);
    SDL_Texture *t;
    int x, y;
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++)
        {
            Uint32 c = hash32((Uint32)((x / cell) * 977 + (y / cell)));
            int edge = transparent_border && ((x % cell) == 0 || (y % cell) == 0);
            px[y * w + x] = edge ? 0x00000000u : (0xFF000000u | (c & 0x00FFFFFFu));
        }
    t = make_texture(r, w, h, px);
    free(px);
    return t;
}

static int scene_create(SDL_Renderer *r, scene *s)
{
    static const int dims[11][2] = {
        {SCREEN_W, SCREEN_H}, {SCREEN_W, SCREEN_H}, {SCREEN_W, SCREEN_H},
        {SCREEN_W, SCREEN_H}, {SCREEN_W, SCREEN_H}, {SCREEN_W, SCREEN_H},
        {SCROLL_W, SCROLL_W}, {SCROLL_W, SCROLL_W}, {SCROLL_W, SCROLL_W},
        {SCROLL_W, SCROLL_W}, {240, 180}};
    int i, ok = 1;
    SDL_zerop(s);
    for (i = 0; i < 11; i++)
    {
        s->targets[i] = make_target(r, dims[i][0], dims[i][1]);
        ok = ok && s->targets[i];
    }
    th_check("create all 11 VVVVVV render targets", ok, "%s", ok ? "" : SDL_GetError());
    /* Placeholder sheets with VVVVVV's grid sizes (dimensions are guesses; see audit 6.5) */
    s->tiles = gen_sheet(r, 320, 240, 8, 1);
    s->sprites = gen_sheet(r, 384, 128, 32, 1);
    s->font = gen_sheet(r, 128, 512, 8, 1);
    s->stream = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, SCREEN_W, SCREEN_H);
    s->stream_buf = (Uint32 *)malloc(SCREEN_W * SCREEN_H * 4);
    return ok && s->tiles && s->sprites && s->font && s->stream && s->stream_buf;
}

static void scene_destroy(scene *s)
{
    int i;
    for (i = 0; i < 11; i++)
        if (s->targets[i])
            SDL_DestroyTexture(s->targets[i]);
    SDL_DestroyTexture(s->tiles);
    SDL_DestroyTexture(s->sprites);
    SDL_DestroyTexture(s->font);
    SDL_DestroyTexture(s->stream);
    free(s->stream_buf);
}

static void draw_tile(SDL_Renderer *r, SDL_Texture *sheet, int cell, int sheet_cols, int idx, int x, int y)
{
    SDL_Rect src, dst;
    src.x = (idx % sheet_cols) * cell; src.y = (idx / sheet_cols) * cell; src.w = cell; src.h = cell;
    dst.x = x; dst.y = y; dst.w = cell; dst.h = cell;
    SDL_RenderCopy(r, sheet, &src, &dst);
}

/* Stage indices for timing */
enum { S_FG, S_BG, S_GAMEPLAY, S_TEXT, S_GAMECOPY, S_FINALCOPY, S_PRESENT, S_COUNT };
static const char *stage_names[S_COUNT] = {"fg tiles (1200)", "bg scroll", "gameplay compose", "text+ui",
                                           "game copy", "final copy (scale/convert)", "present"};

/* Graphics::get_stretch_info, SCALING_LETTERBOX, from SDL_GetRendererOutputSize
 * exactly as VVVVVV 2.4 does it. */
static void get_stretch_info(SDL_Renderer *r, SDL_Rect *rect)
{
    int width = 0, height = 0;
    SDL_GetRendererOutputSize(r, &width, &height);
    if (width * SCREEN_H > height * SCREEN_W)
    {
        rect->x = (width - height * SCREEN_W / SCREEN_H) / 2;
        rect->y = 0;
        rect->w = height * SCREEN_W / SCREEN_H;
        rect->h = height;
    }
    else
    {
        rect->x = 0;
        rect->y = (height - width * SCREEN_H / SCREEN_W) / 2;
        rect->w = width;
        rect->h = width * SCREEN_H / SCREEN_W;
    }
}

static void render_frame(SDL_Renderer *r, scene *s, int frame, int redraw_fg, uint64_t *stage_us)
{
    int i, x, y;
    uint64_t t = th_now_us(), t2;
#define STAGE(n) do { SDL_RenderFlush(r); t2 = th_now_us(); stage_us[n] += t2 - t; t = t2; } while (0)

    /* Foreground tiles: VVVVVV redraws this only on room change. */
    if (redraw_fg)
    {
        SDL_SetRenderTarget(r, s->targets[T_FG]);
        SDL_SetRenderDrawColor(r, 0, 0, 0, 0);
        SDL_RenderClear(r);
        for (y = 0; y < 30; y++)
            for (x = 0; x < 40; x++)
                draw_tile(r, s->tiles, 8, 40, (x * 7 + y * 13 + frame) % 1200, x * 8, y * 8);
    }
    STAGE(S_FG);

    /* Scrolling background: copy to temp with offset, copy back, draw new strip + stars */
    SDL_SetRenderTarget(r, s->targets[T_TMPSCROLL]);
    {
        SDL_Rect src = {0, 0, SCROLL_W, SCROLL_W}, dst = {0, 2, SCROLL_W, SCROLL_W};
        SDL_RenderCopy(r, s->targets[T_BG], &src, &dst);
    }
    SDL_SetRenderTarget(r, s->targets[T_BG]);
    SDL_SetTextureBlendMode(s->targets[T_TMPSCROLL], SDL_BLENDMODE_NONE);
    SDL_RenderCopy(r, s->targets[T_TMPSCROLL], NULL, NULL);
    SDL_SetTextureBlendMode(s->targets[T_TMPSCROLL], SDL_BLENDMODE_BLEND);
    for (x = 0; x < 42; x++)
        draw_tile(r, s->tiles, 8, 40, (x + frame) % 1200, x * 8, 0);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    for (i = 0; i < 50; i++)
    {
        SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
        fill_rect(r, (int)(hash32(i) % SCROLL_W), (int)((hash32(i + 99) + frame * (1 + i % 3)) % SCROLL_W), 2, 2);
    }
    STAGE(S_BG);

    /* Gameplay: background + foreground + sprites with colour/alpha mod and flips */
    SDL_SetRenderTarget(r, s->targets[T_GAMEPLAY]);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    SDL_RenderClear(r);
    {
        SDL_Rect src = {8, 8, SCREEN_W, SCREEN_H};
        SDL_RenderCopy(r, s->targets[T_BG], &src, NULL);
    }
    SDL_RenderCopy(r, s->targets[T_FG], NULL, NULL);
    for (i = 0; i < 25; i++)
    {
        SDL_Rect src, dst;
        int idx = (i + frame / 8) % 48;
        src.x = (idx % 12) * 32; src.y = (idx / 12) * 32; src.w = 32; src.h = 32;
        dst.x = (int)((hash32(i) + frame * (i % 5)) % (SCREEN_W - 32));
        dst.y = (int)(hash32(i * 31) % (SCREEN_H - 32));
        dst.w = 32; dst.h = 32;
        SDL_SetTextureColorMod(s->sprites, (Uint8)(hash32(i) & 0xFF), (Uint8)(hash32(i + 1) & 0xFF), 200);
        SDL_SetTextureAlphaMod(s->sprites, (i % 4 == 0) ? 128 : 255);
        SDL_RenderCopyEx(r, s->sprites, &src, &dst, 0.0, NULL,
                         (i % 3 == 0) ? SDL_FLIP_HORIZONTAL : ((i % 3 == 1) ? SDL_FLIP_VERTICAL : SDL_FLIP_NONE));
        SDL_SetTextureColorMod(s->sprites, 255, 255, 255);
        SDL_SetTextureAlphaMod(s->sprites, 255);
    }
    STAGE(S_GAMEPLAY);

    /* Text box: translucent fill + 200 coloured 8x8 glyphs, minimap-sized target */
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0, 0, 80, 160);
    fill_rect(r, 16, 150, 288, 80);
    for (i = 0; i < 200; i++)
    {
        SDL_SetTextureColorMod(s->font, 164, 164, 255);
        draw_tile(r, s->font, 8, 16, 32 + (i * 7 + frame) % 96, 20 + (i % 35) * 8, 156 + (i / 35) * 10);
        SDL_SetTextureColorMod(s->font, 255, 255, 255);
    }
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    STAGE(S_TEXT);

    /* gameplay -> game texture */
    SDL_SetRenderTarget(r, s->targets[T_GAME]);
    SDL_SetTextureBlendMode(s->targets[T_GAMEPLAY], SDL_BLENDMODE_NONE);
    SDL_RenderCopy(r, s->targets[T_GAMEPLAY], NULL, NULL);
    SDL_SetTextureBlendMode(s->targets[T_GAMEPLAY], SDL_BLENDMODE_BLEND);
    STAGE(S_GAMECOPY);

    /* Final: VVVVVV's letterbox copy of gameTexture to the window */
    SDL_SetRenderTarget(r, NULL);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    SDL_RenderClear(r);
    {
        SDL_Rect dst;
        get_stretch_info(r, &dst);
        SDL_SetTextureBlendMode(s->targets[T_GAME], SDL_BLENDMODE_NONE);
        SDL_RenderCopyEx(r, s->targets[T_GAME], NULL, &dst, 0.0, NULL, SDL_FLIP_NONE);
    }
    STAGE(S_FINALCOPY);
    SDL_RenderPresent(r);
    STAGE(S_PRESENT);
#undef STAGE
}

/* ---------- patched-backend diagnostics (opt build only) ---------- */

#if defined(PS2_SDL_OPT) && defined(__ps2sdk__) /* backend stats exist only in the PS2 build */
/* Mirrors PS2_RenderStats in patches/sdl2 (SDL_render_ps2.c). */
typedef struct
{
    uint64_t presents, update_us, queue_build_us, exec_us, finish_us, vsync_us;
    uint32_t os_queue_size, per_queue_size, os_queue_peak, per_queue_peak, early_flushes, oversize_drops;
} PS2_RenderStats;
extern PS2_RenderStats PS2_render_stats;
#endif

static void stats_reset(void)
{
#if defined(PS2_SDL_OPT) && defined(__ps2sdk__) /* backend stats exist only in the PS2 build */
    uint32_t os = PS2_render_stats.os_queue_size, per = PS2_render_stats.per_queue_size;
    SDL_zero(PS2_render_stats);
    PS2_render_stats.os_queue_size = os;
    PS2_render_stats.per_queue_size = per;
#endif
}

static void stats_report(const char *label, int frames)
{
#if defined(PS2_SDL_OPT) && defined(__ps2sdk__) /* backend stats exist only in the PS2 build */
    const PS2_RenderStats *st = &PS2_render_stats;
    th_log("BACKEND [%s] per frame: texture upload (copy into gsKit texture) %lu us, queue build %lu us, "
           "GS submit (exec) %lu us, GS finish %lu us, vsync %lu us",
           label,
           (unsigned long)(st->update_us / frames), (unsigned long)(st->queue_build_us / frames),
           (unsigned long)(st->exec_us / frames), (unsigned long)(st->finish_us / frames),
           (unsigned long)(st->vsync_us / frames));
    th_log("QUEUE [%s] oneshot %lu B x2, persistent %lu B; peak used oneshot %lu B, persistent %lu B; "
           "early flushes %lu, oversize drops %lu",
           label, (unsigned long)st->os_queue_size, (unsigned long)st->per_queue_size,
           (unsigned long)st->os_queue_peak, (unsigned long)st->per_queue_peak,
           (unsigned long)st->early_flushes, (unsigned long)st->oversize_drops);
#else
    (void)label;
    (void)frames;
#endif
}

static void benchmark(SDL_Renderer *r, scene *s, const char *label, int frames, int redraw_fg_every_frame)
{
    th_stats st = {{0}, 0};
    uint64_t stage_us[S_COUNT] = {0};
    uint64_t t0, prev, now;
    int f, i, a0;
    char name[128];
    SDL_Event e;

    th_log("--- benchmark: %s (%d frames) ---", label, frames);
    /* warm-up frame draws the cached foreground once */
    render_frame(r, s, 0, 1, stage_us);
    for (i = 0; i < S_COUNT; i++)
        stage_us[i] = 0;
    stats_reset();
    a0 = SDL_AtomicGet(&audio_frames);
    t0 = prev = th_now_us();
    /* At least `frames` frames and at least 3 s, so the audio rate is measurable. */
    for (f = 1; f <= frames || prev - t0 < 3000000; f++)
    {
        while (SDL_PollEvent(&e))
        {
            if (e.type == SDL_CONTROLLERBUTTONDOWN)
                th_log("EVENT button %s down", SDL_GameControllerGetStringForButton((SDL_GameControllerButton)e.cbutton.button));
        }
        render_frame(r, s, f, redraw_fg_every_frame, stage_us);
        now = th_now_us();
        th_stats_add(&st, (uint32_t)(now - prev));
        prev = now;
    }
    frames = f - 1;
    snprintf(name, sizeof(name), "frame [%s]", label);
    th_stats_report(&st, name);
    for (i = 0; i < S_COUNT; i++)
        th_log("STAGE %-26s mean=%lu us", stage_names[i], (unsigned long)(stage_us[i] / frames));
    stats_report(label, frames);
    {
        uint64_t dt = th_now_us() - t0;
        double rate = (double)(SDL_AtomicGet(&audio_frames) - a0) * 1000000.0 / (double)dt;
        double fps = (double)frames * 1000000.0 / (double)dt;
        th_log("FRAMES [%s] %d in %lu us", label, frames, (unsigned long)dt);
        th_log("AUDIO [%s] rate=%.0f Hz (expected %d)", label, rate, AUDIO_RATE);
        th_log("FPS   [%s] %.2f", label, fps);
        snprintf(name, sizeof(name), "audio keeps up [%s]", label);
        th_check(name, rate > AUDIO_RATE * 0.95, "%.1f%% of real time", rate * 100.0 / AUDIO_RATE);
    }
}

/* Deterministic reference frame: all targets cleared, one frame with a fixed
 * index, fg redrawn. The software renderer is the same SDL code on host and
 * PS2, so rt_soft_game.bmp should match the host reference bit for bit. */
static void render_evidence(SDL_Renderer *r, scene *s)
{
    uint64_t dummy[S_COUNT] = {0};
    int i;
    for (i = 0; i < 11; i++)
    {
        SDL_SetRenderTarget(r, s->targets[i]);
        SDL_SetRenderDrawColor(r, 0, 0, 0, 0);
        SDL_RenderClear(r);
    }
    render_frame(r, s, 7, 1, dummy);
}

static void save_evidence(SDL_Renderer *r, scene *s, const char *path)
{
    SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormat(0, SCREEN_W, SCREEN_H, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!surf)
        return;
    render_evidence(r, s);
    SDL_SetRenderTarget(r, s->targets[T_GAME]);
    if (SDL_RenderReadPixels(r, NULL, SDL_PIXELFORMAT_ARGB8888, surf->pixels, surf->pitch) == 0)
        th_log("saved %s rc=%d", path, SDL_SaveBMP(surf, path));
    SDL_SetRenderTarget(r, NULL);
    SDL_FreeSurface(surf);
}

/* Compare what the GS displays against the software window surface, scaled
 * to the GS framebuffer with nearest sampling (+-1 source pixel allowed for
 * the rasteriser's sampling position). Catches wrong scaling, aspect, clipping
 * and colour range (the SDL 2.32.10 0..255 vs 0x80 issue shows as ~2x). */
static void verify_gs_output(SDL_Renderer *r, scene *s, const char *label, int tol)
{
#ifdef __ps2sdk__
    int ow = 0, oh = 0, i, x, y, b;
    Uint32 *expect;
    uint8_t *gs[2];
    unsigned long match = 0, total = (unsigned long)GSREAD_W * GSREAD_H, sum_gs = 0, sum_ex = 0;
    int max_delta = 0, buffers_equal;
    char name[128];

    SDL_GetRendererOutputSize(r, &ow, &oh);
    render_evidence(r, s); /* ends with present */
    for (i = 0; i < 3; i++)
    {
        /* same content into both GS buffers */
        SDL_Rect dst;
        SDL_SetRenderTarget(r, NULL);
        SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
        SDL_RenderClear(r);
        get_stretch_info(r, &dst);
        SDL_RenderCopy(r, s->targets[T_GAME], NULL, &dst);
        SDL_RenderPresent(r);
    }
    expect = (Uint32 *)malloc((size_t)ow * oh * 4);
    gs[0] = (uint8_t *)memalign(64, GSREAD_W * GSREAD_H * 3);
    gs[1] = (uint8_t *)memalign(64, GSREAD_W * GSREAD_H * 3);
    if (!expect || !gs[0] || !gs[1] || SDL_RenderReadPixels(r, NULL, SDL_PIXELFORMAT_ARGB8888, expect, ow * 4) != 0)
    {
        th_log("GS verify [%s]: setup failed: %s", label, SDL_GetError());
        goto out;
    }
    for (b = 0; b < 2; b++)
    {
        if (gsread_framebuffer(b, gs[b]) != 0)
        {
            th_log("GS verify [%s]: readback unsupported", label);
            goto out;
        }
    }
    if (gsread_is_blank(gs[0]) && gsread_is_blank(gs[1]))
    {
        th_log("SKIP GS verify [%s]: readback returned only zeros (no GS->EE transfer in this emulator?)", label);
        goto out;
    }
    buffers_equal = memcmp(gs[0], gs[1], GSREAD_W * GSREAD_H * 3) == 0;
    th_log("GS verify [%s]: window surface %dx%d -> GS %dx%d, both GS buffers equal=%d",
           label, ow, oh, GSREAD_W, GSREAD_H, buffers_equal);
    for (y = 0; y < GSREAD_H; y++)
        for (x = 0; x < GSREAD_W; x++)
        {
            const uint8_t *p = gs[0] + (y * GSREAD_W + x) * 3;
            int sx = x * ow / GSREAD_W, sy = y * oh / GSREAD_H, dx, dy, best = 1000;
            for (dy = -1; dy <= 1; dy++)
                for (dx = -1; dx <= 1; dx++)
                {
                    int cx = sx + dx, cy = sy + dy, d;
                    Uint32 e;
                    if (cx < 0 || cy < 0 || cx >= ow || cy >= oh)
                        continue;
                    e = expect[cy * ow + cx];
                    d = SDL_max(SDL_max(abs(p[0] - (int)((e >> 16) & 0xFF)), abs(p[1] - (int)((e >> 8) & 0xFF))),
                                abs(p[2] - (int)(e & 0xFF)));
                    if (d < best)
                        best = d;
                }
            if (best <= tol)
                match++;
            if (best > max_delta)
                max_delta = best;
            {
                Uint32 e = expect[sy * ow + sx];
                sum_gs += p[0] + p[1] + p[2];
                sum_ex += ((e >> 16) & 0xFF) + ((e >> 8) & 0xFF) + (e & 0xFF);
            }
        }
    th_log("GS verify [%s]: %lu/%lu pixels within %d, max delta %d, brightness GS/expected = %.3f",
           label, match, total, tol, max_delta, sum_ex ? (double)sum_gs / (double)sum_ex : 0.0);
    snprintf(name, sizeof(name), "GS output matches window surface [%s]", label);
    th_check(name, match * 1000 >= total * 995, "%.2f%% of pixels", match * 100.0 / total);
out:
    free(expect);
    free(gs[0]);
    free(gs[1]);
#else
    (void)r;
    (void)s;
    (void)label;
    (void)tol;
#endif
}

typedef struct
{
    const char *name;
    const char *window_pixel_size; /* SDL_HINT_PS2_WINDOW_PIXEL_SIZE, NULL = unset */
    const char *prefer_xbgr;       /* SDL_HINT_PS2_PREFER_XBGR8888 */
    const char *queue_size;        /* SDL_HINT_PS2_RENDER_QUEUE_SIZE */
    const char *per_queue_size;    /* SDL_HINT_PS2_RENDER_PERSISTENT_QUEUE_SIZE */
    int gs_tol;                    /* GS verify tolerance: 8 for a 16-bit framebuffer */
    const char *sw_fast;           /* SDL_HINT_SW_BLIT_FASTPATHS (patch 0002) */
    const char *batching;          /* SDL_HINT_RENDER_BATCHING */
} config;

static SDL_Window *open_window(SDL_Renderer **ren, const config *c)
{
    SDL_Window *win;
    SDL_RendererInfo info;
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    SDL_SetHint(SDL_HINT_RENDER_VSYNC, "1");
    SDL_SetHint(SDL_HINT_PS2_DYNAMIC_VSYNC, "1");
    SDL_SetHint("SDL_PS2_WINDOW_PIXEL_SIZE", c->window_pixel_size);
    SDL_SetHint("SDL_PS2_PREFER_XBGR8888", c->prefer_xbgr);
    SDL_SetHint("SDL_PS2_RENDER_QUEUE_SIZE", c->queue_size);
    SDL_SetHint("SDL_PS2_RENDER_PERSISTENT_QUEUE_SIZE", c->per_queue_size);
    SDL_SetHint("SDL_SW_BLIT_FASTPATHS", c->sw_fast);
    SDL_SetHint(SDL_HINT_RENDER_BATCHING, c->batching);
    th_log("=== config: %s (pixel size %s, prefer xbgr %s, queue %s/%s, sw fastpaths %s) ===", c->name,
           c->window_pixel_size ? c->window_pixel_size : "-", c->prefer_xbgr ? c->prefer_xbgr : "-",
           c->queue_size ? c->queue_size : "default", c->per_queue_size ? c->per_queue_size : "default",
           c->sw_fast ? c->sw_fast : "-");
    win = SDL_CreateWindow("rt_soft", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WIN_W, WIN_H,
                           SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!th_check("CreateWindow", win != NULL, "%s", win ? "" : SDL_GetError()))
        return NULL;
    /* Same call VVVVVV makes (Screen.cpp); the hint picks the software driver. */
    *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE);
    if (!th_check("CreateRenderer(ACCEL|TARGET) with RENDER_DRIVER=software", *ren != NULL, "%s",
                  *ren ? "" : SDL_GetError()))
        return win;
    SDL_ShowWindow(win);
    SDL_GetRendererInfo(*ren, &info);
    th_check("renderer is software", SDL_strcmp(info.name, "software") == 0, "%s", info.name);
    {
        int ww, wh, pw, ph, ow = 0, oh = 0;
        SDL_Surface *ws = SDL_GetWindowSurface(win);
        /* SDL_video.c: SDL_WindowTextureData, first member is the backing renderer */
        SDL_Renderer **wtd = (SDL_Renderer **)SDL_GetWindowData(win, "_SDL_WindowTextureData");
        SDL_RendererInfo bi;
        SDL_GetWindowSize(win, &ww, &wh);
        SDL_GetWindowSizeInPixels(win, &pw, &ph);
        SDL_GetRendererOutputSize(*ren, &ow, &oh);
        th_log("window %dx%d, in pixels %dx%d, software output %dx%d, window surface %s",
               ww, wh, pw, ph, ow, oh, ws ? SDL_GetPixelFormatName(ws->format->format) : SDL_GetError());
        if (wtd && *wtd && SDL_GetRendererInfo(*wtd, &bi) == 0)
        {
            int bw = 0, bh = 0;
            SDL_GetRendererOutputSize(*wtd, &bw, &bh);
            th_log("texture framebuffer backed by renderer=%s, output %dx%d", bi.name, bw, bh);
        }
        else
            th_log("no texture framebuffer (native window framebuffer in use)");
#ifdef __ps2sdk__
        th_check("texture framebuffer uses PS2 gsKit", wtd && *wtd && SDL_GetRendererInfo(*wtd, &bi) == 0 &&
                 SDL_strcmp(bi.name, "PS2 gsKit") == 0, NULL);
#endif
    }
    return win;
}

static SDL_GameController *pads[8];

static void open_controllers(void)
{
    int i, opened = 0;
    for (i = 0; i < SDL_NumJoysticks() && i < 8; i++)
        if (SDL_IsGameController(i) && !pads[i] && (pads[i] = SDL_GameControllerOpen(i)) != NULL)
            opened++;
    th_log("controllers: %d joysticks, %d opened as GameController", SDL_NumJoysticks(), opened);
    if (pads[0])
    {
        SDL_GameControllerUpdate();
        th_log("pad0 name=%s A=%d START=%d leftx=%d (polled; button presses need a person or a scripted emulator)",
               SDL_GameControllerName(pads[0]),
               SDL_GameControllerGetButton(pads[0], SDL_CONTROLLER_BUTTON_A),
               SDL_GameControllerGetButton(pads[0], SDL_CONTROLLER_BUTTON_START),
               SDL_GameControllerGetAxis(pads[0], SDL_CONTROLLER_AXIS_LEFTX));
    }
#ifdef __ps2sdk__
    th_check("GameController opened", opened > 0, NULL);
#endif
}

static void run_config(const config *c, int with_correctness, const char *bmp)
{
    SDL_Window *win;
    SDL_Renderer *ren = NULL;
    scene sc;
    char label[96];

    win = open_window(&ren, c);
    if (win && ren)
    {
        th_mem_report("after window+renderer");
        if (with_correctness)
            correctness(ren, win);
        if (scene_create(ren, &sc))
        {
            th_mem_report("after 11 targets+sheets");
            snprintf(label, sizeof(label), "%s, cached fg", c->name);
            benchmark(ren, &sc, label, 180, 0);
            snprintf(label, sizeof(label), "%s, fg redrawn", c->name);
            benchmark(ren, &sc, label, 120, 1);
            if (bmp)
                save_evidence(ren, &sc, bmp);
            verify_gs_output(ren, &sc, c->name, c->gs_tol);
            th_mem_report("config end");
            scene_destroy(&sc);
        }
        SDL_DestroyRenderer(ren);
    }
    if (win)
        SDL_DestroyWindow(win);
    th_log("config %s: window destroyed", c->name);
}

int main(int argc, char **argv)
{
    SDL_AudioSpec want, have;
    SDL_AudioDeviceID dev;
    int rc;
#ifdef PS2_SDL_OPT
#define GAME_BMP "rt_soft_opt_game.bmp"
    /* A:  previous path (CPU letterbox scale into a 640x448 ABGR1555 surface,
     *     default 2 MiB x2 gsKit queues) - kept for A/B in the same binary.
     * A2: CPU scale, 32-bit surface (isolates the 1555 conversion cost).
     * B1: GS scale, 16-bit surface (isolates the scaling cost).
     * B:  GS scaling: 320x240 XBGR8888 window surface, small gsKit queues. */
    static const config configs[] = {
        {"A cpu-scale 1555", NULL, NULL, NULL, NULL, 8, NULL, NULL},
        {"A2 cpu-scale xbgr8888", NULL, "1", NULL, NULL, 2, NULL, NULL},
        {"B1 gs-scale 1555", "320x240", NULL, NULL, NULL, 8, NULL, NULL},
        {"B gs-scale xbgr8888 q64k", "320x240", "1", "65536", "16384", 2, NULL, NULL},
        {"C = B + sw blit fastpaths", "320x240", "1", "65536", "16384", 2, "1", NULL},
        {"D = C + render batching", "320x240", "1", "65536", "16384", 2, "1", "1"},
    };
    th_init("rt_soft_opt", argc, argv);
#else
#define GAME_BMP "rt_soft_stock_game.bmp"
    static const config configs[] = {
        {"stock cpu-scale 1555", NULL, NULL, NULL, NULL, 8, NULL, NULL},
    };
    th_init("rt_soft_stock", argc, argv);
#endif
    th_mem_report("start");
    SDL_SetHint(SDL_HINT_THREAD_STACK_SIZE, "131072");
    rc = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER);
    if (!th_check("SDL_Init", rc == 0, "%s", rc ? SDL_GetError() : ""))
        return th_finish();
    th_mem_report("after SDL_Init");
    /* Audit R8 / P1 W2: main thread below the SDL audio thread. */
    th_set_thread_priority(64);

    SDL_zero(want);
    want.freq = AUDIO_RATE;
    want.format = AUDIO_F32SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = audio_cb;
    dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    th_check("OpenAudioDevice F32", dev != 0, "%s", dev ? "" : SDL_GetError());
    if (dev)
        SDL_PauseAudioDevice(dev, 0);
    open_controllers();

    {
        size_t i;
        for (i = 0; i < sizeof(configs) / sizeof(configs[0]); i++)
            run_config(&configs[i], 1, i == sizeof(configs) / sizeof(configs[0]) - 1 ? GAME_BMP : NULL);
    }

    th_mem_report("end");
    th_summary();
    if (dev)
        SDL_CloseAudioDevice(dev);
    th_log("teardown: audio closed");
    SDL_Quit();
    return th_finish();
}
