/* G1 path C: the scene display lists as raw GIF packets on the GS.
 *
 * - Render targets live in VRAM (PSMCT32, page aligned); a target is used as
 *   a texture by pointing TEX0 at it. Image sheets are PSMT8 + CT32 CLUT.
 * - Vertex colour and CLUT values follow the GS colour range (0x80 = 1.0).
 *   White sheets (sprites, font) store RGB 0x80 in the CLUT, so MODULATE
 *   gives exactly the modulation colour, as SDL does for white texels.
 * - BLEND copies: ALPHA (Cs-Cd)*As>>7+Cd, alpha test drops As == 0 texels
 *   (SDL leaves those destination pixels untouched too).
 *
 * Per frame it measures: build (EE writes the packet), kick (cache flush +
 * DMA start), gs (DMA + GS until FINISH; emulator timing in PCSX2), and it
 * compares the frame-0 gameTexture with gs_poc_ref_<scene>.bin from
 * gs_poc_sw.elf. Also measures texture/CLUT/framebuffer uploads.
 * Standalone: does not use SDL or any game code. */
#include <fcntl.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <dma.h>
#include <gif_tags.h>
#include <graph.h>
#include <gs_gp.h>
#include <gs_psm.h>
#include <iopcontrol.h>
#include <kernel.h>
#include <ps2_filesystem_driver.h>
#include <sbv_patches.h>
#include <screenshot.h>
#include <sifrpc.h>

#include "../common/harness.h"
#include "scene.h"

/* ---- hardware access: GIF DMA (channel 2) and GS CSR ---- */
#define D2_CHCR (*(volatile u32 *)0x1000A000)
#define D2_MADR (*(volatile u32 *)0x1000A010)
#define D2_QWC (*(volatile u32 *)0x1000A020)
#define GS_CSR (*(volatile u64 *)0x12001000)
#define CSR_FINISH 2

static void gif_send(const void *data, int qwc)
{
    while (D2_CHCR & 0x100)
        ;
    D2_MADR = (u32)data;
    D2_QWC = (u32)qwc;
    D2_CHCR = 0x101; /* from memory, normal mode, start */
}

static void gif_wait(void)
{
    while (D2_CHCR & 0x100)
        ;
}

static void gs_wait_finish(void)
{
    while (!(GS_CSR & CSR_FINISH))
        ;
}

/* ---- VRAM layout (byte addresses) ---- */
typedef struct
{
    u32 addr; /* bytes */
    int bw;   /* buffer width in 64-pixel units */
    int psm, w, h, tw, th;
} vram_buf;

static vram_buf vb[TX_N], display, clut[TX_N_SHEETS];
static u32 vram_top;

static int log2ceil(int v)
{
    int n = 0;
    while ((1 << n) < v)
        n++;
    return n;
}

static void vram_alloc(vram_buf *b, int w, int h, int psm, int bufw, int bufh)
{
    int bpp = psm == GS_PSM_8 ? 1 : 4;
    u32 bytes = (u32)bufw * (u32)bufh * (u32)bpp;
    vram_top = (vram_top + 8191) & ~8191u; /* page */
    b->addr = vram_top;
    b->bw = bufw / 64;
    b->psm = psm;
    b->w = w, b->h = h;
    b->tw = log2ceil(bufw), b->th = log2ceil(bufh);
    vram_top += bytes;
}

/* ---- packet writer: A+D blocks and REGLIST sprite runs ---- */
#define PKT_U64 (128 * 1024)
static u64 *pkt, *P, *ad_tag, *run_tag;
static int ad_n, run_n, run_nreg;
static u64 run_prim, run_regs;

static void close_run(void)
{
    if (run_tag)
    {
        run_tag[0] = GIF_SET_TAG(run_n, 0, 0, 0, 1, run_nreg);
        run_tag[1] = run_regs;
        run_tag = NULL;
    }
}

static void close_ad(int eop)
{
    if (ad_tag)
    {
        ad_tag[0] = GIF_SET_TAG(ad_n, eop, 0, 0, 0, 1);
        ad_tag[1] = GIF_REG_AD;
        ad_tag = NULL;
    }
}

static void ad(int reg, u64 val)
{
    close_run();
    if (!ad_tag)
    {
        ad_tag = P;
        P += 2;
        ad_n = 0;
    }
    P[0] = val;
    P[1] = (u64)reg;
    P += 2;
    ad_n++;
}

/* sprite runs: RGBAQ UV XYZ2 UV XYZ2 NOP (textured) or RGBAQ XYZ2 XYZ2 NOP */
#define REGS_TEX 0xF53531ull
#define REGS_FLAT 0xF551ull
static void run_begin(u64 prim, int textured)
{
    int nreg = textured ? 6 : 4;
    u64 regs = textured ? REGS_TEX : REGS_FLAT;
    if (run_tag && run_prim == prim && run_regs == regs && run_n < 32000)
        return;
    /* the GIFtag PRIM field (PRE) only applies in PACKED mode, so set PRIM
     * through A+D before a REGLIST run (found by testing: without it the GS
     * drew single points) */
    ad(GS_REG_PRIM, prim);
    close_ad(0);
    run_tag = P;
    P += 2;
    run_n = 0;
    run_prim = prim, run_regs = regs, run_nreg = nreg;
}

#define Q1 0x3F800000u
#define XY(v) (u64)(((v) + 2048) << 4)

/* u/v in 1/16 texel (GS fixed point) */
static void sprite_tex(int r, int g, int b, int a, int u0, int v0, int u1, int v1, int x0, int y0, int x1, int y1)
{
    P[0] = GIF_SET_RGBAQ(r, g, b, a, Q1);
    P[1] = GIF_SET_UV(u0, v0);
    P[2] = GIF_SET_XYZ(XY(x0), XY(y0), 0);
    P[3] = GIF_SET_UV(u1, v1);
    P[4] = GIF_SET_XYZ(XY(x1), XY(y1), 0);
    P[5] = 0;
    P += 6;
    run_n++;
}

static void sprite_flat(int r, int g, int b, int a, int x0, int y0, int x1, int y1)
{
    P[0] = GIF_SET_RGBAQ(r, g, b, a, Q1);
    P[1] = GIF_SET_XYZ(XY(x0), XY(y0), 0);
    P[2] = GIF_SET_XYZ(XY(x1), XY(y1), 0);
    P[3] = 0;
    P += 4;
    run_n++;
}

/* 0..255 (SDL) -> 0..0x80 (GS) */
static int gs8(int v)
{
    return (v * 128 + 127) / 255;
}

/* ---- display list -> packet ---- */
static int st_target, st_tex, st_test;

static void set_test(int alpha_test)
{
    if (st_test == alpha_test)
        return;
    st_test = alpha_test;
    /* ATE, NOTEQUAL 0, fail=KEEP; z test on + ALWAYS (ZTE=0 is not allowed) */
    ad(GS_REG_TEST, GS_SET_TEST(alpha_test, 7, 0, 0, 0, 0, 1, 1));
}

static void set_tex(int t)
{
    const vram_buf *b = &vb[t];
    if (st_tex == t)
        return;
    st_tex = t;
    if (t < TX_N_SHEETS)
        ad(GS_REG_TEX0, GS_SET_TEX0(b->addr / 256, b->bw, GS_PSM_8, b->tw, b->th, 1, 0, clut[t].addr / 256, 0, 0, 0, 1));
    else
        ad(GS_REG_TEX0, GS_SET_TEX0(b->addr / 256, b->bw, GS_PSM_32, b->tw, b->th, 1, 0, 0, 0, 0, 0, 0));
}

static void exec(const gp_list *l)
{
    int i;
    for (i = 0; i < l->n; i++)
    {
        const gp_op *o = &l->ops[i];
        const vram_buf *tb = st_target == T_WINDOW ? &display : &vb[st_target];
        switch (o->op)
        {
        case OP_TARGET:
            st_target = o->tex;
            tb = st_target == T_WINDOW ? &display : &vb[st_target];
            ad(GS_REG_FRAME, GS_SET_FRAME(tb->addr / 8192, tb->bw, GS_PSM_32, 0));
            ad(GS_REG_SCISSOR, GS_SET_SCISSOR(0, tb->w - 1, 0, tb->h - 1));
            ad(GS_REG_TEXFLUSH, 0);
            break;
        case OP_CLEAR:
        case OP_FILL:
        {
            int x = o->op == OP_FILL ? o->dx : 0, y = o->op == OP_FILL ? o->dy : 0;
            int w = o->op == OP_FILL ? o->dw : tb->w, h = o->op == OP_FILL ? o->dh : tb->h;
            set_test(0);
            run_begin(GS_SET_PRIM(GS_PRIM_SPRITE, 0, 0, 0, 0, 0, 1, 0, 0), 0);
            sprite_flat(o->r, o->g, o->b, gs8(o->a), x, y, x + w, y + h);
            break;
        }
        case OP_COPY:
        {
            const int blend = gp_tex_blend[o->tex];
            int r, g, b, v0 = o->sy * 16, v1 = (o->sy + o->sh) * 16;
            int dx = o->dx, dy = o->dy, dw = o->dw, dh = o->dh;
            if (o->tex == TX_SPRITES || o->tex == TX_FONT)
                r = o->r, g = o->g, b = o->b; /* CLUT holds 0x80: exact */
            else if (o->tex < TX_N_SHEETS)
                r = gs8(o->r), g = gs8(o->g), b = gs8(o->b);
            else
                r = g = b = 0x80;
            /* flipped: V runs downwards; the sprite samples at the top edge
             * of each pixel row, so start just below the texel boundary or
             * the first row reads the texel outside the source rectangle
             * (seen as a one-row shift against SDL in 2x flipped text) */
            if (o->flipv)
                v0 = (o->sy + o->sh) * 16 - 1, v1 = o->sy * 16 - 1;
            if (st_target == T_WINDOW) /* final copy: GS stretch to 640x448 */
                dx = 0, dy = 0, dw = display.w, dh = display.h;
            set_tex(o->tex);
            set_test(blend);
            run_begin(GS_SET_PRIM(GS_PRIM_SPRITE, 0, 1, 0, blend, 0, 1, 0, 0), 1);
            sprite_tex(r, g, b, gs8(o->a), o->sx * 16, v0, (o->sx + o->sw) * 16, v1, dx, dy, dx + dw, dy + dh);
            break;
        }
        }
    }
}

static void frame_begin(void)
{
    P = pkt;
    ad_tag = run_tag = NULL;
}

static int frame_end(void)
{
    close_run();
    ad(GS_REG_FINISH, 0);
    close_ad(1);
    return (int)(P - pkt) / 2; /* qwords */
}

static void kick_and_wait(int qwc, uint64_t *t_kicked)
{
    GS_CSR = CSR_FINISH; /* clear the previous FINISH event */
    FlushCache(0);
    gif_send(pkt, qwc);
    if (t_kicked)
        *t_kicked = th_now_us();
    gif_wait();
    gs_wait_finish();
}

static void setup_context(void)
{
    frame_begin();
    ad(GS_REG_PRMODECONT, 1);
    ad(GS_REG_XYOFFSET, GS_SET_XYOFFSET(2048 << 4, 2048 << 4));
    ad(GS_REG_ALPHA, GS_SET_ALPHA(0, 1, 0, 1, 0));
    ad(GS_REG_TEX1, GS_SET_TEX1(1, 0, 0, 0, 0, 0, 0));
    ad(GS_REG_CLAMP, GS_SET_CLAMP(1, 1, 0, 0, 0, 0));
    ad(GS_REG_COLCLAMP, 1);
    ad(GS_REG_DTHE, 0);
    ad(GS_REG_PABE, 0);
    ad(GS_REG_FBA, 0);
    ad(GS_REG_ZBUF, GS_SET_ZBUF(0, 0, 1));
    ad(GS_REG_TEST, GS_SET_TEST(0, 7, 0, 0, 0, 0, 1, 1));
    st_test = 0;
    st_tex = -1;
    st_target = T_WINDOW;
    ad(GS_REG_FRAME, GS_SET_FRAME(display.addr / 8192, display.bw, GS_PSM_32, 0));
    ad(GS_REG_SCISSOR, GS_SET_SCISSOR(0, display.w - 1, 0, display.h - 1));
    kick_and_wait(frame_end(), NULL);
}

/* ---- uploads (EE -> VRAM image transfers) ---- */
static u64 up_hdr[16] __attribute__((aligned(64)));

static void upload(const vram_buf *b, int w, int h, int psm, const void *data, int bytes)
{
    u64 *q = up_hdr;
    int qwc = bytes / 16;
    q[0] = GIF_SET_TAG(4, 0, 0, 0, 0, 1);
    q[1] = GIF_REG_AD;
    q[2] = GS_SET_BITBLTBUF(0, 0, 0, b->addr / 256, b->bw, psm);
    q[3] = GS_REG_BITBLTBUF;
    q[4] = GS_SET_TRXPOS(0, 0, 0, 0, 0);
    q[5] = GS_REG_TRXPOS;
    q[6] = GS_SET_TRXREG(w, h);
    q[7] = GS_REG_TRXREG;
    q[8] = GS_SET_TRXDIR(0);
    q[9] = GS_REG_TRXDIR;
    q[10] = GIF_SET_TAG(qwc, 0, 0, 0, 2, 0); /* IMAGE */
    q[11] = 0;
    /* then FINISH (A+D) after the data, as a separate small packet */
    q[12] = GIF_SET_TAG(1, 1, 0, 0, 0, 1);
    q[13] = GIF_REG_AD;
    q[14] = 0;
    q[15] = GS_REG_FINISH;
    GS_CSR = CSR_FINISH;
    FlushCache(0);
    gif_send(up_hdr, 6);
    gif_send(data, qwc);
    gif_send(up_hdr + 12, 2);
    gif_wait();
    gs_wait_finish();
}

static double time_upload(const vram_buf *b, int w, int h, int psm, const void *data, int bytes, int reps)
{
    uint64_t t0 = th_now_us();
    int i;
    for (i = 0; i < reps; i++)
        upload(b, w, h, psm, data, bytes);
    return (double)(th_now_us() - t0) / reps;
}

/* PSMT8 CLUT in CSM1 order: entries 8-15 and 16-23 of each 32 swap */
static int csm1(int i)
{
    return (i & 0xE7) | ((i & 0x08) << 1) | ((i & 0x10) >> 1);
}

static u8 *idx[TX_N_SHEETS];
static u32 *clutdata[TX_N_SHEETS];
static int idx_bytes[TX_N_SHEETS], ncolours[TX_N_SHEETS];

static int palettise(int t, const gp_image *im)
{
    const vram_buf *b = &vb[t];
    int bufw = b->bw * 64, bufh = 1 << b->th, x, y, n = 0, k;
    u32 pal[256];
    idx_bytes[t] = bufw * bufh;
    idx[t] = (u8 *)memalign(64, (size_t)idx_bytes[t]);
    clutdata[t] = (u32 *)memalign(64, 256 * 4);
    memset(idx[t], 0, (size_t)idx_bytes[t]);
    memset(clutdata[t], 0, 1024);
    pal[n++] = 0; /* index 0: transparent */
    for (y = 0; y < im->h; y++)
        for (x = 0; x < im->w; x++)
        {
            u32 v = im->px[y * im->w + x];
            if ((v >> 24) == 0)
                v = 0;
            for (k = 0; k < n && pal[k] != v; k++)
                ;
            if (k == n)
            {
                if (n == 256)
                    return -1;
                pal[n++] = v;
            }
            idx[t][y * bufw + x] = (u8)k;
        }
    for (k = 0; k < n; k++)
    {
        u32 v = pal[k];
        int a = gs8((int)(v >> 24));
        if (t == TX_SPRITES || t == TX_FONT)
            v = 0x808080u; /* white sheets: 0x80 so MODULATE yields the mod colour */
        clutdata[t][csm1(k)] = (v & 0xFFFFFFu) | (u32)a << 24;
    }
    ncolours[t] = n;
    return n;
}

/* ---- readback and comparison ---- */
static u32 gsbuf[320 * 240] __attribute__((aligned(64)));
static u32 refbuf[320 * 240];

static void readback(const vram_buf *b)
{
    int y;
    /* 32-line bands (PCSX2 returns at most ~40 lines per CT32 read); a CT32
     * page is 32 lines high, so band k starts k page rows further on and is
     * read from y=0 (a non-zero y returned zeros here) */
    for (y = 0; y < b->h; y += 32)
        ps2_screenshot(gsbuf + y * b->w, (b->addr + (u32)(y / 32) * (u32)b->bw * 8192u) / 256, 0, 0, b->w,
                       b->h - y < 32 ? b->h - y : 32, GS_PSM_32);
    FlushCache(0);
}

static void save_ppm(const char *name, const u32 *px)
{
    static u8 row[320 * 3];
    char path[64];
    int fd, x, y;
    snprintf(path, sizeof(path), "%s", name);
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
        return;
    write(fd, "P6\n320 240\n255\n", 15);
    for (y = 0; y < 240; y++)
    {
        for (x = 0; x < 320; x++)
            memcpy(row + x * 3, &px[y * 320 + x], 3);
        write(fd, row, sizeof(row));
    }
    close(fd);
}

static void compare(const char *name)
{
    char path[64];
    int fd, i, c, exact = 0, d1 = 0, d4 = 0, maxd = 0, blank = 1;
    snprintf(path, sizeof(path), "gs_poc_ref_%s.bin", name);
    fd = open(path, O_RDONLY);
    if (!th_check("reference present", fd >= 0, "%s (run gs_poc_sw.elf first)", path))
        return;
    read(fd, refbuf, sizeof(refbuf));
    close(fd);
    for (i = 0; i < 320 * 240; i++)
    {
        int m = 0;
        if (gsbuf[i] & 0xFFFFFF)
            blank = 0;
        for (c = 0; c < 24; c += 8)
        {
            int d = (int)((gsbuf[i] >> c) & 255) - (int)((refbuf[i] >> c) & 255);
            if (d < 0)
                d = -d;
            if (d > m)
                m = d;
        }
        if (m == 0)
            exact++;
        else if (m <= 1)
            d1++;
        else if (m > 4)
            d4++;
        if (m > maxd)
            maxd = m;
    }
    if (blank)
    {
        th_log("CORRECT %-16s readback all black: GS readback unsupported here?", name);
        return;
    }
    th_log("CORRECT %-16s exact %.3f%% (%d), off by 1: %d, off by >4: %d, max channel delta %d (of %d pixels)", name,
           exact * 100.0 / (320 * 240), exact, d1, d4, maxd, 320 * 240);
    snprintf(path, sizeof(path), "gs_poc_gs_%s.ppm", name);
    save_ppm(path, gsbuf);
}

/* ---- scenes ---- */
static void frame0(int scene)
{
    gp_list l;
    gp_build_scene(scene, 0, &l);
    frame_begin();
    exec(&l);
    kick_and_wait(frame_end(), NULL);
    gp_free_scene(&l);
}

static void run(int scene, int frames)
{
    th_stats s_build = {{0}, 0}, s_gs = {{0}, 0};
    double sb = 0, sk = 0, sg = 0;
    int f, qwc = 0, maxq = 0;
    gp_list l;
    char label[96];
    for (f = 1; f <= frames; f++)
    {
        uint64_t t0, t1, t2, t3;
        gp_build_scene(scene, f, &l);
        t0 = th_now_us();
        frame_begin();
        exec(&l);
        qwc = frame_end();
        t1 = th_now_us();
        kick_and_wait(qwc, &t2);
        t3 = th_now_us();
        if (qwc > maxq)
            maxq = qwc;
        th_stats_add(&s_build, (uint32_t)(t1 - t0));
        th_stats_add(&s_gs, (uint32_t)(t3 - t2));
        sb += (double)(t1 - t0), sk += (double)(t2 - t1), sg += (double)(t3 - t2);
        if (f < frames)
            gp_free_scene(&l);
    }
    snprintf(label, sizeof(label), "C %s build", l.name);
    th_stats_report(&s_build, label);
    snprintf(label, sizeof(label), "C %s gs", l.name);
    th_stats_report(&s_gs, label);
    th_log("RESULT_C %-16s build %.2f kick %.2f gs %.2f total %.2f ms | ops=%d copies=%d fills=%d targets=%d | packet %d qw (%d KiB), %.2f us/op build",
           l.name, sb / frames / 1000.0, sk / frames / 1000.0, sg / frames / 1000.0, (sb + sk + sg) / frames / 1000.0,
           l.n, l.copies, l.fills, l.targets, maxq, maxq * 16 / 1024, sb / frames / l.n);
    gp_free_scene(&l);
}

int main(int argc, char **argv)
{
    static gp_image sheets[TX_N_SHEETS];
    static const char *names[TX_N] = {"tiles", "tint", "sprites", "font", "game", "gameplay", "fg", "tower", "temp"};
    u32 *fb32;
    int i, sc;

    SifInitRpc(0);
    while (!SifIopReset(NULL, 0))
        ;
    while (!SifIopSync())
        ;
    SifInitRpc(0);
    sbv_patch_enable_lmb();
    sbv_patch_disable_prefix_check();
    sbv_patch_fileio();
    init_ps2_filesystem_driver();
    {
        /* USB (mass:) appears a while after the drivers load, as in fs_smoke */
        static char cwd[256];
        if (getcwd(cwd, sizeof(cwd)))
            waitUntilDeviceIsReady(cwd);
    }
    th_init("gs_poc_gs", argc, argv);
    th_set_thread_priority(64);

    pkt = (u64 *)memalign(64, PKT_U64 * 8);
    fb32 = (u32 *)memalign(64, 320 * 240 * 4);

    /* VRAM: display 640x448 CT32, targets CT32, sheets PSMT8, CLUTs */
    vram_alloc(&display, 640, 448, GS_PSM_32, 640, 448);
    for (i = TX_N_SHEETS; i < TX_N; i++)
        vram_alloc(&vb[i], gp_tex_w[i], gp_tex_h[i], GS_PSM_32, (gp_tex_w[i] + 63) & ~63, (gp_tex_h[i] + 31) & ~31);
    for (i = 0; i < TX_N_SHEETS; i++)
        vram_alloc(&vb[i], gp_tex_w[i], gp_tex_h[i], GS_PSM_8, 1 << log2ceil(gp_tex_w[i]) < 128 ? 128 : 1 << log2ceil(gp_tex_w[i]),
                   1 << log2ceil(gp_tex_h[i]));
    for (i = 0; i < TX_N_SHEETS; i++)
    {
        vram_top = (vram_top + 255) & ~255u;
        clut[i].addr = vram_top;
        clut[i].bw = 1;
        vram_top += 1024;
    }
    th_log("VRAM display @%u, %u bytes used of 4194304", display.addr, vram_top);
    for (i = 0; i < TX_N; i++)
        th_log("VRAM %-9s @%7u %dx%d %s bw=%d tw=%d th=%d", names[i], vb[i].addr, vb[i].w, vb[i].h,
               vb[i].psm == GS_PSM_8 ? "PSMT8" : "CT32", vb[i].bw, vb[i].tw, vb[i].th);
    if (!th_check("VRAM fits", vram_top <= 4194304u, "%u bytes", vram_top))
        return th_finish();

    graph_initialize(0, 640, 448, GS_PSM_32, 0, 0);
    dma_channel_initialize(DMA_CHANNEL_GIF, NULL, 0);
    setup_context();
    {
        /* sanity: a flat fill into gameTexture, read back */
        u64 csr0 = GS_CSR, csr1;
        GS_CSR = CSR_FINISH;
        csr1 = GS_CSR;
        frame_begin();
        st_target = T_GAME;
        ad(GS_REG_FRAME, GS_SET_FRAME(vb[T_GAME].addr / 8192, vb[T_GAME].bw, GS_PSM_32, 0));
        ad(GS_REG_SCISSOR, GS_SET_SCISSOR(0, 319, 0, 239));
        run_begin(GS_SET_PRIM(GS_PRIM_SPRITE, 0, 0, 0, 0, 0, 1, 0, 0), 0);
        sprite_flat(10, 20, 30, 0x80, 0, 0, 320, 240);
        kick_and_wait(frame_end(), NULL);
        readback(&vb[T_GAME]);
        th_log("SANITY csr before=%llx after clear=%llx now=%llx; gameTexture px0=%08x px(160,120)=%08x", csr0, csr1,
               (unsigned long long)GS_CSR, gsbuf[0], gsbuf[120 * 320 + 160]);
        {
            char line[128];
            int y, x, n, k = 0;
            for (y = 0; y < 240; y += 32)
            {
                for (n = 0, x = 0; x < 320 * (240 - y < 32 ? 240 - y : 32); x++)
                    n += gsbuf[y * 320 + x] != 0;
                k += snprintf(line + k, sizeof(line) - (size_t)k, " %d", n);
            }
            th_log("SANITY nonzero per 32-line band:%s", line);
            /* readback check without drawing: upload a known image */
            for (x = 0; x < 320 * 240; x++)
                fb32[x] = 0x11000000u | (u32)x;
            upload(&vb[T_GAME], 320, 240, GS_PSM_32, fb32, 320 * 240 * 4);
            readback(&vb[T_GAME]);
            for (n = 0, x = 0; x < 320 * 240; x++)
                n += gsbuf[x] == fb32[x];
            th_log("SANITY uploaded image read back: %d of 76800 pixels match (px(160,120)=%08x)", n,
                   gsbuf[120 * 320 + 160]);
        }
    }

    gp_make_sheets(sheets);
    for (i = 0; i < TX_N_SHEETS; i++)
    {
        int n = palettise(i, &sheets[i]);
        th_check("sheet palettised losslessly", n > 0, "%s: %d colours", names[i], n);
    }

    /* uploads: one-time sheet and CLUT uploads, and the 300 KB CT32 frame a
     * per-frame upload (today's window texture, or a hybrid) would cost */
    for (i = 0; i < TX_N_SHEETS; i++)
    {
        double us = time_upload(&vb[i], vb[i].bw * 64, 1 << vb[i].th, GS_PSM_8, idx[i], idx_bytes[i], 10);
        th_log("UPLOAD %-8s PSMT8 %dx%d %d KiB: %.0f us", names[i], vb[i].bw * 64, 1 << vb[i].th, idx_bytes[i] / 1024, us);
        us = time_upload(&clut[i], 16, 16, GS_PSM_32, clutdata[i], 1024, 10);
        th_log("UPLOAD %-8s CLUT 16x16 CT32 1 KiB: %.0f us", names[i], us);
    }
    memset(fb32, 0x40, 320 * 240 * 4);
    th_log("UPLOAD CT32 320x240 (300 KiB, a full frame): %.0f us",
           time_upload(&vb[T_TEMP], 320, 240, GS_PSM_32, fb32, 320 * 240 * 4, 10));

    for (sc = SC_ROOM; sc < SC_N; sc++)
    {
        gp_list l;
        if (sc == SC_ROOM)
            frame0(SC_ROOM_SETUP);
        if (sc == SC_TOWER_CLIMB)
            frame0(SC_TOWER);
        frame0(sc);
        gp_build_scene(sc, 0, &l);
        readback(&vb[T_GAME]);
        compare(l.name);
        gp_free_scene(&l);
        run(sc, 60);
    }
    run(SC_ROOM_SETUP, 30);
    th_summary();
    th_finish();
    SleepThread();
    return 0;
}
