#include "scene.h"

#include <stdlib.h>
#include <string.h>

const int gp_tex_w[TX_N] = {320, 320, 384, 128, 320, 320, 320, 336, 336};
const int gp_tex_h[TX_N] = {240, 240, 512, 128, 240, 240, 240, 336, 336};
const uint8_t gp_tex_blend[TX_N] = {1, 1, 1, 1, 0, 0, 1, 0, 0};

static uint32_t rng;
static uint32_t rnd(void)
{
    rng = rng * 1664525u + 1013904223u;
    return rng >> 8;
}

static uint32_t abgr(int r, int g, int b, int a)
{
    return (uint32_t)r | (uint32_t)g << 8 | (uint32_t)b << 16 | (uint32_t)a << 24;
}

static void alloc_image(gp_image *im, int w, int h)
{
    im->w = w;
    im->h = h;
    im->px = (uint32_t *)calloc((size_t)w * h, 4);
}

void gp_make_sheets(gp_image s[TX_N_SHEETS])
{
    uint32_t pal[150];
    int i, x, y;
    rng = 12345;
    for (i = 0; i < 150; i++)
        pal[i] = abgr(rnd() & 255, rnd() & 255, rnd() & 255, 255);

    /* tiles: 40x30 tiles of 8x8; tile 0 empty, ~20% with holes */
    alloc_image(&s[TX_TILES], 320, 240);
    for (i = 1; i < 40 * 30; i++)
    {
        uint32_t c[4];
        int holes = rnd() % 5 == 0, k;
        for (k = 0; k < 4; k++)
            c[k] = pal[rnd() % 150];
        for (y = 0; y < 8; y++)
            for (x = 0; x < 8; x++)
            {
                uint32_t h = rnd();
                uint32_t v = c[(h >> 4) & 3];
                if (holes && (h & 3) == 0)
                    v = 0;
                s[TX_TILES].px[((i / 40) * 8 + y) * 320 + (i % 40) * 8 + x] = v;
            }
    }
    /* grayscale variant (the game's _tint textures) */
    alloc_image(&s[TX_TINT], 320, 240);
    for (i = 0; i < 320 * 240; i++)
    {
        uint32_t v = s[TX_TILES].px[i];
        int l = (int)(((v & 255) * 77 + ((v >> 8) & 255) * 150 + ((v >> 16) & 255) * 29) >> 8);
        s[TX_TINT].px[i] = abgr(l, l, l, (int)(v >> 24));
    }
    /* sprites: 12x16 cells of 32x32, white blobs; a few edge pixels carry
     * partial alpha 247..254 like sprites.png */
    alloc_image(&s[TX_SPRITES], 384, 512);
    for (i = 0; i < 12 * 16; i++)
    {
        int r2 = 80 + (int)(rnd() % 160);
        for (y = 0; y < 32; y++)
            for (x = 0; x < 32; x++)
            {
                int dx = x - 16, dy = y - 16, d = dx * dx + dy * dy, a = 0;
                if (d < r2)
                    a = 255;
                else if (d < r2 + 12 && (rnd() & 7) == 0)
                    a = 247 + (int)(rnd() % 8);
                if (a)
                    s[TX_SPRITES].px[((i / 12) * 32 + y) * 384 + (i % 12) * 32 + x] = abgr(255, 255, 255, a);
            }
    }
    /* font: 16x16 glyphs of 8x8, white masks */
    alloc_image(&s[TX_FONT], 128, 128);
    for (i = 0; i < 256; i++)
        for (y = 1; y < 7; y++)
            for (x = 1; x < 7; x++)
                if (rnd() % 5 < 2)
                    s[TX_FONT].px[((i / 16) * 8 + y) * 128 + (i % 16) * 8 + x] = abgr(255, 255, 255, 255);
}

/* ---- display lists ---- */

static gp_list *cur;
static int cap;

static gp_op *add(int op)
{
    gp_op *o;
    if (cur->n == cap)
    {
        cap = cap ? cap * 2 : 1024;
        cur->ops = (gp_op *)realloc(cur->ops, (size_t)cap * sizeof(gp_op));
    }
    o = &cur->ops[cur->n++];
    memset(o, 0, sizeof(*o));
    o->op = (uint8_t)op;
    o->r = o->g = o->b = o->a = 255;
    return o;
}

static void target(int t)
{
    add(OP_TARGET)->tex = (uint8_t)t;
    cur->targets++;
}

static void clear(int r, int g, int b, int a)
{
    gp_op *o = add(OP_CLEAR);
    o->r = (uint8_t)r, o->g = (uint8_t)g, o->b = (uint8_t)b, o->a = (uint8_t)a;
}

static void fill(int x, int y, int w, int h, int r, int g, int b)
{
    gp_op *o = add(OP_FILL);
    o->dx = (int16_t)x, o->dy = (int16_t)y, o->dw = (int16_t)w, o->dh = (int16_t)h;
    o->r = (uint8_t)r, o->g = (uint8_t)g, o->b = (uint8_t)b;
    cur->fills++;
}

static gp_op *copy(int tex, int sx, int sy, int sw, int sh, int dx, int dy, int dw, int dh)
{
    gp_op *o = add(OP_COPY);
    o->tex = (uint8_t)tex;
    o->sx = (int16_t)sx, o->sy = (int16_t)sy, o->sw = (int16_t)sw, o->sh = (int16_t)sh;
    o->dx = (int16_t)dx, o->dy = (int16_t)dy, o->dw = (int16_t)dw, o->dh = (int16_t)dh;
    cur->copies++;
    return o;
}

static void tile(int tex, int id, int x, int y)
{
    copy(tex, (id % 40) * 8, (id / 40) * 8, 8, 8, x, y, 8, 8);
}

static void colour(gp_op *o, uint32_t h)
{
    o->r = (uint8_t)(64 + (h & 127) + ((h >> 7) & 63));
    o->g = (uint8_t)(32 + ((h >> 3) & 223));
    o->b = (uint8_t)(16 + ((h >> 11) & 239));
}

/* font::print with PR_BOR (4 black passes + 1 coloured) or plain */
static void text(int x, int y, int n, int border, int scale, uint32_t seed, int flipv)
{
    int i, k;
    static const int off[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
    for (i = 0; i < n; i++)
    {
        int g = 33 + (int)((seed + (uint32_t)i * 7u) % 90);
        int gx = x + i * 8 * scale;
        gp_op *o;
        if (border)
            for (k = 0; k < 4; k++)
            {
                o = copy(TX_FONT, (g % 16) * 8, (g / 16) * 8, 8, 8, gx + off[k][0], y + off[k][1], 8 * scale, 8 * scale);
                o->r = o->g = o->b = 0;
                o->flipv = (uint8_t)flipv;
            }
        o = copy(TX_FONT, (g % 16) * 8, (g / 16) * 8, 8, 8, gx, y, 8 * scale, 8 * scale);
        colour(o, seed * 31u + (uint32_t)i);
        o->flipv = (uint8_t)flipv;
    }
}

static void sprite(int id, int x, int y, uint32_t h)
{
    gp_op *o = copy(TX_SPRITES, (id % 12) * 32, (id / 12) * 32, 32, 32, x, y, 32, 32);
    colour(o, h);
}

static void finish_frame(void)
{
    target(T_GAME);
    copy(T_GAMEPLAY, 0, 0, 320, 240, 0, 0, 320, 240);
    target(T_WINDOW);
    copy(T_GAME, 0, 0, 320, 240, 0, 0, 320, 240);
}

void gp_build_scene(int scene, int f, gp_list *out)
{
    int x, y, i;
    memset(out, 0, sizeof(*out));
    cur = out;
    cap = 0;
    rng = 777u + (uint32_t)scene;
    switch (scene)
    {
    case SC_ROOM_SETUP:
        out->name = "room_setup";
        out->what = "foreground cache redraw on room entry (Graphics::drawmap)";
        target(T_FG);
        clear(0, 0, 0, 0);
        for (y = 0; y < 30; y++)
            for (x = 0; x < 40; x++)
            {
                int id = (int)(rnd() % 1200);
                if (rnd() % 4 == 0)
                    continue; /* tile 0: not drawn */
                tile(TX_TILES, id ? id : 1, x * 8, y * 8);
            }
        break;
    case SC_ROOM:
        out->name = "room";
        out->what = "room_ss1-like: starfield, cached foreground, 12 sprites, HUD text";
        target(T_GAMEPLAY);
        clear(0, 0, 0, 255);
        fill(0, 0, 320, 240, 0, 0, 0);
        for (i = 0; i < 50; i++)
            fill((int)((rnd() + (uint32_t)f * 3u) % 320), (int)(rnd() % 240), 2, 2, 120, 120, 120);
        copy(T_FG, 0, 0, 320, 240, 0, 0, 320, 240);
        for (i = 0; i < 12; i++)
            sprite(i * 7 % 192, 20 + i * 24 + (f % 8), 40 + (i * 37) % 160, rnd());
        for (i = 0; i < 6; i++)
        {
            gp_op *o = copy(TX_TINT, 8 * (5 + i), 16, 8, 8, 100 + i * 8, 200, 8, 8);
            colour(o, rnd());
        }
        fill(0, 230, 320, 10, 0, 0, 0);
        fill(0, 0, 320, 10, 0, 0, 0);
        text(60, 231, 25, 1, 1, 11, 0);          /* room name */
        text(4, 2, 12, 1, 1, 22, 0);             /* time trial HUD */
        text(4, 12, 12, 1, 1, 33, 0);
        text(200, 2, 28, 0, 1, 44, 1);          /* flipped 1x line */
        finish_frame();
        break;
    case SC_TOWER:
    case SC_TOWER_CLIMB:
    {
        int off = (f * 2) % 16;
        out->name = scene == SC_TOWER ? "tower" : "tower_climb";
        out->what = scene == SC_TOWER ? "tower worst case: full background redraw (logic) + 1240 fg tiles"
                                      : "tower climbing: background scroll + 84 new tiles + 1240 fg tiles";
        target(T_TOWER);
        if (scene == SC_TOWER)
        {
            clear(0, 0, 0, 255);
            for (y = 0; y < 40; y++)
                for (x = 0; x < 33; x++)
                    tile(TX_TILES, 1 + (int)(rnd() % 1199), x * 8 + 4, y * 8 + 8);
        }
        else
        {
            target(T_TEMP);
            clear(0, 0, 0, 0);
            copy(T_TOWER, 0, 0, 336, 336, 0, -2, 336, 336);
            target(T_TOWER);
            copy(T_TEMP, 0, 0, 336, 336, 0, 0, 336, 336);
            for (x = 0; x < 42; x++)
            {
                tile(TX_TILES, 1 + (int)(rnd() % 1199), x * 8, 320);
                tile(TX_TILES, 1 + (int)(rnd() % 1199), x * 8, 328);
            }
        }
        target(T_GAMEPLAY);
        clear(0, 0, 0, 255);
        copy(T_TOWER, 8, 8 + off, 320, 240, 0, 0, 320, 240);
        for (y = 0; y < 31; y++)
            for (x = 0; x < 40; x++)
                tile(TX_TILES, 1 + (int)(rnd() % 1199), x * 8, y * 8 - off % 8);
        for (i = 0; i < 80; i++)
            tile(TX_TILES, 1 + (int)(rnd() % 1199), (i % 40) * 8, i < 40 ? 0 : 232);
        for (i = 0; i < 8; i++)
            sprite(i * 11 % 192, 30 + i * 33, 60 + (i * 53 + f) % 140, rnd());
        text(80, 112, 30, 1, 1, 55, 0);
        finish_frame();
        break;
    }
    case SC_GRAVITRON:
    case SC_GRAVITRON_FLIP:
    {
        const int fl = scene == SC_GRAVITRON_FLIP;
        out->name = fl ? "gravitron_flip" : "super_gravitron";
        out->what = fl ? "Super Gravitron in flip mode: 2x text flipped vertically"
                       : "Super Gravitron: 11 nested fills, 20 sprites, 2x bordered text";
        target(T_GAMEPLAY);
        clear(0, 0, 0, 255);
        for (i = 0; i < 11; i++)
            fill(i * 14, i * 10, 320 - i * 28, 240 - i * 20, 30 + i * 18, 10 + i * 9, 60 + i * 12);
        fill(0, 0, 320, 4, 255, 255, 255);
        fill(0, 236, 320, 4, 255, 255, 255);
        fill(0, 100, 320, 2, 200, 200, 200);
        for (i = 0; i < 20; i++)
            sprite(i * 5 % 192, (i * 41 + f * 3) % 320 - 16, 20 + (i * 29) % 190, rnd());
        text(8, 30, 9, 1, 2, 66, fl);
        text(8, 60, 9, 1, 2, 77, fl);
        text(8, 180, 9, 1, 2, 88, fl);
        text(8, 210, 18, 1, 2, 99, fl);
        finish_frame();
        break;
    }
    }
}

void gp_free_scene(gp_list *l)
{
    free(l->ops);
    l->ops = NULL;
}
