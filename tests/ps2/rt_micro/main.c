/* rt_micro: per-operation cost of SDL's software renderer on the EE.
 *
 * Runs on the software renderer the way VVVVVV will get it (SDL_CreateRenderer
 * with SDL_HINT_RENDER_DRIVER=software), drawing into render targets only, so
 * no GS / texture framebuffer cost is included. Each case issues N
 * identical operations into a render target, flushes, and reports
 * microseconds per call and nanoseconds per destination pixel.
 *
 * Sheets are procedural stand-ins with VVVVVV's grid sizes and plausible
 * alpha coverage (tiles opaque, sprites ~50% opaque, glyphs ~30% opaque);
 * coverage is logged because blend cost can depend on it. */
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>

#include "../common/harness.h"

#define W 320
#define H 240

static SDL_Renderer *r;
static SDL_Texture *tiles, *sprites, *font, *fg, *bg, *t_a, *t_b, *opaque_full;

static Uint32 hash32(Uint32 x)
{
    x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
    return x;
}

/* coverage: 0 = opaque, 1 = sprite-like blob, 2 = glyph-like bits, 3 = fg layer (half empty) */
static SDL_Texture *make_sheet(int w, int h, int cell, int coverage, const char *name)
{
    Uint32 *px = (Uint32 *)malloc((size_t)w * h * 4);
    unsigned long opaque = 0;
    int x, y;
    SDL_Texture *t;
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++)
        {
            Uint32 c = hash32((Uint32)((x / cell) * 977 + (y / cell))) | 0xFF000000u;
            int cx = x % cell - cell / 2, cy = y % cell - cell / 2, on = 1;
            if (coverage == 1)
                on = cx * cx + cy * cy < (cell * cell) / 4; /* disc: ~78% of a cell, edges soft-free */
            else if (coverage == 2)
                on = (hash32((Uint32)(x * 131 + y * 7919)) % 100) < 30;
            else if (coverage == 3)
                on = ((x / cell + y / cell) & 1) == 0;
            px[y * w + x] = on ? c : 0x00000000u;
            opaque += on;
        }
    t = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, w, h);
    SDL_UpdateTexture(t, NULL, px, w * 4);
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    th_log("sheet %-8s %dx%d cell %d: %.0f%% opaque", name, w, h, cell, opaque * 100.0 / ((double)w * h));
    free(px);
    return t;
}

typedef void (*op_fn)(int i);

static void bench(const char *name, int n, int pixels_per_call, op_fn fn, SDL_Texture *target)
{
    uint64_t t0, dt;
    int i;
    SDL_SetRenderTarget(r, target);
    SDL_RenderFlush(r);
    t0 = th_now_us();
    for (i = 0; i < n; i++)
        fn(i);
    SDL_RenderFlush(r);
    dt = th_now_us() - t0;
    th_log("OP %-44s n=%5d  %8.2f us/call  %7.1f ns/px", name, n, (double)dt / n,
           pixels_per_call ? (double)dt * 1000.0 / ((double)n * pixels_per_call) : 0.0);
}

static void copy_cell(SDL_Texture *t, int cell, int cols, int idx, int x, int y)
{
    SDL_Rect s, d;
    s.x = (idx % cols) * cell; s.y = (idx / cols) * cell; s.w = cell; s.h = cell;
    d.x = x; d.y = y; d.w = cell; d.h = cell;
    SDL_RenderCopy(r, t, &s, &d);
}

static void copy_cell_ex(SDL_Texture *t, int cell, int cols, int idx, int x, int y, SDL_RendererFlip flip)
{
    SDL_Rect s, d;
    s.x = (idx % cols) * cell; s.y = (idx / cols) * cell; s.w = cell; s.h = cell;
    d.x = x; d.y = y; d.w = cell; d.h = cell;
    SDL_RenderCopyEx(r, t, &s, &d, 0.0, NULL, flip);
}

/* --- cases --- */
static void op_tile(int i) { copy_cell(tiles, 8, 40, i % 1200, (i % 40) * 8, (i / 40 % 30) * 8); }
static void op_tile_mod_const(int i) { copy_cell(tiles, 8, 40, i % 1200, (i % 40) * 8, (i / 40 % 30) * 8); }
static void op_tile_mod_toggle(int i)
{
    /* VVVVVV draw_grid_tile: set mod, draw, reset to 255 */
    SDL_SetTextureColorMod(tiles, 200, 100, 50);
    copy_cell(tiles, 8, 40, i % 1200, (i % 40) * 8, (i / 40 % 30) * 8);
    SDL_SetTextureColorMod(tiles, 255, 255, 255);
}
static void op_glyph_mod_toggle(int i)
{
    SDL_SetTextureColorMod(font, 196, 196, 255);
    copy_cell(font, 8, 16, 32 + i % 96, (i % 40) * 8, (i / 40 % 30) * 8);
    SDL_SetTextureColorMod(font, 255, 255, 255);
}
static void op_glyph_nomod(int i) { copy_cell(font, 8, 16, 32 + i % 96, (i % 40) * 8, (i / 40 % 30) * 8); }
static void op_sprite(int i) { copy_cell(sprites, 32, 12, i % 48, (i * 37) % (W - 32), (i * 53) % (H - 32)); }
static void op_sprite_mod_toggle(int i)
{
    SDL_SetTextureColorMod(sprites, 180, 220, 255);
    copy_cell(sprites, 32, 12, i % 48, (i * 37) % (W - 32), (i * 53) % (H - 32));
    SDL_SetTextureColorMod(sprites, 255, 255, 255);
}
static void op_sprite_mod_alpha(int i)
{
    SDL_SetTextureColorMod(sprites, 180, 220, 255);
    SDL_SetTextureAlphaMod(sprites, 128);
    copy_cell(sprites, 32, 12, i % 48, (i * 37) % (W - 32), (i * 53) % (H - 32));
    SDL_SetTextureColorMod(sprites, 255, 255, 255);
    SDL_SetTextureAlphaMod(sprites, 255);
}
static void op_sprite_flip_h(int i) { copy_cell_ex(sprites, 32, 12, i % 48, (i * 37) % (W - 32), (i * 53) % (H - 32), SDL_FLIP_HORIZONTAL); }
static void op_sprite_flip_mod(int i)
{
    SDL_SetTextureColorMod(sprites, 180, 220, 255);
    copy_cell_ex(sprites, 32, 12, i % 48, (i * 37) % (W - 32), (i * 53) % (H - 32), SDL_FLIP_VERTICAL);
    SDL_SetTextureColorMod(sprites, 255, 255, 255);
}
static void op_tile_plain_copy(int i) { copy_cell(tiles, 8, 40, i % 1200, (i % 40) * 8, (i / 40 % 30) * 8); }
static void op_tile_copyex_none(int i) { copy_cell_ex(tiles, 8, 40, i % 1200, (i % 40) * 8, (i / 40 % 30) * 8, SDL_FLIP_NONE); }
static void op_tiny(int i) { SDL_Rect s = {0, 0, 1, 1}, d = {i % W, (i / W) % H, 1, 1}; SDL_RenderCopy(r, tiles, &s, &d); }
static void op_full_none(int i) { (void)i; SDL_RenderCopy(r, opaque_full, NULL, NULL); }
static void op_full_blend_fg(int i) { (void)i; SDL_RenderCopy(r, fg, NULL, NULL); }
static void op_full_flip_v(int i) { (void)i; SDL_RenderCopyEx(r, opaque_full, NULL, NULL, 0.0, NULL, SDL_FLIP_VERTICAL); }
static void op_bg_sub(int i) { SDL_Rect s = {i % 16, i % 16, W, H}; (void)i; SDL_RenderCopy(r, bg, &s, NULL); }
static void op_clear(int i) { (void)i; SDL_SetRenderDrawColor(r, 0, 0, 0, 0); SDL_RenderClear(r); }
static void op_fill_full_none(int i) { SDL_Rect d = {0, 0, W, H}; (void)i; SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE); SDL_SetRenderDrawColor(r, 10, 20, 30, 255); SDL_RenderFillRect(r, &d); }
static void op_fill_box_blend(int i) { SDL_Rect d = {16, 150, 288, 80}; (void)i; SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND); SDL_SetRenderDrawColor(r, 0, 0, 80, 160); SDL_RenderFillRect(r, &d); }
static void op_fill_star(int i) { SDL_Rect d = {(i * 37) % W, (i * 53) % H, 2, 2}; SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE); SDL_SetRenderDrawColor(r, 255, 255, 255, 255); SDL_RenderFillRect(r, &d); }
static void op_target_switch(int i)
{
    /* switch target and do a tiny draw (switches alone are deferred until a draw) */
    SDL_SetRenderTarget(r, (i & 1) ? t_a : t_b);
    op_tile(i);
}

int main(int argc, char **argv)
{
    SDL_Window *win;
    th_init(
#ifdef PS2_SDL_OPT
        "rt_micro_opt",
#else
        "rt_micro_stock",
#endif
        argc, argv);
    if (SDL_Init(SDL_INIT_VIDEO) != 0)
    {
        th_check("SDL_Init", 0, "%s", SDL_GetError());
        return th_finish();
    }
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    SDL_SetHint(SDL_HINT_RENDER_VSYNC, "0");
    SDL_SetHint("SDL_PS2_WINDOW_PIXEL_SIZE", "320x240");
    SDL_SetHint("SDL_PS2_PREFER_XBGR8888", "1");

    {
        /* pass 0: as configured so far; 1: + blit fast paths (patch 0002);
         * 2: + SDL_HINT_RENDER_BATCHING=1 (SDL disables batching by default
         * when the render driver is chosen by hint, as VVVVVV's PS2 build does) */
        static const char *fast[] = {"0", "1", "1"};
        static const char *batch[] = {"0", "0", "1"};
        int pass;
        for (pass = 0; pass < 3; pass++)
        {
#ifndef PS2_SDL_OPT
            if (pass == 1)
                continue; /* stock SDL has no fast paths */
#endif
            SDL_SetHint("SDL_SW_BLIT_FASTPATHS", fast[pass]);
            SDL_SetHint(SDL_HINT_RENDER_BATCHING, batch[pass]);
            th_log("=== pass %d: SDL_SW_BLIT_FASTPATHS=%s SDL_RENDER_BATCHING=%s ===", pass, fast[pass], batch[pass]);
            win = SDL_CreateWindow("rt_micro", 0, 0, 640, 448, SDL_WINDOW_ALLOW_HIGHDPI);
            r = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE);
            if (!th_check("software renderer", r != NULL, "%s", r ? "" : SDL_GetError()))
                break;

            tiles = make_sheet(320, 240, 8, 0, "tiles");
            sprites = make_sheet(384, 128, 32, 1, "sprites");
            font = make_sheet(128, 512, 8, 2, "font");
            fg = make_sheet(W, H, 8, 3, "fglayer");
            bg = make_sheet(336, 336, 8, 0, "bg");
            opaque_full = make_sheet(W, H, 8, 0, "opaque");
            SDL_SetTextureBlendMode(opaque_full, SDL_BLENDMODE_NONE);
            SDL_SetTextureBlendMode(bg, SDL_BLENDMODE_NONE);
            t_a = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, W, H);
            t_b = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, W, H);

            bench("tile 8x8 opaque, no mod", 2400, 64, op_tile, t_a);
            SDL_SetTextureColorMod(tiles, 200, 100, 50);
            bench("tile 8x8 opaque, colour mod constant", 2400, 64, op_tile_mod_const, t_a);
            SDL_SetTextureColorMod(tiles, 255, 255, 255);
            bench("tile 8x8 opaque, colour mod set/reset", 2400, 64, op_tile_mod_toggle, t_a);
            bench("glyph 8x8 30% opaque, no mod", 2400, 64, op_glyph_nomod, t_a);
            bench("glyph 8x8 30% opaque, colour mod set/reset", 2400, 64, op_glyph_mod_toggle, t_a);
            bench("sprite 32x32 78% opaque, no mod", 600, 1024, op_sprite, t_a);
            bench("sprite 32x32, colour mod set/reset", 600, 1024, op_sprite_mod_toggle, t_a);
            bench("sprite 32x32, colour+alpha128 mod", 600, 1024, op_sprite_mod_alpha, t_a);
            bench("sprite 32x32, CopyEx FLIP_H", 300, 1024, op_sprite_flip_h, t_a);
            bench("sprite 32x32, CopyEx FLIP_V + colour mod", 300, 1024, op_sprite_flip_mod, t_a);
            bench("1x1 copy (per-call overhead)", 4000, 1, op_tiny, t_a);
            bench("tile 8x8 via RenderCopy", 2400, 64, op_tile_plain_copy, t_a);
            bench("tile 8x8 via RenderCopyEx(0, NONE) (VVVVVV)", 2400, 64, op_tile_copyex_none, t_a);
            bench("320x240 copy, BLEND_NONE opaque", 30, W * H, op_full_none, t_a);
            bench("320x240 copy, BLEND fg layer (50% empty)", 30, W * H, op_full_blend_fg, t_a);
            bench("320x240 sub-rect of 336x336, NONE", 30, W * H, op_bg_sub, t_a);
            bench("320x240 CopyEx FLIP_V (flip mode)", 10, W * H, op_full_flip_v, t_a);
            bench("RenderClear 320x240 target", 60, W * H, op_clear, t_a);
            bench("FillRect 320x240 NONE", 60, W * H, op_fill_full_none, t_a);
            bench("FillRect 288x80 BLEND a=160 (textbox)", 60, 288 * 80, op_fill_box_blend, t_a);
            bench("FillRect 2x2 NONE (stars)", 2000, 4, op_fill_star, t_a);
            bench("target switch + 8x8 tile", 1200, 64, op_target_switch, t_a);

            SDL_DestroyTexture(tiles);
            SDL_DestroyTexture(sprites);
            SDL_DestroyTexture(font);
            SDL_DestroyTexture(fg);
            SDL_DestroyTexture(bg);
            SDL_DestroyTexture(opaque_full);
            SDL_DestroyTexture(t_a);
            SDL_DestroyTexture(t_b);
            SDL_DestroyRenderer(r);
            SDL_DestroyWindow(win);
        }
    }
    th_summary();
    SDL_Quit();
    return th_finish();
}
