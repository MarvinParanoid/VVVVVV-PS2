/* G1 path A: the scene display lists through SDL's software renderer, set up
 * like the game (PS2Platform.cpp PS2_PreInit) and called like the game
 * (Graphics::draw_grid_tile: QueryTexture, colour/alpha mod set and reset,
 * RenderCopyEx per tile). Measures, per frame:
 *   issue   time spent in the SDL calls (render API + command queue)
 *   flush   SDL_RenderFlush: the software renderer executing the queue
 *   present SDL_RenderPresent: window texture to the GS, stretch, vsync
 * and writes gs_poc_ref_<scene>.bin (gameTexture of frame 0, ABGR8888) as
 * the reference for gs_poc_gs.elf.
 *
 * With gs_poc_b.txt next to the ELF it instead uses SDL's own GS renderer
 * ("PS2 gsKit", path B) for the same calls. That renderer ignores
 * SetRenderTarget, so the picture is wrong and no reference is written; only
 * the CPU cost of the call stream is meaningful. */
#include <SDL.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#include "../common/harness.h"
#include "scene.h"

static SDL_Renderer *r;
static SDL_Texture *tex[TX_N];
static int path_b;

static void exec(const gp_list *l)
{
    int i;
    for (i = 0; i < l->n; i++)
    {
        const gp_op *o = &l->ops[i];
        switch (o->op)
        {
        case OP_TARGET:
            SDL_SetRenderTarget(r, o->tex == T_WINDOW ? NULL : tex[o->tex]);
            break;
        case OP_CLEAR:
            SDL_SetRenderDrawColor(r, o->r, o->g, o->b, o->a);
            SDL_RenderClear(r);
            break;
        case OP_FILL:
        {
            SDL_Rect d = {o->dx, o->dy, o->dw, o->dh};
            SDL_SetRenderDrawColor(r, o->r, o->g, o->b, o->a);
            SDL_RenderFillRect(r, &d);
            break;
        }
        case OP_COPY:
        {
            SDL_Rect s = {o->sx, o->sy, o->sw, o->sh}, d = {o->dx, o->dy, o->dw, o->dh};
            if (o->tex < TX_N_SHEETS)
            {
                int w, h;
                const int mod = (o->r & o->g & o->b & o->a) != 255;
                SDL_QueryTexture(tex[o->tex], NULL, NULL, &w, &h);
                if (mod)
                {
                    SDL_SetTextureColorMod(tex[o->tex], o->r, o->g, o->b);
                    SDL_SetTextureAlphaMod(tex[o->tex], o->a);
                }
                SDL_RenderCopyEx(r, tex[o->tex], &s, &d, 0, NULL, o->flipv ? SDL_FLIP_VERTICAL : SDL_FLIP_NONE);
                if (mod)
                {
                    SDL_SetTextureColorMod(tex[o->tex], 255, 255, 255);
                    SDL_SetTextureAlphaMod(tex[o->tex], 255);
                }
            }
            else
                SDL_RenderCopy(r, tex[o->tex], &s, &d);
            break;
        }
        }
    }
}

static void save_reference(const char *name)
{
    static uint32_t px[320 * 240];
    char path[64];
    int fd;
    SDL_SetRenderTarget(r, tex[T_GAME]);
    SDL_RenderReadPixels(r, NULL, SDL_PIXELFORMAT_ABGR8888, px, 320 * 4);
    SDL_SetRenderTarget(r, NULL);
    snprintf(path, sizeof(path), "gs_poc_ref_%s.bin", name);
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    th_check("reference written", fd >= 0 && write(fd, px, sizeof(px)) == (int)sizeof(px), "%s", path);
    if (fd >= 0)
        close(fd);
}

static void run(int scene, int frames)
{
    th_stats s_issue = {{0}, 0}, s_flush = {{0}, 0}, s_present = {{0}, 0};
    double sum_i = 0, sum_f = 0, sum_p = 0;
    gp_list l;
    int f;
    char label[96];
    for (f = 1; f <= frames; f++)
    {
        uint64_t t0, t1, t2, t3;
        gp_build_scene(scene, f, &l);
        t0 = th_now_us();
        exec(&l);
        t1 = th_now_us();
        SDL_RenderFlush(r);
        t2 = th_now_us();
        SDL_RenderPresent(r);
        t3 = th_now_us();
        th_stats_add(&s_issue, (uint32_t)(t1 - t0));
        th_stats_add(&s_flush, (uint32_t)(t2 - t1));
        th_stats_add(&s_present, (uint32_t)(t3 - t2));
        sum_i += (double)(t1 - t0), sum_f += (double)(t2 - t1), sum_p += (double)(t3 - t2);
        if (f == frames)
            th_log("SCENE %s ops=%d copies=%d fills=%d target_switches=%d (%s)", l.name, l.n, l.copies, l.fills,
                   l.targets, l.what);
        gp_free_scene(&l);
    }
    gp_build_scene(scene, 1, &l);
    snprintf(label, sizeof(label), "%s %s issue", path_b ? "B" : "A", l.name);
    th_stats_report(&s_issue, label);
    snprintf(label, sizeof(label), "%s %s flush", path_b ? "B" : "A", l.name);
    th_stats_report(&s_flush, label);
    snprintf(label, sizeof(label), "%s %s present", path_b ? "B" : "A", l.name);
    th_stats_report(&s_present, label);
    th_log("RESULT_%s %-16s issue %.2f flush %.2f present %.2f total %.2f ms (mean of %d frames)", path_b ? "B" : "A", l.name,
           sum_i / frames / 1000.0, sum_f / frames / 1000.0, sum_p / frames / 1000.0,
           (sum_i + sum_f + sum_p) / frames / 1000.0, frames);
    gp_free_scene(&l);
}

static void frame0(int scene)
{
    gp_list l;
    gp_build_scene(scene, 0, &l);
    exec(&l);
    SDL_RenderFlush(r);
    gp_free_scene(&l);
}

int main(int argc, char **argv)
{
    static gp_image sheets[TX_N_SHEETS];
    SDL_Window *win;
    int i, sc;
    {
        int fd = open("gs_poc_b.txt", O_RDONLY);
        path_b = fd >= 0;
        if (fd >= 0)
            close(fd);
    }
    th_init(path_b ? "gs_poc_b" : "gs_poc_sw", argc, argv);
    /* PS2_PreInit, minus music/diagnostics hints */
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, path_b ? "PS2 gsKit" : "software");
    SDL_SetHint(SDL_HINT_RENDER_BATCHING, "1");
    SDL_SetHint("SDL_SW_BLIT_FASTPATHS", "1");
    SDL_SetHint("SDL_PS2_P3_FASTPATHS", "1");
    SDL_SetHint(SDL_HINT_PS2_DYNAMIC_VSYNC, "1");
    SDL_SetHint("SDL_PS2_WINDOW_PIXEL_SIZE", "320x240");
    SDL_SetHint("SDL_PS2_PREFER_XBGR8888", "1");
    SDL_SetHint("SDL_PS2_RENDER_QUEUE_SIZE", "65536");
    SDL_SetHint("SDL_PS2_RENDER_PERSISTENT_QUEUE_SIZE", "16384");
    SDL_SetHint(SDL_HINT_THREAD_STACK_SIZE, "131072");
    if (SDL_Init(SDL_INIT_VIDEO) != 0)
    {
        th_check("SDL_Init", 0, "%s", SDL_GetError());
        return th_finish();
    }
    th_set_thread_priority(64);
    win = SDL_CreateWindow("gs_poc_sw", 0, 0, 640, 448, 0);
    r = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE);
    if (!th_check("renderer created", r != NULL, "%s", r ? "" : SDL_GetError()))
        return th_finish();
    {
        SDL_RendererInfo info;
        SDL_GetRendererInfo(r, &info);
        th_log("renderer: %s (path %s)", info.name, path_b ? "B" : "A");
    }

    gp_make_sheets(sheets);
    for (i = 0; i < TX_N_SHEETS; i++)
    {
        /* GraphicsResources.cpp: ABGR8888 surface -> static texture, BLEND */
        SDL_Surface *s = SDL_CreateRGBSurfaceWithFormatFrom(sheets[i].px, sheets[i].w, sheets[i].h, 32, sheets[i].w * 4,
                                                            SDL_PIXELFORMAT_ABGR8888);
        tex[i] = SDL_CreateTextureFromSurface(r, s);
        SDL_FreeSurface(s);
    }
    for (i = TX_N_SHEETS; i < TX_N; i++)
    {
        /* Graphics::create_buffers on PS2: ABGR8888 targets, blend NONE */
        tex[i] = SDL_CreateTexture(r, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_TARGET, gp_tex_w[i], gp_tex_h[i]);
        SDL_SetTextureBlendMode(tex[i], gp_tex_blend[i] ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE);
    }
    th_mem_report("textures created");

    /* references: each scene's frame 0 after a fixed prelude, then timing */
    for (sc = SC_ROOM; sc < SC_N; sc++)
    {
        gp_list l;
        if (sc == SC_ROOM)
            frame0(SC_ROOM_SETUP);
        if (sc == SC_TOWER_CLIMB)
            frame0(SC_TOWER);
        frame0(sc);
        gp_build_scene(sc, 0, &l);
        if (!path_b)
            save_reference(l.name);
        gp_free_scene(&l);
        run(sc, 60);
    }
    /* the room-entry foreground redraw on its own */
    run(SC_ROOM_SETUP, 30);
    th_summary();
    return th_finish();
}
