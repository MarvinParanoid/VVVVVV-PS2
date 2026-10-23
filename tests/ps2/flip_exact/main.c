/* flip_exact (P3.1): proves the flipped-copy fast path (patches/sdl2/0008,
 * hint SDL_SW_FLIP_FASTPATH) is bit-exact, and times it.
 *
 * The same deterministic stream of flipped RenderCopyEx calls (angle 0) is
 * rendered by a software renderer on a plain surface in four modes:
 *   REF       no fast paths: SDL's rotozoom path, everything ARGB8888
 *   OLD       0002 + P3 fast paths, ABGR8888 (RC2's configuration)
 *   NEW       OLD + SDL_SW_FLIP_FASTPATH
 *   NEW_ARGB  0002 + SDL_SW_FLIP_FASTPATH, ARGB8888 (no P3)
 * and every pixel of every target and of the screen is compared with REF.
 * Covers V/H/HV flips; unscaled, 2x-6x and unequal x/y scales; BLEND, NONE,
 * ADD, MOD; colour and alpha modulation incl. 0 and 255; sources with alpha 0,
 * 255 and in between; clipping on every edge; another target as the source;
 * LINEAR textures (must fall back). Then times VVVVVV-like streams (flipped
 * 1x and 2x bordered glyphs, a flipped full-frame copy) in OLD and NEW.
 *
 * P3.2 additions (lifecycle): two renderers alive at once with mixed flipped
 * and plain copies, scratch growth and shrink, a renderer destroyed and
 * recreated mid-stream, a flipped copy too large to allocate (must fail like
 * the previous code and leave everything else intact), no scratch
 * allocations in steady state, ARGB/ABGR/RGBA/BGRA/XRGB sources, a screen
 * surface with a non-trivial pitch and an odd-width texture; fast path on vs
 * off. SDL_sw_flip_stats (patch 0008) is read as an extern, so this test does
 * not link against an SDL without the patch, and each mode reports which
 * path actually ran. */
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../common/harness.h"

/* patches/sdl2/0008 diagnostics (not a public API) */
typedef struct
{
    Uint32 fast, fallback, scratch_allocs, scratch_alloc_failures;
} SDL_SWFlipStats;
extern SDL_SWFlipStats SDL_sw_flip_stats;

#define W 320
#define H 240
#define NTARGETS 3
#define NOPS 4000

static Uint32 rng_state;

static Uint32 rnd(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

enum { MODE_REF, MODE_OLD, MODE_NEW, MODE_NEW_ARGB, N_MODES };
static const char *mode_name[N_MODES] = {"REF", "OLD", "NEW", "NEW_ARGB"};

static void set_mode(int mode)
{
    SDL_SetHint("SDL_SW_BLIT_FASTPATHS", mode != MODE_REF ? "1" : "0");
    SDL_SetHint("SDL_PS2_P3_FASTPATHS", (mode == MODE_OLD || mode == MODE_NEW) ? "1" : "0");
    SDL_SetHint("SDL_SW_FLIP_FASTPATH", (mode == MODE_NEW || mode == MODE_NEW_ARGB) ? "1" : "0");
}

static Uint32 mode_format(int mode)
{
    return (mode == MODE_OLD || mode == MODE_NEW) ? SDL_PIXELFORMAT_ABGR8888 : SDL_PIXELFORMAT_ARGB8888;
}

static SDL_Texture *make_sheet(SDL_Renderer *r, int w, int h, Uint32 format, int white)
{
    Uint32 *px = (Uint32 *)malloc((size_t)w * h * 4);
    SDL_Texture *t;
    int i;
    for (i = 0; i < w * h; i++)
    {
        Uint32 a, v = rnd();
        switch (v % 4)
        {
        case 0: a = 0; break;
        case 1: case 2: a = 255; break;
        default: a = (v >> 8) & 0xFF; break;
        }
        px[i] = (a << 24) | (white ? 0x00FFFFFFu : (rnd() & 0x00FFFFFFu));
    }
    if (format == SDL_PIXELFORMAT_ABGR8888)
        for (i = 0; i < w * h; i++)
            px[i] = (px[i] & 0xFF00FF00u) | ((px[i] >> 16) & 0xFFu) | ((px[i] & 0xFFu) << 16);
    t = SDL_CreateTexture(r, format, SDL_TEXTUREACCESS_STATIC, w, h);
    SDL_UpdateTexture(t, NULL, px, w * 4);
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    free(px);
    return t;
}

typedef struct
{
    SDL_Surface *screen;
    SDL_Renderer *r;
    SDL_Texture *sheet, *font, *targets[NTARGETS];
} ctx;

static void ctx_open(ctx *c, int mode, Uint32 seed)
{
    const Uint32 fmt = mode_format(mode);
    int i;
    set_mode(mode);
    rng_state = seed;
    c->screen = SDL_CreateRGBSurfaceWithFormat(0, W, H, 32,
                                               fmt == SDL_PIXELFORMAT_ABGR8888 ? SDL_PIXELFORMAT_XBGR8888 : SDL_PIXELFORMAT_ARGB8888);
    c->r = SDL_CreateSoftwareRenderer(c->screen);
    c->sheet = make_sheet(c->r, 256, 256, fmt, 0);
    c->font = make_sheet(c->r, 128, 128, fmt, 1);
    for (i = 0; i < NTARGETS; i++)
    {
        c->targets[i] = SDL_CreateTexture(c->r, fmt, SDL_TEXTUREACCESS_TARGET, W, H);
        SDL_SetRenderTarget(c->r, c->targets[i]);
        SDL_SetRenderDrawColor(c->r, (Uint8)rnd(), (Uint8)rnd(), (Uint8)rnd(), (Uint8)rnd());
        SDL_RenderClear(c->r);
    }
}

static void ctx_close(ctx *c)
{
    int i;
    for (i = 0; i < NTARGETS; i++)
        SDL_DestroyTexture(c->targets[i]);
    SDL_DestroyTexture(c->sheet);
    SDL_DestroyTexture(c->font);
    SDL_DestroyRenderer(c->r);
    SDL_FreeSurface(c->screen);
}

static Uint32 *read_all(ctx *c)
{
    Uint32 *out = (Uint32 *)malloc((size_t)W * H * 4 * (NTARGETS + 1));
    int i;
    for (i = 0; i < NTARGETS; i++)
    {
        SDL_SetRenderTarget(c->r, c->targets[i]);
        SDL_RenderReadPixels(c->r, NULL, SDL_PIXELFORMAT_ARGB8888, out + (size_t)i * W * H, W * 4);
    }
    SDL_SetRenderTarget(c->r, NULL);
    SDL_RenderReadPixels(c->r, NULL, SDL_PIXELFORMAT_ARGB8888, out + (size_t)NTARGETS * W * H, W * 4);
    for (i = 0; i < W * H; i++) /* the screen is compared on RGB only */
        out[(size_t)NTARGETS * W * H + i] &= 0x00FFFFFFu;
    return out;
}

/* the correctness stream: flipped copies only */
static Uint32 *render_stream(int mode, Uint32 seed)
{
    static const SDL_BlendMode blends[] = {SDL_BLENDMODE_BLEND, SDL_BLENDMODE_BLEND, SDL_BLENDMODE_NONE,
                                           SDL_BLENDMODE_ADD, SDL_BLENDMODE_MOD};
    static const SDL_RendererFlip flips[] = {SDL_FLIP_VERTICAL, SDL_FLIP_VERTICAL, SDL_FLIP_HORIZONTAL,
                                             (SDL_RendererFlip)(SDL_FLIP_HORIZONTAL | SDL_FLIP_VERTICAL)};
    static const int scales[][2] = {{1, 1}, {1, 1}, {2, 2}, {2, 2}, {3, 3}, {4, 4}, {6, 6}, {2, 1}, {1, 2}, {3, 2}};
    ctx c;
    Uint32 *out;
    int i;
    ctx_open(&c, mode, seed);
    for (i = 0; i < NOPS; i++)
    {
        const int t = (int)(rnd() % NTARGETS);
        SDL_Texture *src;
        SDL_Rect s, d;
        const int *sc = scales[rnd() % (sizeof(scales) / sizeof(scales[0]))];
        const Uint8 cm = (Uint8)(rnd() % 3 == 0 ? 255 : rnd());
        const SDL_RendererFlip flip = flips[rnd() % 4];
        const int linear = rnd() % 16 == 0;
        SDL_SetRenderTarget(c.r, c.targets[t]);
        if (rnd() % 10 == 0)
        {
            /* another target as the source (gameTexture -> window in flip mode) */
            src = c.targets[(t + 1) % NTARGETS];
            if (rnd() % 3 == 0)
            {
                s.x = 0, s.y = 0, s.w = W, s.h = H;
            }
            else
            {
                s.w = 8 + (int)(rnd() % 100), s.h = 8 + (int)(rnd() % 80);
                s.x = (int)(rnd() % (W - s.w)), s.y = (int)(rnd() % (H - s.h));
            }
        }
        else
        {
            static const int sizes[] = {8, 8, 8, 12, 32, 5};
            src = rnd() % 2 ? c.font : c.sheet;
            s.w = sizes[rnd() % 6];
            s.h = s.w == 5 ? 7 : s.w;
            s.x = (int)(rnd() % (128 - s.w)), s.y = (int)(rnd() % (128 - s.h));
        }
        d.w = s.w * sc[0], d.h = s.h * sc[1];
        if (d.w > 2 * W || d.h > 2 * H)
            d.w = s.w, d.h = s.h;
        d.x = (int)(rnd() % (W + d.w)) - d.w / 2; /* often clipped */
        d.y = (int)(rnd() % (H + d.h)) - d.h / 2;
        SDL_SetTextureBlendMode(src, blends[rnd() % 5]);
        SDL_SetTextureColorMod(src, cm, (Uint8)(cm ^ rnd()), (Uint8)(rnd() % 4 == 0 ? 255 : rnd()));
        SDL_SetTextureAlphaMod(src, (Uint8)(rnd() % 3 == 0 ? 255 : (rnd() % 5 == 0 ? 0 : rnd())));
        SDL_SetTextureScaleMode(src, linear ? SDL_ScaleModeLinear : SDL_ScaleModeNearest);
        SDL_RenderCopyEx(c.r, src, &s, &d, 0.0, NULL, flip);
        SDL_SetTextureColorMod(src, 255, 255, 255);
        SDL_SetTextureAlphaMod(src, 255);
        SDL_SetTextureScaleMode(src, SDL_ScaleModeNearest);
    }
    /* final flipped full-frame copy to the screen, as Graphics::render does */
    SDL_SetRenderTarget(c.r, NULL);
    SDL_SetTextureBlendMode(c.targets[0], SDL_BLENDMODE_NONE);
    SDL_RenderCopyEx(c.r, c.targets[0], NULL, NULL, 0.0, NULL, SDL_FLIP_VERTICAL);
    out = read_all(&c);
    ctx_close(&c);
    return out;
}

static size_t compare(const Uint32 *a, const Uint32 *b, size_t n, size_t *first)
{
    size_t i, diff = 0;
    *first = (size_t)-1;
    for (i = 0; i < n; i++)
        if (a[i] != b[i])
        {
            if (*first == (size_t)-1)
                *first = i;
            diff++;
        }
    return diff;
}

/* VVVVVV-like timing streams: font::print with PR_BOR (4 black + 1 coloured
 * copies per glyph) flipped, at 1x and 2x; and the flipped final frame copy */
static double time_stream(int mode, int kind, int reps)
{
    static const int off[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
    ctx c;
    uint64_t t0;
    int rep, g, k;
    ctx_open(&c, mode, 777);
    SDL_SetTextureBlendMode(c.targets[1], SDL_BLENDMODE_NONE);
    t0 = th_now_us();
    for (rep = 0; rep < reps; rep++)
    {
        if (kind == 2)
        {
            SDL_SetRenderTarget(c.r, NULL);
            SDL_RenderCopyEx(c.r, c.targets[1], NULL, NULL, 0.0, NULL, SDL_FLIP_VERTICAL);
            SDL_RenderFlush(c.r);
            continue;
        }
        SDL_SetRenderTarget(c.r, c.targets[0]);
        for (g = 0; g < 40; g++)
        {
            const int scale = kind == 1 ? 2 : 1;
            SDL_Rect s = {(g % 16) * 8, (g / 16) * 8, 8, 8};
            for (k = 0; k < 5; k++)
            {
                SDL_Rect d = {8 + (g % 18) * 8 * scale, 20 + (g / 18) * 10 * scale, 8 * scale, 8 * scale};
                Uint8 v = k < 4 ? 0 : 220;
                if (k < 4)
                    d.x += off[k][0], d.y += off[k][1];
                SDL_SetTextureColorMod(c.font, v, v, (Uint8)(k < 4 ? 0 : 255));
                SDL_RenderCopyEx(c.r, c.font, &s, &d, 0.0, NULL, SDL_FLIP_VERTICAL);
                SDL_SetTextureColorMod(c.font, 255, 255, 255);
            }
        }
        SDL_RenderFlush(c.r);
    }
    ctx_close(&c);
    return (double)(th_now_us() - t0) / reps;
}


/* ---- P3.2 lifecycle test ---- */
#define LW 320
#define LH 240
#define LPITCH (LW * 4 + 52) /* screen pitch that is not width * 4 */

typedef struct
{
    Uint8 *screen_px;
    SDL_Surface *screen;
    SDL_Renderer *r;
    SDL_Texture *sheet, *odd, *target;
} lctx;

static void lctx_open(lctx *c, Uint32 fmt, Uint32 seed)
{
    rng_state = seed;
    c->screen_px = (Uint8 *)calloc(1, (size_t)LPITCH * LH);
    c->screen = SDL_CreateRGBSurfaceWithFormatFrom(c->screen_px, LW, LH, 32, LPITCH, SDL_PIXELFORMAT_ARGB8888);
    c->r = SDL_CreateSoftwareRenderer(c->screen);
    c->sheet = make_sheet(c->r, 128, 128, fmt, 0);
    c->odd = make_sheet(c->r, 37, 29, fmt, 0); /* pitch 148 */
    c->target = SDL_CreateTexture(c->r, fmt, SDL_TEXTUREACCESS_TARGET, LW, LH);
    SDL_SetRenderTarget(c->r, c->target);
    SDL_SetRenderDrawColor(c->r, 30, 60, 90, 200);
    SDL_RenderClear(c->r);
}

/* appends target + screen pixels (ARGB8888) to out, then frees c */
static Uint32 *lctx_close(lctx *c, Uint32 *out)
{
    int y;
    SDL_SetRenderTarget(c->r, c->target);
    SDL_RenderReadPixels(c->r, NULL, SDL_PIXELFORMAT_ARGB8888, out, LW * 4);
    out += LW * LH;
    SDL_SetRenderTarget(c->r, NULL);
    SDL_SetTextureBlendMode(c->target, SDL_BLENDMODE_NONE);
    SDL_RenderCopyEx(c->r, c->target, NULL, NULL, 0.0, NULL, SDL_FLIP_VERTICAL);
    SDL_RenderFlush(c->r);
    for (y = 0; y < LH; y++)
        memcpy(out + y * LW, c->screen_px + (size_t)y * LPITCH, LW * 4);
    out += LW * LH;
    SDL_DestroyTexture(c->sheet);
    SDL_DestroyTexture(c->odd);
    SDL_DestroyTexture(c->target);
    SDL_DestroyRenderer(c->r);
    SDL_FreeSurface(c->screen);
    free(c->screen_px);
    return out;
}

/* one mixed op: flipped or plain copy of a size class, onto the target */
static void lop(lctx *c, int size_class)
{
    static const SDL_RendererFlip flips[] = {SDL_FLIP_NONE, SDL_FLIP_VERTICAL, SDL_FLIP_HORIZONTAL,
                                             (SDL_RendererFlip)(SDL_FLIP_HORIZONTAL | SDL_FLIP_VERTICAL)};
    static const SDL_BlendMode blends[] = {SDL_BLENDMODE_BLEND, SDL_BLENDMODE_NONE, SDL_BLENDMODE_ADD, SDL_BLENDMODE_MOD};
    SDL_Texture *src = rnd() % 4 == 0 ? c->odd : c->sheet;
    int sw = src == c->odd ? 37 : 128, sh = src == c->odd ? 29 : 128;
    SDL_Rect s, d;
    int scale = size_class == 0 ? 1 : size_class == 1 ? 2 : 4;
    s.w = size_class == 2 ? sw : 1 + (int)(rnd() % (unsigned)(sw < 16 ? sw : 16));
    s.h = size_class == 2 ? sh : 1 + (int)(rnd() % (unsigned)(sh < 16 ? sh : 16));
    s.x = (int)(rnd() % (unsigned)(sw - s.w + 1)), s.y = (int)(rnd() % (unsigned)(sh - s.h + 1));
    d.w = s.w * scale, d.h = s.h * scale;
    d.x = (int)(rnd() % (LW + d.w)) - d.w / 2, d.y = (int)(rnd() % (LH + d.h)) - d.h / 2;
    SDL_SetRenderTarget(c->r, c->target);
    SDL_SetTextureBlendMode(src, blends[rnd() % 4]);
    SDL_SetTextureColorMod(src, (Uint8)rnd(), (Uint8)rnd(), (Uint8)(rnd() % 2 ? 255 : rnd()));
    SDL_SetTextureAlphaMod(src, (Uint8)(rnd() % 3 ? 255 : rnd()));
    SDL_RenderCopyEx(c->r, src, &s, &d, 0.0, NULL, flips[rnd() % 4]);
}

static int lifecycle_run(int fast, Uint32 fmt, Uint32 seed, Uint32 *out, Uint32 *steady_allocs, Uint32 *fail_delta,
                         int *huge_rc)
{
    lctx a, b, c;
    SDL_SWFlipStats st;
    /* pitch above INT32: SDL refuses the surface without touching memory, on the host too */
    SDL_Rect s = {0, 0, 16, 16}, d = {-1000, 10, 600000000, 1};
    int i;
    SDL_SetHint("SDL_SW_BLIT_FASTPATHS", "1");
    SDL_SetHint("SDL_PS2_P3_FASTPATHS", "1");
    SDL_SetHint("SDL_SW_FLIP_FASTPATH", fast ? "1" : "0");
    lctx_open(&a, fmt, seed);
    lctx_open(&b, fmt, seed ^ 0x55u);
    /* growth: small, 2x, whole sheet 4x (512x512 scratch), back to small; interleaved renderers */
    for (i = 0; i < 400; i++)
    {
        lop(&a, i < 100 ? 0 : i < 200 ? 1 : i < 220 ? 2 : 0);
        lop(&b, i % 3 == 0 ? 1 : 0);
    }
    /* a flipped copy too large to allocate: the fast path must fail over to the previous code */
    st = SDL_sw_flip_stats;
    SDL_SetRenderTarget(a.r, a.target);
    SDL_SetTextureBlendMode(a.sheet, SDL_BLENDMODE_BLEND);
    *huge_rc = SDL_RenderCopyEx(a.r, a.sheet, &s, &d, 0.0, NULL, SDL_FLIP_VERTICAL);
    SDL_RenderFlush(a.r);
    *fail_delta = SDL_sw_flip_stats.scratch_alloc_failures - st.scratch_alloc_failures;
    /* renderer a goes away mid-stream; b continues; c is new */
    out = lctx_close(&a, out);
    lctx_open(&c, fmt, seed ^ 0xAAu);
    for (i = 0; i < 200; i++)
    {
        lop(&b, i % 2);
        lop(&c, i % 5 == 0 ? 1 : 0);
    }
    /* steady state: after a warm-up at the largest size this mix uses (16x16
     * at 2x), the same mix again must not (re)allocate scratches */
    {
        SDL_Rect ws = {0, 0, 16, 16}, wd = {0, 0, 32, 32};
        SDL_SetRenderTarget(b.r, b.target);
        SDL_RenderCopyEx(b.r, b.sheet, &ws, &wd, 0.0, NULL, SDL_FLIP_VERTICAL);
        SDL_SetRenderTarget(c.r, c.target);
        SDL_RenderCopyEx(c.r, c.sheet, &ws, &wd, 0.0, NULL, SDL_FLIP_VERTICAL);
    }
    SDL_RenderFlush(b.r);
    SDL_RenderFlush(c.r);
    st = SDL_sw_flip_stats;
    for (i = 0; i < 300; i++)
    {
        lop(&b, i % 2);
        lop(&c, i % 5 == 0 ? 1 : 0);
    }
    SDL_RenderFlush(b.r);
    SDL_RenderFlush(c.r);
    *steady_allocs = SDL_sw_flip_stats.scratch_allocs - st.scratch_allocs;
    out = lctx_close(&b, out);
    out = lctx_close(&c, out);
    return 0;
}

static void lifecycle_tests(void)
{
    static const Uint32 fmts[] = {SDL_PIXELFORMAT_ARGB8888, SDL_PIXELFORMAT_ABGR8888, SDL_PIXELFORMAT_RGBA8888,
                                  SDL_PIXELFORMAT_BGRA8888, SDL_PIXELFORMAT_XRGB8888};
    const size_t n = (size_t)LW * LH * 6; /* 3 renderers x (target + screen) */
    size_t f;
    for (f = 0; f < sizeof(fmts) / sizeof(fmts[0]); f++)
    {
        Uint32 *on = (Uint32 *)malloc(n * 4), *off = (Uint32 *)malloc(n * 4);
        Uint32 steady_on = 0, steady_off = 0, fail_on = 0, fail_off = 0;
        int rc_on = 0, rc_off = 0;
        SDL_SWFlipStats st0 = SDL_sw_flip_stats;
        size_t first, d;
        char name[96];
        Uint32 fast_on;
        lifecycle_run(1, fmts[f], 0x5EED0000u + (Uint32)f, on, &steady_on, &fail_on, &rc_on);
        fast_on = SDL_sw_flip_stats.fast - st0.fast;
        lifecycle_run(0, fmts[f], 0x5EED0000u + (Uint32)f, off, &steady_off, &fail_off, &rc_off);
        d = compare(off, on, n, &first);
        snprintf(name, sizeof(name), "lifecycle %s: fast path = previous code", SDL_GetPixelFormatName(fmts[f]));
        th_check(name, d == 0, "%lu of %lu pixels differ", (unsigned long)d, (unsigned long)n);
        if (d)
            th_log("  first diff at %lu: off %08lx on %08lx", (unsigned long)first, (unsigned long)off[first],
                   (unsigned long)on[first]);
        snprintf(name, sizeof(name), "lifecycle %s: no scratch allocs in steady state", SDL_GetPixelFormatName(fmts[f]));
        th_check(name, steady_on == 0, "%lu allocations", (unsigned long)steady_on);
        snprintf(name, sizeof(name), "lifecycle %s: oversized flip falls back like before", SDL_GetPixelFormatName(fmts[f]));
        th_check(name, rc_on == rc_off && fail_on == 1 && fail_off == 0, "rc fast %d / previous %d, scratch alloc failures %lu / %lu",
                 rc_on, rc_off, (unsigned long)fail_on, (unsigned long)fail_off);
        th_log("lifecycle %s: fast-path copies %lu, scratch alloc failures %lu (the oversized copy; batched, so "
               "SDL_RenderCopyEx returns 0 on both paths and the failure only shows in this counter)",
               SDL_GetPixelFormatName(fmts[f]), (unsigned long)fast_on, (unsigned long)fail_on);
        free(on);
        free(off);
    }
}

int main(int argc, char **argv)
{
    static const Uint32 seeds[] = {0x1234567u, 0xC0FFEEu, 0xBADF00Du};
    static const char *kinds[] = {"40 glyphs x5 (PR_BOR), 1x, flipped", "40 glyphs x5 (PR_BOR), 2x, flipped",
                                  "full 320x240 frame, flipped (gameTexture -> window)"};
    size_t k;
    int m;
    th_init("flip_exact", argc, argv);
    for (k = 0; k < sizeof(seeds) / sizeof(seeds[0]); k++)
    {
        const size_t n = (size_t)W * H * (NTARGETS + 1);
        Uint32 *out[N_MODES];
        for (m = 0; m < N_MODES; m++)
        {
            uint64_t t0 = th_now_us();
            const SDL_SWFlipStats st0 = SDL_sw_flip_stats;
            Uint32 fast, fallback;
            char name[80];
            out[m] = render_stream(m, seeds[k]);
            fast = SDL_sw_flip_stats.fast - st0.fast;
            fallback = SDL_sw_flip_stats.fallback - st0.fallback;
            th_log("seed %08lx %-8s %d flipped copies: %lu us (fast path %lu, previous code %lu)", (unsigned long)seeds[k],
                   mode_name[m], NOPS, (unsigned long)(th_now_us() - t0), (unsigned long)fast, (unsigned long)fallback);
            snprintf(name, sizeof(name), "%s ran the intended path, seed %08lx", mode_name[m], (unsigned long)seeds[k]);
            if (m == MODE_NEW || m == MODE_NEW_ARGB)
                th_check(name, fast > (Uint32)NOPS / 2, "fast %lu fallback %lu", (unsigned long)fast, (unsigned long)fallback);
            else
                th_check(name, fast == 0, "fast %lu fallback %lu", (unsigned long)fast, (unsigned long)fallback);
        }
        for (m = MODE_OLD; m < N_MODES; m++)
        {
            size_t first, d = compare(out[MODE_REF], out[m], n, &first);
            char name[80];
            snprintf(name, sizeof(name), "%s bit-exact vs REF, seed %08lx", mode_name[m], (unsigned long)seeds[k]);
            th_check(name, d == 0, "%lu of %lu pixels differ%s", (unsigned long)d, (unsigned long)n, d ? "" : "");
            if (d)
                th_log("  first diff at %lu: ref %08lx got %08lx", (unsigned long)first, (unsigned long)out[MODE_REF][first],
                       (unsigned long)out[m][first]);
        }
        for (m = 0; m < N_MODES; m++)
            free(out[m]);
    }
    for (k = 0; k < 3; k++)
    {
        const int reps = k == 2 ? 30 : 20;
        const double t_old = time_stream(MODE_OLD, (int)k, reps), t_new = time_stream(MODE_NEW, (int)k, reps);
        th_log("TIME %-52s OLD %8.0f us  NEW %8.0f us  (%.1fx)", kinds[k], t_old, t_new, t_old / t_new);
    }
    lifecycle_tests();
    th_log("flip stats total: fast %lu, previous code %lu, scratch allocs %lu, failed %lu",
           (unsigned long)SDL_sw_flip_stats.fast, (unsigned long)SDL_sw_flip_stats.fallback,
           (unsigned long)SDL_sw_flip_stats.scratch_allocs, (unsigned long)SDL_sw_flip_stats.scratch_alloc_failures);
    th_mem_report("end");
    th_summary();
    return th_finish();
}
