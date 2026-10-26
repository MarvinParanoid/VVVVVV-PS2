/* sdl_bisect: what in the game's start-up breaks USB file I/O on a real PS2?
 *
 * io_bisect (2026-10-26, console) cleared the IOP subsystems: USB reads stay
 * correct with the audio driver, audsrv playback, the joystick driver and 8
 * polled pad slots. The game still hangs or reads bad data right after
 * SDL_Init, before any window exists. What the game does there that
 * io_bisect did not: SDL_Init itself, lowering the main thread to priority 64
 * (PS2_PostInit), file access through PhysicsFS (archive kept open, pthread
 * mutexes, O_CLOEXEC), then the SDL audio device, the renderer and the music
 * thread reading data-ps2.zip in parallel.
 *
 * This program repeats that start-up in the game's order, with SDL's own
 * main() bringing up the IOP, and after each step does rounds of the game's
 * file operations: PhysicsFS open of a missing file, a 4 KB write and read
 * back, PhysicsFS reads of every graphics/ and sounds/ file and four chunks
 * of vvvvvvmusic.vvv (CRCs compared with the first round), plus the raw
 * CRC-checked reads of fs_smoke_expect.txt. sdl_bisect.log is rewritten after
 * every round and lists the EE threads after every step.
 *
 * Screen: text until the renderer step; from then on one bar per round (green
 * ok, red bad), a yellow marker showing the operation in progress and, when
 * everything is done, a wide white bar.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <SDL.h>
#include <physfs.h>

#include <debug.h>
#include <kernel.h>
#include <timer.h>

#define LOG_CAP (96 * 1024)
static char log_buf[LOG_CAP];
static int log_len;
static int screen_row;
static int text_screen = 1; /* until the renderer takes the GS */
static int quiet;

static SDL_Renderer *renderer;
static int rounds_done, bad_rounds[64];
static int op_index, op_count = 1;
static int finished;

static unsigned long long now_us(void)
{
    return GetTimerSystemTime() * 1000ULL / (kBUSCLK / 1000ULL);
}

static void save_log(void)
{
    int fd = open("sdl_bisect.log", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        write(fd, log_buf, log_len);
        close(fd);
    }
}

static void out(const char *fmt, ...)
{
    char line[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    if (text_screen && !quiet && screen_row < 25) {
        scr_setXY(0, screen_row++);
        scr_printf("%s", line);
    }
    printf("%s", line);
    if (log_len + (int)strlen(line) < LOG_CAP) {
        memcpy(log_buf + log_len, line, strlen(line));
        log_len += (int)strlen(line);
    }
}

static void draw_progress(void)
{
    SDL_Rect r;
    int i;
    SDL_SetRenderDrawColor(renderer, 0, 0, 40, 255);
    SDL_RenderClear(renderer);
    for (i = 0; i < rounds_done && i < 64; i++) {
        r.x = 16 + (i % 32) * 9;
        r.y = 40 + (i / 32) * 40;
        r.w = 7;
        r.h = 32;
        if (bad_rounds[i])
            SDL_SetRenderDrawColor(renderer, 220, 0, 0, 255);
        else
            SDL_SetRenderDrawColor(renderer, 0, 200, 0, 255);
        SDL_RenderFillRect(renderer, &r);
    }
    r.x = 16 + op_index * 288 / (op_count > 0 ? op_count : 1);
    r.y = 140;
    r.w = 6;
    r.h = 20;
    SDL_SetRenderDrawColor(renderer, 255, 220, 0, 255);
    SDL_RenderFillRect(renderer, &r);
    if (finished) {
        /* done: a wide white bar under the round bars */
        r.x = 16;
        r.y = 180;
        r.w = 288;
        r.h = 24;
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        SDL_RenderFillRect(renderer, &r);
    }
    SDL_RenderPresent(renderer);
}

/* the operation in progress: bottom text line, or the marker once video is up */
static void doing(const char *what)
{
    if (text_screen) {
        scr_setXY(0, 27);
        scr_printf("doing: %-60.60s", what);
    } else {
        draw_progress();
    }
    op_index++;
}

static unsigned long crc_table[256];

static unsigned long crc32_update(unsigned long crc, const unsigned char *p, size_t n)
{
    if (crc_table[1] == 0) {
        unsigned long c;
        int i, k;
        for (i = 0; i < 256; i++) {
            c = (unsigned long)i;
            for (k = 0; k < 8; k++)
                c = (c >> 1) ^ (0xEDB88320UL & (0UL - (c & 1)));
            crc_table[i] = c;
        }
    }
    crc = ~crc & 0xFFFFFFFFUL;
    while (n--)
        crc = crc_table[(crc ^ *p++) & 0xFF] ^ (crc >> 8);
    return ~crc & 0xFFFFFFFFUL;
}

/* ---- raw reads with known CRCs (fs_smoke_expect.txt), as in io_bisect ---- */
#define MAX_REGIONS 32
static struct {
    char path[64];
    long off, len;
    unsigned long crc;
} regions[MAX_REGIONS];
static int n_regions;

static void load_expect(void)
{
    static char text[8192];
    char *line, *save = NULL;
    int fd = open("fs_smoke_expect.txt", O_RDONLY), n;
    if (fd < 0) {
        out("fs_smoke_expect.txt missing: no raw CRC checks\n");
        return;
    }
    n = read(fd, text, sizeof(text) - 1);
    close(fd);
    text[n > 0 ? n : 0] = '\0';
    for (line = strtok_r(text, "\n", &save); line && n_regions < MAX_REGIONS; line = strtok_r(NULL, "\n", &save)) {
        char kind[8];
        if (line[0] == '#' || sscanf(line, "%7s %63s", kind, regions[n_regions].path) != 2)
            continue;
        if (strcmp(kind, "file") == 0) {
            regions[n_regions].off = 0;
            if (sscanf(line, "%*s %*s %ld %lx", &regions[n_regions].len, &regions[n_regions].crc) == 2 &&
                regions[n_regions].len < 128 * 1024)
                n_regions++;
        } else if (sscanf(line, "%*s %*s %ld %ld %lx", &regions[n_regions].off, &regions[n_regions].len,
                          &regions[n_regions].crc) == 3) {
            n_regions++;
        }
    }
}

/* ---- PhysicsFS reads, CRCs taken in the first round ---- */
#define MAX_ASSETS 64
static struct {
    char name[64];
    long off, len; /* len -1: whole file */
    unsigned long crc;
    long size;
} assets[MAX_ASSETS];
static int n_assets, assets_baselined;
static unsigned char buf[65536] __attribute__((aligned(64)));

static void add_dir(const char *dir)
{
    char **list = PHYSFS_enumerateFiles(dir), **i;
    if (!list)
        return;
    for (i = list; *i && n_assets < MAX_ASSETS - 4; i++) {
        snprintf(assets[n_assets].name, sizeof(assets[n_assets].name), "%s/%s", dir, *i);
        assets[n_assets].len = -1;
        n_assets++;
    }
    PHYSFS_freeList(list);
}

static void list_assets(void)
{
    static const long music_offs[4] = {0, 7 * 1024 * 1024 + 4321, 19 * 1024 * 1024 + 777, 33 * 1024 * 1024 + 12345};
    int k;
    add_dir("graphics");
    add_dir("sounds");
    for (k = 0; k < 4; k++) {
        strcpy(assets[n_assets].name, "vvvvvvmusic.vvv");
        assets[n_assets].off = music_offs[k];
        assets[n_assets].len = 65536;
        n_assets++;
    }
}

static int read_asset(int i, unsigned long *crc_out, long *size_out)
{
    PHYSFS_File *f = PHYSFS_openRead(assets[i].name);
    unsigned long crc = 0;
    long left, got = 0;
    if (!f)
        return -1;
    *size_out = (long)PHYSFS_fileLength(f);
    left = assets[i].len < 0 ? *size_out : assets[i].len;
    if (assets[i].off && !PHYSFS_seek(f, (PHYSFS_uint64)assets[i].off)) {
        PHYSFS_close(f);
        return -2;
    }
    while (left > 0) {
        PHYSFS_sint64 r = PHYSFS_readBytes(f, buf, left < (long)sizeof(buf) ? (PHYSFS_uint64)left : sizeof(buf));
        if (r <= 0)
            break;
        crc = crc32_update(crc, buf, (size_t)r);
        left -= (long)r;
        got += (long)r;
    }
    PHYSFS_close(f);
    *crc_out = crc;
    return left == 0 ? 0 : -3;
}

/* ---- one round of the game's start-up file operations ---- */
static int io_round(const char *step, int round)
{
    unsigned long long t0 = now_us(), t_missing, t_write, t_physfs, t_raw;
    int i, bad = 0, fd, missing_ok, write_ok = 0, delete_ok = 0;
    const char *write_err = "";
    char what[96];
    PHYSFS_File *f;

    op_index = 0;
    op_count = 3 + n_assets + n_regions;

    doing("PhysicsFS open of a missing file (saves/qsave.vvv)");
    t_missing = now_us();
    f = PHYSFS_openRead("saves/qsave.vvv");
    missing_ok = f == NULL;
    if (f)
        PHYSFS_close(f);
    t_missing = now_us() - t_missing;

    doing("PhysicsFS write + read back (saves/bisect.tmp)");
    t_write = now_us();
    for (i = 0; i < 4096; i++)
        buf[i] = (unsigned char)(i * 7 + round);
    f = PHYSFS_openWrite("saves/bisect.tmp");
    if (!f) {
        write_err = "openWrite";
    } else {
        unsigned long want = crc32_update(0, buf, 4096);
        if (PHYSFS_writeBytes(f, buf, 4096) != 4096)
            write_err = "writeBytes";
        /* the game ignores PHYSFS_close's result (its flush may fail on host:) */
        PHYSFS_close(f);
        memset(buf, 0, 4096);
        f = PHYSFS_openRead("saves/bisect.tmp");
        if (!f) {
            write_err = "openRead";
        } else {
            if (PHYSFS_readBytes(f, buf, 4096) != 4096)
                write_err = "readBytes";
            else if (crc32_update(0, buf, 4096) != want)
                write_err = "CRC";
            PHYSFS_close(f);
        }
        /* host: in PCSX2 cannot delete; reported, not counted as bad */
        delete_ok = PHYSFS_delete("saves/bisect.tmp") != 0;
    }
    write_ok = !*write_err;
    if (!write_ok) {
        static char why[96];
        snprintf(why, sizeof(why), "%s: %s", write_err, PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
        write_err = why;
    }
    t_write = now_us() - t_write;
    if (!write_ok)
        bad++;

    t_physfs = now_us();
    for (i = 0; i < n_assets; i++) {
        unsigned long crc = 0;
        long size = 0;
        int rc;
        snprintf(what, sizeof(what), "PhysicsFS read %s @%ld", assets[i].name, assets[i].off);
        doing(what);
        rc = read_asset(i, &crc, &size);
        if (!assets_baselined) {
            assets[i].crc = crc;
            assets[i].size = size;
            if (rc != 0) {
                quiet = 0;
                out("  baseline: %s rc %d\n", assets[i].name, rc);
                bad++;
            }
        } else if (rc != 0 || crc != assets[i].crc || size != assets[i].size) {
            int was = quiet;
            bad++;
            quiet = 0;
            out("  BAD %s @%ld: rc %d size %ld/%ld crc %08lx want %08lx\n", assets[i].name, assets[i].off, rc, size,
                assets[i].size, crc, assets[i].crc);
            quiet = was;
        }
    }
    assets_baselined = 1;
    t_physfs = now_us() - t_physfs;

    t_raw = now_us();
    for (i = 0; i < n_regions; i++) {
        unsigned long crc = 0;
        long left = regions[i].len, got = 0;
        snprintf(what, sizeof(what), "raw read %s @%ld (%ld B)", regions[i].path, regions[i].off, regions[i].len);
        doing(what);
        fd = open(regions[i].path, O_RDONLY);
        if (fd < 0) {
            bad++;
            continue;
        }
        if (lseek(fd, regions[i].off, SEEK_SET) == regions[i].off) {
            while (left > 0) {
                int r = read(fd, buf, left < (long)sizeof(buf) ? (int)left : (int)sizeof(buf));
                if (r <= 0)
                    break;
                crc = crc32_update(crc, buf, (size_t)r);
                left -= r;
                got += r;
            }
        }
        close(fd);
        if (got != regions[i].len || crc != regions[i].crc) {
            int was = quiet;
            bad++;
            quiet = 0;
            out("  BAD raw %s @%ld: %ld of %ld B, crc %08lx want %08lx\n", regions[i].path, regions[i].off, got,
                regions[i].len, crc, regions[i].crc);
            quiet = was;
        }
    }
    t_raw = now_us() - t_raw;

    if (!write_ok) {
        int was = quiet;
        quiet = 0;
        out("  write failed: %s\n", write_err);
        quiet = was;
    }
    out("%-8s #%d: missing %s %llu ms | write %s%s %llu ms | physfs %d files %llu ms | raw %d %llu ms | %s %llu ms\n",
        step, round, missing_ok ? "ok" : "OPENED?", t_missing / 1000, write_ok ? "ok" : "BAD", delete_ok ? "" : " (no delete)", t_write / 1000, n_assets,
        t_physfs / 1000, n_regions, t_raw / 1000, bad ? "BAD" : "ok", (now_us() - t0) / 1000);
    if (rounds_done < 64)
        bad_rounds[rounds_done] = bad;
    rounds_done++;
    save_log();
    if (!text_screen)
        draw_progress();
    else
        doing("idle");
    return bad;
}

static void run_step(const char *step, int rounds)
{
    int r;
    for (r = 1; r <= rounds; r++)
        io_round(step, r);
}

/* every EE thread: id, status, priority, PC-ish func field (the kernel stub it
 * sleeps in), wait type and object */
static void list_threads(void)
{
    static const char *states[] = {"?", "RUN", "READY", "?", "WAIT", "?", "?", "?", "SUSP"};
    ee_thread_status_t st;
    int id;
    out("  threads (self %d):", GetThreadId());
    for (id = 1; id < 80; id++) {
        memset(&st, 0, sizeof(st));
        if (ReferThreadStatus(id, &st) < 0 || (st.status == 0 && st.initial_priority == 0 && !st.func))
            continue;
        out(" [%d %s%s p%d/%d f%p w%u:%u]", id, st.status <= 8 ? states[st.status] : "",
            st.status == 0x0c ? "WAITSUSP" : st.status == 0x10 ? "DORMANT" : "", st.current_priority,
            st.initial_priority, st.func, (unsigned)st.waitType, (unsigned)st.waitId);
    }
    out("\n");
}

/* ---- like the game's audio: an SDL device fed by SDL's audio thread ---- */
static volatile unsigned long audio_callbacks;
static void SDLCALL audio_cb(void *userdata, Uint8 *stream, int len)
{
    (void)userdata;
    memset(stream, 0, (size_t)len);
    audio_callbacks++;
}

/* ---- like the game's music thread: PhysicsFS reads of the music blob ---- */
static volatile unsigned long music_reads, music_bad;
static volatile int music_stop;
static int SDLCALL music_thread(void *arg)
{
    static unsigned char mbuf[16384] __attribute__((aligned(64)));
    PHYSFS_File *f = PHYSFS_openRead("vvvvvvmusic.vvv");
    PHYSFS_sint64 size;
    unsigned long long pos = 0;
    (void)arg;
    if (!f) {
        music_bad++;
        return 0;
    }
    size = PHYSFS_fileLength(f);
    while (!music_stop) {
        if (PHYSFS_readBytes(f, mbuf, sizeof(mbuf)) != (PHYSFS_sint64)sizeof(mbuf)) {
            pos = (pos + 1234567) % (unsigned long long)(size - (PHYSFS_sint64)sizeof(mbuf));
            if (!PHYSFS_seek(f, pos))
                music_bad++;
        }
        music_reads++;
        SDL_Delay(20); /* the game's worker refills a ring, not flat out */
    }
    PHYSFS_close(f);
    return 0;
}

int main(int argc, char *argv[])
{
    char cwd[256], path[300];
    const char *sep;
    SDL_AudioSpec want, have;
    SDL_AudioDeviceID dev;
    SDL_Window *window;
    SDL_Thread *music;
    int rc;
    unsigned long long t0;

    init_scr();
    scr_setCursor(0);
    out("VVVVVV PS2 sdl_bisect: what in the game's start-up breaks USB I/O\n");
    if (!getcwd(cwd, sizeof(cwd)))
        strcpy(cwd, "?");
    out("argv[0] %s  cwd %s\n", argc > 0 ? argv[0] : "?", cwd);
    list_threads();

    /* FILESYSTEM_init: base dir mounted and writable, data-ps2.zip mounted */
    doing("PhysicsFS init and mounts");
    rc = PHYSFS_init(argc > 0 ? argv[0] : NULL);
    PHYSFS_permitSymbolicLinks(1);
    sep = cwd[strlen(cwd) - 1] == '/' ? "" : "/";
    snprintf(path, sizeof(path), "%s%ssavedata/", cwd, sep);
    mkdir(path, 0777);
    rc = rc && PHYSFS_mount(path, NULL, 0) && PHYSFS_setWriteDir(path);
    snprintf(path, sizeof(path), "%s%ssavedata/saves/", cwd, sep);
    mkdir(path, 0777);
    snprintf(path, sizeof(path), "%s%sdata-ps2.zip", cwd, sep);
    rc = rc && PHYSFS_mount(path, NULL, 1);
    out("PhysicsFS: %s (%s)\n", rc ? "ok" : "FAILED", rc ? path : PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
    load_expect();
    list_assets();
    out("%d PhysicsFS reads + %d raw regions per round\n", n_assets, n_regions);

    run_step("1 base", 2);

    /* the game's hints (PS2_PreInit) and SDL_Init flags */
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    SDL_SetHint(SDL_HINT_RENDER_BATCHING, "1");
    SDL_SetHint("SDL_SW_BLIT_FASTPATHS", "1");
    SDL_SetHint(SDL_HINT_PS2_DYNAMIC_VSYNC, "1");
    SDL_SetHint("SDL_PS2_WINDOW_PIXEL_SIZE", "320x240");
    SDL_SetHint("SDL_PS2_PREFER_XBGR8888", "1");
    SDL_SetHint("SDL_PS2_RENDER_QUEUE_SIZE", "65536");
    SDL_SetHint("SDL_PS2_RENDER_PERSISTENT_QUEUE_SIZE", "16384");
    SDL_SetHint(SDL_HINT_THREAD_STACK_SIZE, "131072");
    doing("SDL_Init(VIDEO | AUDIO | JOYSTICK | GAMECONTROLLER)");
    t0 = now_us();
    rc = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER);
    out("SDL_Init: %d (%llu ms) video=%s audio=%s joysticks=%d\n", rc, (now_us() - t0) / 1000,
        SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "-",
        SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "-", SDL_NumJoysticks());
    list_threads();
    run_step("2 SDL", 2);

    /* PS2_PostInit */
    doing("ChangeThreadPriority(main, 64)");
    rc = ChangeThreadPriority(GetThreadId(), 64);
    out("main thread priority 64: %d\n", rc);
    list_threads();
    run_step("3 prio64", 3);

    doing("SDL_OpenAudioDevice 44100 S16 stereo 1024, unpaused");
    SDL_zero(want);
    want.freq = 44100;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = audio_cb;
    dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (dev)
        SDL_PauseAudioDevice(dev, 0);
    out("audio device %u: %d Hz, %d samples\n", (unsigned)dev, have.freq, have.samples);
    list_threads();
    run_step("4 audio", 3);
    out("  audio callbacks so far: %lu\n", audio_callbacks);

    doing("window + renderer (game hints); screen switches to bars");
    window = SDL_CreateWindow("sdl_bisect", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 640, 480, 0);
    renderer = window ? SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC) : NULL;
    out("window %p renderer %p %s\n", (void *)window, (void *)renderer, renderer ? "" : SDL_GetError());
    if (renderer) {
        SDL_RenderSetLogicalSize(renderer, 320, 240);
        text_screen = 0;
    }
    list_threads();
    run_step("5 video", 3);

    music = SDL_CreateThread(music_thread, "vvvvvv_music", NULL);
    out("music thread %p\n", (void *)music);
    list_threads();
    run_step("6 music", 3);

    out("7: 12 more rounds with everything running\n");
    quiet = 1;
    run_step("7 soak", 12);
    quiet = 0;
    music_stop = 1;
    if (music)
        SDL_WaitThread(music, NULL);
    out("audio callbacks %lu, music reads %lu (bad seeks %lu)\n", audio_callbacks, music_reads, music_bad);
    list_threads();
    out("DONE: no hang. Log: sdl_bisect.log\n");
    save_log();
    finished = 1;
    if (text_screen)
        doing("finished - photograph the screen, then reset");
    for (;;) {
        if (renderer)
            draw_progress();
        SDL_Delay(500);
    }
    return 0;
}
