/* G1 feasibility proof of concept: VVVVVV-like scenes as a display list that
 * both test programs execute.
 *
 *   gs_poc_sw.elf  runs the list through SDL's software renderer configured
 *                  exactly like the game (path A) and saves the result as the
 *                  reference;
 *   gs_poc_gs.elf  runs the same list as raw GIF packets on the GS with render
 *                  targets in VRAM and paletted textures (path C), measures
 *                  command generation, GS completion and uploads, and
 *                  compares its output with the reference.
 *
 * All images are generated here (no game assets). Op counts per scene follow
 * the RC2 benchmark (cmds/copies/fills per render, docs/ps2-gs-renderer-
 * feasibility.md). Standalone: nothing here is used by the game. */
#ifndef GS_POC_SCENE_H
#define GS_POC_SCENE_H

#include <stdint.h>

/* textures: 4 image sheets + 5 render targets, as in Graphics.cpp */
enum
{
    TX_TILES,    /* tiles.png-like: 320x240, ~150 colours, binary alpha */
    TX_TINT,     /* grayscale variant, drawn with colour modulation */
    TX_SPRITES,  /* sprites.png-like: 384x512 white + alpha mask (TEX_WHITE) */
    TX_FONT,     /* 128x128 white glyph masks */
    TX_N_SHEETS,
    T_GAME = TX_N_SHEETS, /* 320x240 targets */
    T_GAMEPLAY,
    T_FG,                 /* foregroundTexture: the only BLEND target */
    T_TOWER,              /* 336x336 */
    T_TEMP,               /* 336x336 (tempScrollingTexture) */
    TX_N,
    T_WINDOW = 255
};

enum
{
    OP_TARGET,
    OP_CLEAR,
    OP_COPY,
    OP_FILL
};

typedef struct
{
    uint8_t op, tex, flipv, pad;
    int16_t sx, sy, sw, sh;
    int16_t dx, dy, dw, dh;
    uint8_t r, g, b, a;
} gp_op;

typedef struct
{
    const char *name;
    const char *what;
    gp_op *ops;
    int n;
    int copies, fills, targets;
} gp_list;

#define SHEET_MAX_W 384
#define SHEET_MAX_H 512

typedef struct
{
    int w, h;
    uint32_t *px; /* ABGR8888 as SDL stores it: bytes R,G,B,A */
} gp_image;

extern const int gp_tex_w[TX_N], gp_tex_h[TX_N];
/* blend mode a copy from this texture uses (the game never changes them per
 * draw): 1 = BLEND, 0 = NONE */
extern const uint8_t gp_tex_blend[TX_N];

void gp_make_sheets(gp_image sheets[TX_N_SHEETS]);

/* scenes; each list is one frame. *_setup lists run once before timing. */
enum
{
    SC_ROOM_SETUP,   /* foreground cache redraw on room entry: 1200 tiles */
    SC_ROOM,         /* room_ss1: ~300 copies, ~55 fills */
    SC_TOWER,        /* tower worst case: bg full redraw (logic) + render */
    SC_TOWER_CLIMB,  /* tower typical: bg scroll + new rows + render */
    SC_GRAVITRON,    /* nested fills + 2x text */
    SC_GRAVITRON_FLIP, /* same in flip mode: 2x text flipped (SDL's slow path) */
    SC_N
};
void gp_build_scene(int scene, int frame, gp_list *out);
void gp_free_scene(gp_list *l);

#endif
