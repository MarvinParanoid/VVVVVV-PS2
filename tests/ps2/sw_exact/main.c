/* sw_exact: proves the software-renderer patches are bit-exact. The same
 * deterministic pseudo-random command stream is rendered three ways into a
 * software renderer on a plain surface, and every pixel of every target is
 * compared with the reference (no fast paths, everything ARGB8888):
 *   P1c: patches/sdl2/0002 (SDL_HINT_SW_BLIT_FASTPATHS), ARGB8888 as before;
 *   P3:  0002 + SDL_PS2_P3_FASTPATHS (patches 0003-0005) with the VVVVVV PS2
 *        layout: ABGR8888 source and targets, XBGR8888 screen surface.
 *
 * Covers what the fast paths touch and their neighbours: colour/alpha mod
 * (incl. 255 and 0), BLEND / NONE / ADD / MOD, RenderCopyEx with H/V/HV flips
 * at angle 0, 2x scaled copies, partly off-target destinations, sources with
 * alpha 0, 255 and in-between, nested render targets, blended fills, clears
 * and full-target opaque copies (dead-clear elimination). The screen surface
 * is compared on RGB only (XBGR8888 has no alpha). */
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../common/harness.h"

#define W 320
#define H 240
#define NTARGETS 3
#define NOPS 6000

static Uint32 rng_state;

static Uint32 rnd(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

enum { MODE_REF, MODE_P1C, MODE_P3 };

static SDL_Texture *make_sheet(SDL_Renderer *r, int w, int h, Uint32 format)
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
        case 1: a = 255; break;
        default: a = (v >> 8) & 0xFF; break;
        }
        px[i] = (a << 24) | (rnd() & 0x00FFFFFFu);
    }
    if (format == SDL_PIXELFORMAT_ABGR8888)
        for (i = 0; i < w * h; i++) /* same colours, ABGR layout */
            px[i] = (px[i] & 0xFF00FF00u) | ((px[i] >> 16) & 0xFFu) | ((px[i] & 0xFFu) << 16);
    t = SDL_CreateTexture(r, format, SDL_TEXTUREACCESS_STATIC, w, h);
    SDL_UpdateTexture(t, NULL, px, w * 4);
    free(px);
    return t;
}

/* Renders the stream; returns a malloc'd copy of all target pixels. */
static Uint32 *render_stream(int mode, Uint32 seed)
{
    const Uint32 fmt = mode == MODE_P3 ? SDL_PIXELFORMAT_ABGR8888 : SDL_PIXELFORMAT_ARGB8888;
    SDL_Surface *screen = SDL_CreateRGBSurfaceWithFormat(0, W, H, 32, mode == MODE_P3 ? SDL_PIXELFORMAT_XBGR8888 : SDL_PIXELFORMAT_ARGB8888);
    SDL_Renderer *r;
    SDL_Texture *sheet, *targets[NTARGETS];
    Uint32 *out = (Uint32 *)malloc((size_t)W * H * 4 * (NTARGETS + 1));
    static const SDL_BlendMode blends[] = {SDL_BLENDMODE_BLEND, SDL_BLENDMODE_BLEND, SDL_BLENDMODE_NONE,
                                           SDL_BLENDMODE_ADD, SDL_BLENDMODE_MOD};
    static const SDL_RendererFlip flips[] = {SDL_FLIP_NONE, SDL_FLIP_HORIZONTAL, SDL_FLIP_VERTICAL,
                                             (SDL_RendererFlip)(SDL_FLIP_HORIZONTAL | SDL_FLIP_VERTICAL)};
    int i;

    SDL_SetHint("SDL_SW_BLIT_FASTPATHS", mode != MODE_REF ? "1" : "0");
    SDL_SetHint("SDL_PS2_P3_FASTPATHS", mode == MODE_P3 ? "1" : "0");
    rng_state = seed;
    r = SDL_CreateSoftwareRenderer(screen);
    sheet = make_sheet(r, 256, 256, fmt);
    for (i = 0; i < NTARGETS; i++)
    {
        targets[i] = SDL_CreateTexture(r, fmt, SDL_TEXTUREACCESS_TARGET, W, H);
        SDL_SetRenderTarget(r, targets[i]);
        SDL_SetRenderDrawColor(r, (Uint8)rnd(), (Uint8)rnd(), (Uint8)rnd(), (Uint8)rnd());
        SDL_RenderClear(r);
    }
    for (i = 0; i < NOPS; i++)
    {
        Uint32 v = rnd();
        int t = (int)(v % NTARGETS);
        SDL_Texture *src;
        SDL_Rect s, d;
        Uint8 cm = (Uint8)(rnd() % 3 == 0 ? 255 : rnd());
        SDL_RendererFlip flip = flips[rnd() % 4];

        SDL_SetRenderTarget(r, targets[t]);
        switch (rnd() % 16)
        {
        case 0: /* blended or plain fill */
            d.x = (int)(rnd() % W) - 20; d.y = (int)(rnd() % H) - 20;
            d.w = 1 + (int)(rnd() % 120); d.h = 1 + (int)(rnd() % 90);
            SDL_SetRenderDrawBlendMode(r, blends[rnd() % 5]);
            SDL_SetRenderDrawColor(r, (Uint8)rnd(), (Uint8)rnd(), (Uint8)rnd(), (Uint8)(rnd() % 3 == 0 ? 255 : rnd()));
            SDL_RenderFillRect(r, &d);
            continue;
        case 1: /* clear, often right before a full opaque copy (a dead clear) */
            SDL_SetRenderDrawColor(r, (Uint8)rnd(), (Uint8)rnd(), (Uint8)rnd(), (Uint8)rnd());
            SDL_RenderClear(r);
            if (rnd() % 2)
            {
                SDL_Texture *full = targets[(t + 1) % NTARGETS];
                SDL_SetTextureBlendMode(full, SDL_BLENDMODE_NONE);
                SDL_RenderCopy(r, full, NULL, NULL);
            }
            continue;
        default:
            break;
        }
        if (rnd() % 8 == 0)
        {
            /* nested: another target as source, full or partial */
            src = targets[(t + 1) % NTARGETS];
            s.w = 8 + (int)(rnd() % 120); s.h = 8 + (int)(rnd() % 100);
            s.x = (int)(rnd() % (W - s.w)); s.y = (int)(rnd() % (H - s.h));
        }
        else
        {
            src = sheet;
            s.w = (rnd() % 2) ? 8 : 32; s.h = s.w;
            s.x = (int)(rnd() % (256 - s.w)); s.y = (int)(rnd() % (256 - s.h));
        }
        d.w = s.w; d.h = s.h;
        if (rnd() % 6 == 0)
        {
            /* scaled (SDL_COPY_NEAREST), like VVVVVV's 2x text */
            d.w = s.w * 2;
            d.h = s.h * (1 + (int)(rnd() % 2));
        }
        d.x = (int)(rnd() % (W + s.w)) - s.w / 2;  /* sometimes clipped */
        d.y = (int)(rnd() % (H + s.h)) - s.h / 2;
        SDL_SetTextureBlendMode(src, blends[rnd() % 5]);
        SDL_SetTextureColorMod(src, cm, (Uint8)(cm ^ rnd()), (Uint8)(rnd() % 4 == 0 ? 255 : rnd()));
        SDL_SetTextureAlphaMod(src, (Uint8)(rnd() % 3 == 0 ? 255 : (rnd() % 5 == 0 ? 0 : rnd())));
        if (flip == SDL_FLIP_NONE && rnd() % 2)
            SDL_RenderCopy(r, src, &s, &d);
        else
            SDL_RenderCopyEx(r, src, &s, &d, 0.0, NULL, flip);
        SDL_SetTextureColorMod(src, 255, 255, 255);
        SDL_SetTextureAlphaMod(src, 255);
    }
    for (i = 0; i < NTARGETS; i++)
    {
        SDL_SetRenderTarget(r, targets[i]);
        SDL_RenderReadPixels(r, NULL, SDL_PIXELFORMAT_ARGB8888, out + (size_t)i * W * H, W * 4);
    }
    SDL_SetRenderTarget(r, NULL);
    SDL_SetTextureBlendMode(targets[0], SDL_BLENDMODE_NONE);
    SDL_RenderCopy(r, targets[0], NULL, NULL);
    SDL_RenderReadPixels(r, NULL, SDL_PIXELFORMAT_ARGB8888, out + (size_t)NTARGETS * W * H, W * 4);
    for (i = 0; i < W * H; i++) /* the screen is compared on RGB only */
        out[(size_t)NTARGETS * W * H + i] &= 0x00FFFFFFu;

    for (i = 0; i < NTARGETS; i++)
        SDL_DestroyTexture(targets[i]);
    SDL_DestroyTexture(sheet);
    SDL_DestroyRenderer(r);
    SDL_FreeSurface(screen);
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

int main(int argc, char **argv)
{
    Uint32 seeds[] = {0x1234567u, 0xC0FFEEu, 0xBADF00Du};
    size_t k;
    th_init(
#ifdef PS2_SDL_OPT
        "sw_exact_opt",
#else
        "sw_exact_stock",
#endif
        argc, argv);
    for (k = 0; k < sizeof(seeds) / sizeof(seeds[0]); k++)
    {
        const size_t n = (size_t)W * H * (NTARGETS + 1);
        uint64_t t0 = th_now_us(), t1, t2, t3;
        Uint32 *ref, *p1c, *p3;
        size_t d1, d3, f1, f3;
        char name[80];
        ref = render_stream(MODE_REF, seeds[k]);
        t1 = th_now_us();
        p1c = render_stream(MODE_P1C, seeds[k]);
        t2 = th_now_us();
        p3 = render_stream(MODE_P3, seeds[k]);
        t3 = th_now_us();
        d1 = compare(ref, p1c, n, &f1);
        d3 = compare(ref, p3, n, &f3);
        th_log("seed %08lx: %d ops, reference %lu us, P1c fast paths %lu us, P3 (ABGR) %lu us", (unsigned long)seeds[k],
               NOPS, (unsigned long)(t1 - t0), (unsigned long)(t2 - t1), (unsigned long)(t3 - t2));
        if (d1)
            th_log("  P1c first diff at %lu: ref %08lx got %08lx", (unsigned long)f1, (unsigned long)ref[f1], (unsigned long)p1c[f1]);
        if (d3)
            th_log("  P3 first diff at %lu: ref %08lx got %08lx", (unsigned long)f3, (unsigned long)ref[f3], (unsigned long)p3[f3]);
#ifdef PS2_SDL_OPT
        snprintf(name, sizeof(name), "P1c bit-exact, seed %08lx", (unsigned long)seeds[k]);
        th_check(name, d1 == 0, "%lu of %lu pixels differ", (unsigned long)d1, (unsigned long)n);
        snprintf(name, sizeof(name), "P3 ABGR bit-exact, seed %08lx", (unsigned long)seeds[k]);
        th_check(name, d3 == 0, "%lu of %lu pixels differ", (unsigned long)d3, (unsigned long)n);
#else
        snprintf(name, sizeof(name), "seed %08lx", (unsigned long)seeds[k]);
        th_log("%s: stock SDL has no fast paths (P1c %lu, P3 %lu differ)", name, (unsigned long)d1, (unsigned long)d3);
#endif
        free(ref);
        free(p1c);
        free(p3);
    }
    th_summary();
    return th_finish();
}
