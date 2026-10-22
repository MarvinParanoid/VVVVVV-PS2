/* rt_scene: replicas of real VVVVVV 2.4.4 frames, built from per-frame
 * operation counts read out of the game's render code (see
 * docs/ps2-p1-report.md section "Real workload"), timed on the full Option A
 * path (software renderer -> 320x240 XBGR8888 texture framebuffer -> GS).
 *
 * The *counts* are estimates derived from the game source; the *times* are
 * measured. Sheets are procedural stand-ins (no game assets).
 *
 * Every scene follows the game's frame skeleton (Render.cpp gamerender +
 * Graphics::render): window clear x2, switch game -> gameplay, background,
 * foreground copy (BLEND), entities, room name (PR_BOR: 4 black + 1 colour
 * pass per glyph, all colour-modded), switch -> game, gameplay -> game copy
 * (NONE), overlays, switch -> window, clear, letterboxed copy, present. */
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>

#include "../common/harness.h"

#define SW 320
#define SH 240

static SDL_Renderer *r;
static SDL_Texture *tiles, *sprites, *font, *t_game, *t_gameplay, *t_fg, *t_bg, *t_temp;

static Uint32 hash32(Uint32 x)
{
    x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
    return x;
}

static SDL_Texture *sheet(int w, int h, int cell, int kind)
{
    Uint32 *px = (Uint32 *)malloc((size_t)w * h * 4);
    int x, y;
    SDL_Texture *t;
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++)
        {
            Uint32 c = hash32((Uint32)((x / cell) * 977 + (y / cell))) | 0xFF000000u;
            int cx = x % cell - cell / 2, cy = y % cell - cell / 2, on = 1;
            if (kind == 1) /* sprite: ~55% opaque blob */
                on = cx * cx + cy * cy < (cell * cell) * 18 / 100;
            else if (kind == 2) /* white glyph bits, ~30% */
            {
                on = (hash32((Uint32)(x * 131 + y * 7919)) % 100) < 30;
                c = 0xFFFFFFFFu;
            }
            px[y * w + x] = on ? c : 0;
        }
    t = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, w, h);
    SDL_UpdateTexture(t, NULL, px, w * 4);
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND); /* GraphicsResources.cpp: loaded PNGs */
    free(px);
    return t;
}

static SDL_Texture *target(int w, int h)
{
    /* render targets keep the default NONE blend mode (Graphics.cpp) */
    return SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, w, h);
}

static void cell(SDL_Texture *t, int size, int cols, int idx, int x, int y)
{
    SDL_Rect s, d;
    s.x = (idx % cols) * size; s.y = (idx / cols) * size; s.w = size; s.h = size;
    d.x = x; d.y = y; d.w = size; d.h = size;
    /* draw_texture_part: CopyEx with flip NONE -> SDL fast path to RenderCopy */
    SDL_RenderCopyEx(r, t, &s, &d, 0.0, NULL, SDL_FLIP_NONE);
}

/* Graphics::draw_grid_tile with colour: set rgb + alpha, draw, reset */
static void cell_mod(SDL_Texture *t, int size, int cols, int idx, int x, int y, Uint8 cr, Uint8 cg, Uint8 cb)
{
    SDL_SetTextureColorMod(t, cr, cg, cb);
    SDL_SetTextureAlphaMod(t, 255);
    cell(t, size, cols, idx, x, y);
    SDL_SetTextureColorMod(t, 255, 255, 255);
    SDL_SetTextureAlphaMod(t, 255);
}

static void fill(int x, int y, int w, int h, Uint8 cr, Uint8 cg, Uint8 cb)
{
    SDL_Rect d;
    d.x = x; d.y = y; d.w = w; d.h = h;
    SDL_SetRenderDrawColor(r, cr, cg, cb, 255);
    SDL_RenderFillRect(r, &d);
}

/* font::print: one colour-modded 8x8 glyph per char; PR_BOR adds 4 black passes */
static void print(int x, int y, int chars, int border, Uint8 cr, Uint8 cg, Uint8 cb)
{
    int pass, i;
    static const int off[4][2] = {{0, -1}, {-1, 0}, {1, 0}, {0, 1}};
    if (border)
        for (pass = 0; pass < 4; pass++)
            for (i = 0; i < chars; i++)
                cell_mod(font, 8, 16, 33 + i % 90, x + i * 8 + off[pass][0], y + off[pass][1], 0, 0, 0);
    for (i = 0; i < chars; i++)
        cell_mod(font, 8, 16, 33 + i % 90, x + i * 8, y, cr, cg, cb);
}

/* Graphics::drawpixeltextbox: opaque fill + colour-modded border tiles + text */
static void textbox(int x, int y, int cols, int lines)
{
    int w = cols * 8 + 16, h = lines * 8 + 16, i;
    fill(x, y, w, h, 0, 0, 0);
    for (i = 0; i < w / 8; i++)
    {
        cell_mod(tiles, 8, 40, 1100, x + i * 8, y, 164, 164, 255);
        cell_mod(tiles, 8, 40, 1101, x + i * 8, y + h - 8, 164, 164, 255);
    }
    for (i = 1; i < h / 8 - 1; i++)
    {
        cell_mod(tiles, 8, 40, 1102, x, y + i * 8, 164, 164, 255);
        cell_mod(tiles, 8, 40, 1103, x + w - 8, y + i * 8, 164, 164, 255);
    }
    for (i = 0; i < lines; i++)
        print(x + 8, y + 8 + i * 8, cols, 0, 164, 164, 255);
}

static void sprites_n(int n)
{
    int i;
    for (i = 0; i < n; i++)
        cell_mod(sprites, 32, 12, i % 48, 20 + (int)(hash32(i) % 260), 20 + (int)(hash32(i + 77) % 180),
                 (Uint8)hash32(i), (Uint8)hash32(i + 1), 255);
}

static void rebuild_fg(int ntiles)
{
    int i;
    SDL_SetRenderTarget(r, t_fg);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 0);
    SDL_RenderClear(r);
    for (i = 0; i < ntiles; i++)
        cell(tiles, 8, 40, (i * 7) % 1000, (i % 40) * 8, (i / 40 % 30) * 8);
}

/* Graphics::scroll_texture (warp/tower bg, logic rate) + new strip of tiles */
static void scroll_bg(int strip_tiles)
{
    int i;
    SDL_Rect src = {0, 0, 336, 336}, dst = {0, 2, 336, 336};
    SDL_SetRenderTarget(r, t_temp);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 0);
    SDL_RenderClear(r);
    SDL_RenderCopy(r, t_bg, &src, &dst);
    SDL_SetRenderTarget(r, t_bg);
    SDL_RenderCopy(r, t_temp, NULL, NULL);
    for (i = 0; i < strip_tiles; i++)
        cell(tiles, 8, 40, i % 1000, (i % 42) * 8, (i / 42) * 8);
}

enum { BG_STARS, BG_GRAVITRON, BG_TOWER };

typedef struct
{
    const char *name;
    int bg;
    int fg_cached;       /* 1: copy cached foreground; 0: draw tiles every frame (towers) */
    int frame_tiles;     /* uncached 8x8 tiles per frame (towers, hallway bg) */
    int sprites;         /* 32x32 colour-modded */
    int small_tiles;     /* 8x8 no-mod entity tiles (platforms, conveyors) */
    int roomname_chars;  /* PR_BOR */
    int dialogue;        /* 2 boxes + "press ACTION" prompt + cutscene bars */
    int hud_glyphs;      /* plain colour-modded glyphs (Gravitron HUD etc.) */
    int scroll_strip;    /* >0: logic-rate bg scroll with this many tiles (every frame here) */
    int fg_rebuild_every;/* >0: foreground rebuilt every n frames (final stretch: 2) */
    int fg_tiles;        /* nonzero tiles in the cached foreground */
} scene;

static void frame(const scene *sc, int f)
{
    int i;
    SDL_Rect dst;
    int ow, oh;

    if (sc->fg_rebuild_every && f % sc->fg_rebuild_every == 0)
        rebuild_fg(sc->fg_tiles);
    if (sc->scroll_strip)
        scroll_bg(sc->scroll_strip);

    /* Screen::present ends with a clear, main.cpp clears again (2 of the 3
     * per-frame window clears; the third is in Graphics::render below) */
    SDL_SetRenderTarget(r, NULL);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    SDL_RenderClear(r);
    SDL_RenderClear(r);
    SDL_SetRenderTarget(r, t_game);
    SDL_SetRenderTarget(r, t_gameplay);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

    switch (sc->bg)
    {
    case BG_STARS:
        fill(0, 0, SW, SH, 0, 0, 0);
        for (i = 0; i < 50; i++)
            fill((int)(hash32(i) % SW), (int)((hash32(i + 9) + f) % SH), 2, 2, 255, 255, 255);
        break;
    case BG_GRAVITRON: /* bg 5: 11 nested fills, ~5 screens of pixels */
        for (i = 0; i < 11; i++)
        {
            int w = SW + 160 - i * 40, h = SH + 120 - i * 30;
            fill((SW - w) / 2, (SH - h) / 2, w, h, (Uint8)(i * 20), 0, (Uint8)(200 - i * 15));
        }
        break;
    case BG_TOWER: /* drawtowerbackground: clear + copy of the scrolled bg */
    {
        SDL_Rect s = {8, 8, SW, SH};
        SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
        SDL_RenderClear(r);
        SDL_RenderCopy(r, t_bg, &s, NULL);
        break;
    }
    }

    if (sc->fg_cached)
        SDL_RenderCopy(r, t_fg, NULL, NULL);
    for (i = 0; i < sc->frame_tiles; i++)
        cell(tiles, 8, 40, (i * 13) % 1000, (i % 40) * 8, (i / 40 % 30) * 8);

    sprites_n(sc->sprites);
    for (i = 0; i < sc->small_tiles; i++)
        cell(tiles, 8, 40, 500 + i % 8, 100 + (i % 20) * 8, 150 + (i / 20) * 8);
    for (i = 0; i < sc->hud_glyphs; i++)
        cell_mod(font, 8, 16, 33 + i % 90, (i % 38) * 8 + 4, 4 + (i / 38) * 10, 255, 255, 255 - (Uint8)i);

    if (sc->roomname_chars)
    {
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        fill(0, SH - 10, SW, 10, 0, 0, 0);
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
        print((SW - sc->roomname_chars * 8) / 2, SH - 9, sc->roomname_chars, 1, 196, 196, 255 - 64);
    }
    if (sc->dialogue)
    {
        fill(0, 0, SW, 16, 0, 0, 0);
        fill(0, SH - 16, SW, 16, 0, 0, 0);
        textbox(24, 40, 30, 4);
        textbox(24, 120, 30, 3);
    }

    SDL_SetRenderTarget(r, t_game);
    SDL_RenderCopy(r, t_gameplay, NULL, NULL);
    if (sc->dialogue)
        print(32, 200, 32, 1, 255, 255, 255); /* "- Press ACTION to advance text -" */

    SDL_SetRenderTarget(r, NULL);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    SDL_RenderClear(r);
    SDL_GetRendererOutputSize(r, &ow, &oh);
    dst.h = oh; dst.w = oh * SW / SH;
    if (dst.w > ow) { dst.w = ow; dst.h = ow * SH / SW; }
    dst.x = (ow - dst.w) / 2; dst.y = (oh - dst.h) / 2;
    SDL_RenderCopy(r, t_game, NULL, &dst);
    SDL_RenderPresent(r);
}

static const scene scenes[] = {
    /* name,                               bg,          fgc, ftiles, spr, small, room, dlg, hud, scroll, fgre, fgt */
    {"a  typical room (stars, 6 sprites)",  BG_STARS,     1,     0,   6,    4,  15,   0,   0,     0,    0, 755},
    {"b  dialogue (a + 2 boxes + prompt)",  BG_STARS,     1,     0,   9,    4,  15,   1,   0,     0,    0, 755},
    {"c1 Super Gravitron",                  BG_GRAVITRON, 1,     0,  14,    0,   0,   0, 250,     0,    0, 400},
    {"c2 The Tower (uncached tiles)",       BG_TOWER,     0,  1220,   3,    0,   0,   0,   0,   160,    0, 0},
    {"c3 Panic Room minitower",             BG_TOWER,     0,  1290,   5,    0,  16,   0,   0,   160,    0, 0},
    {"c4 tower hallway bg (1200 tiles)",    BG_STARS,     1,  1200,   3,    0,  16,   0,   0,     0,    0, 617},
    {"c5 final stretch (fg rebuild /2)",    BG_STARS,     1,     0,  14,    0,  16,   0,   0,     0,    2, 1116},
    {"c7 The Warning (12 spr, 156 tiles)",  BG_STARS,     1,     0,  12,  156,  12,   0,   0,     0,    0, 800},
};

int main(int argc, char **argv)
{
    SDL_Window *win;
    int pass, k;
    th_init(
#ifdef PS2_SDL_OPT
        "rt_scene_opt",
#else
        "rt_scene_stock",
#endif
        argc, argv);
    SDL_SetHint(SDL_HINT_THREAD_STACK_SIZE, "131072");
    if (SDL_Init(SDL_INIT_VIDEO) != 0)
    {
        th_check("SDL_Init", 0, "%s", SDL_GetError());
        return th_finish();
    }
    th_set_thread_priority(64);
    /* P1 config B (VVVVVV PS2 settings) */
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    SDL_SetHint(SDL_HINT_RENDER_VSYNC, "0");
    SDL_SetHint("SDL_PS2_WINDOW_PIXEL_SIZE", "320x240");
    SDL_SetHint("SDL_PS2_PREFER_XBGR8888", "1");
    SDL_SetHint("SDL_PS2_RENDER_QUEUE_SIZE", "65536");
    SDL_SetHint("SDL_PS2_RENDER_PERSISTENT_QUEUE_SIZE", "16384");

    for (pass = 0; pass < 3; pass++)
    {
        /* 0: P1 config B; 1: + blit fast paths (0002); 2: + render batching */
        static const char *fast[] = {"0", "1", "1"};
        static const char *batch[] = {"0", "0", "1"};
#ifndef PS2_SDL_OPT
        if (pass == 1)
            continue;
#endif
        SDL_SetHint("SDL_SW_BLIT_FASTPATHS", fast[pass]);
        SDL_SetHint(SDL_HINT_RENDER_BATCHING, batch[pass]);
        th_log("=== pass %d: SDL_SW_BLIT_FASTPATHS=%s SDL_RENDER_BATCHING=%s ===", pass, fast[pass], batch[pass]);
        win = SDL_CreateWindow("rt_scene", 0, 0, 640, 448, SDL_WINDOW_ALLOW_HIGHDPI);
        r = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE);
        if (!th_check("software renderer", r != NULL, "%s", r ? "" : SDL_GetError()))
            break;
        tiles = sheet(320, 240, 8, 0);
        sprites = sheet(384, 128, 32, 1);
        font = sheet(128, 512, 8, 2);
        t_game = target(SW, SH);
        t_gameplay = target(SW, SH);
        t_fg = target(SW, SH);
        SDL_SetTextureBlendMode(t_fg, SDL_BLENDMODE_BLEND); /* Graphics.cpp:2814 */
        t_bg = target(336, 336);
        t_temp = target(336, 336);
        for (k = 0; k < (int)(sizeof(scenes) / sizeof(scenes[0])); k++)
        {
            const scene *sc = &scenes[k];
            th_stats st = {{0}, 0};
            uint64_t prev, now, t0;
            int f, frames = 90;
            char label[96];
            rebuild_fg(sc->fg_tiles);
            frame(sc, 0); /* warm-up */
            t0 = prev = th_now_us();
            for (f = 1; f <= frames; f++)
            {
                frame(sc, f);
                now = th_now_us();
                th_stats_add(&st, (uint32_t)(now - prev));
                prev = now;
            }
            snprintf(label, sizeof(label), "%s [pass %d]", sc->name, pass);
            th_stats_report(&st, label);
            th_log("SCENE %-44s mean %.1f ms", label, (double)(th_now_us() - t0) / frames / 1000.0);
        }
        SDL_DestroyTexture(tiles);
        SDL_DestroyTexture(sprites);
        SDL_DestroyTexture(font);
        SDL_DestroyTexture(t_game);
        SDL_DestroyTexture(t_gameplay);
        SDL_DestroyTexture(t_fg);
        SDL_DestroyTexture(t_bg);
        SDL_DestroyTexture(t_temp);
        SDL_DestroyRenderer(r);
        SDL_DestroyWindow(win);
    }
    th_summary();
    SDL_Quit();
    return th_finish();
}
