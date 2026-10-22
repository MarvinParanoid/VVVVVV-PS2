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
 * 1x and 2x bordered glyphs, a flipped full-frame copy) in OLD and NEW. */
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../common/harness.h"

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
            out[m] = render_stream(m, seeds[k]);
            th_log("seed %08lx %-8s %d flipped copies: %lu us", (unsigned long)seeds[k], mode_name[m], NOPS,
                   (unsigned long)(th_now_us() - t0));
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
    th_mem_report("end");
    th_summary();
    return th_finish();
}
