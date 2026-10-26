/* io_bisect: which IOP subsystem breaks USB file I/O on a real PS2?
 *
 * Found on the first console test (2026-10-26): fs_smoke reads the stick
 * correctly, but the game hangs or reads bad data right after SDL_Init, at a
 * different file each time. fs_smoke and the game bring the IOP up the same
 * way; SDL_Init then adds the audio driver (libsd + audsrv, plus SDL's audio
 * thread feeding audsrv) and the joystick driver (sio2man/padman/mtapman,
 * multitap ports and 8 pad slots opened, polled every frame).
 *
 * This program adds those one at a time, the way SDL does, and after each
 * step repeats a round of the file operations the game does at start-up:
 * open a file that does not exist (where the game hung), mkdir of an existing
 * directory, and CRC-checked reads of data-ps2.zip chunks and small files
 * from fs_smoke_expect.txt. The operation in progress is always shown on the
 * bottom line, so a hang shows where it is; io_bisect.log is rewritten after
 * every round. Run it from the folder with data-ps2.zip and
 * fs_smoke_expect.txt (any VVVVVV package folder). Standalone: no SDL.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <audsrv.h>
#include <debug.h>
#include <iopcontrol.h>
#include <kernel.h>
#include <libmtap.h>
#include <libpad.h>
#include <ps2_audio_driver.h>
#include <ps2_filesystem_driver.h>
#include <ps2_joystick_driver.h>
#include <sbv_patches.h>
#include <sifrpc.h>
#include <timer.h>

#define LOG_CAP (48 * 1024)
static char log_buf[LOG_CAP];
static int log_len;
static int screen_row;
static int quiet; /* soak rounds: only problems go to the screen */

static unsigned long long now_us(void)
{
    return GetTimerSystemTime() * 1000ULL / (kBUSCLK / 1000ULL);
}

static void save_log(void)
{
    int fd = open("io_bisect.log", O_WRONLY | O_CREAT | O_TRUNC, 0644);
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
    if (!quiet && screen_row < 25) {
        scr_setXY(0, screen_row++);
        scr_printf("%s", line);
    }
    printf("%s", line);
    if (log_len + (int)strlen(line) < LOG_CAP) {
        memcpy(log_buf + log_len, line, strlen(line));
        log_len += (int)strlen(line);
    }
}

/* the operation in progress, on the last line: visible if it never returns */
static void doing(const char *what)
{
    scr_setXY(0, 27);
    scr_printf("doing: %-60.60s", what);
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

/* fs_smoke_expect.txt: "file <path> <len> <crc>" / "chunk <path> <off> <len> <crc>" */
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
        out("fs_smoke_expect.txt missing: no CRC checks\n");
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
                regions[n_regions].len < 128 * 1024) /* small files only (not the ELF) */
                n_regions++;
        } else if (sscanf(line, "%*s %*s %ld %ld %lx", &regions[n_regions].off, &regions[n_regions].len,
                          &regions[n_regions].crc) == 3) {
            n_regions++;
        }
    }
}

/* one round of the game's start-up file operations */
static int io_round(const char *step, int round)
{
    static unsigned char buf[32768];
    unsigned long long t0 = now_us(), t_missing, t_mkdir, t_reads;
    int i, bad = 0, fd, mk, mk_errno, missing_ok;
    char what[96];

    doing("open of a file that does not exist (savedata/saves/qsave.vvv)");
    t_missing = now_us();
    fd = open("savedata/saves/qsave.vvv", O_RDONLY);
    missing_ok = fd < 0;
    if (fd >= 0)
        close(fd);
    t_missing = now_us() - t_missing;

    doing("mkdir of an existing directory (savedata)");
    t_mkdir = now_us();
    mk = mkdir("savedata", 0777);
    mk_errno = errno;
    t_mkdir = now_us() - t_mkdir;

    t_reads = now_us();
    for (i = 0; i < n_regions; i++) {
        unsigned long crc = 0;
        long left = regions[i].len, got = 0;
        snprintf(what, sizeof(what), "read %s @%ld (%ld B)", regions[i].path, regions[i].off, regions[i].len);
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
            out("  BAD %s @%ld: %ld of %ld B, crc %08lx want %08lx\n", regions[i].path, regions[i].off, got,
                regions[i].len, crc, regions[i].crc);
            quiet = was;
        }
    }
    t_reads = now_us() - t_reads;
    doing("writing io_bisect.log");
    out("%-9s #%d: missing-open %s %llu ms | mkdir rc %d errno %d %llu ms | %d/%d regions %s %llu ms | %llu ms\n",
        step, round, missing_ok ? "ok" : "OPENED?", t_missing / 1000, mk, mk_errno, t_mkdir / 1000, n_regions - bad, n_regions,
        bad ? "BAD" : "ok", t_reads / 1000, (now_us() - t0) / 1000);
    save_log();
    doing("idle");
    return bad;
}

/* ---- like SDL's audio thread: feed audsrv 512 frames at a time ---- */
static volatile unsigned long audio_submits;
static void audio_thread(void *arg)
{
    static short silence[512 * 2] __attribute__((aligned(64)));
    (void)arg;
    for (;;) {
        audsrv_wait_audio(sizeof(silence));
        audsrv_play_audio((char *)silence, sizeof(silence));
        audio_submits++;
    }
}

/* ---- like SDL's joystick polling: every opened pad, ~60 times a second ---- */
static char pad_bufs[8][256] __attribute__((aligned(64)));
static int pad_port[8], pad_slot[8], n_pads;
static volatile unsigned long pad_polls;
static void pad_thread(void *arg)
{
    struct padButtonStatus buttons;
    int i;
    (void)arg;
    for (;;) {
        for (i = 0; i < n_pads; i++)
            if (padGetState(pad_port[i], pad_slot[i]) == PAD_STATE_STABLE)
                padRead(pad_port[i], pad_slot[i], &buttons);
        pad_polls++;
        usleep(16000);
    }
}

static int start_thread(void (*fn)(void *), int prio, void *stack, int size)
{
    extern void *_gp;
    ee_thread_t t;
    int id;
    memset(&t, 0, sizeof(t));
    t.func = (void *)fn;
    t.stack = stack;
    t.stack_size = size;
    t.gp_reg = &_gp;
    t.initial_priority = prio;
    id = CreateThread(&t);
    if (id >= 0)
        StartThread(id, NULL);
    return id;
}

static void run_step(const char *step, int rounds)
{
    int r;
    for (r = 1; r <= rounds; r++)
        io_round(step, r);
}

int main(int argc, char *argv[])
{
    static unsigned char audio_stack[16 * 1024] __attribute__((aligned(16)));
    static unsigned char pad_stack[16 * 1024] __attribute__((aligned(16)));
    char cwd[256];
    struct audsrv_fmt_t fmt;
    int rc, port, slot;
    unsigned long long t0;

    SifInitRpc(0);
    while (!SifIopReset(NULL, 0)) {
    }
    while (!SifIopSync()) {
    }
    SifInitRpc(0);
    sbv_patch_enable_lmb();
    sbv_patch_disable_prefix_check();
    sbv_patch_fileio();
    init_ps2_filesystem_driver();

    init_scr();
    scr_setCursor(0);
    out("VVVVVV PS2 io_bisect: which IOP subsystem breaks USB file I/O\n");
    if (!getcwd(cwd, sizeof(cwd)))
        strcpy(cwd, "?");
    out("argv[0] %s  cwd %s\n", argc > 0 ? argv[0] : "?", cwd);
    doing("waiting for the device");
    out("device ready: %d\n", waitUntilDeviceIsReady(cwd));
    mkdir("savedata", 0777);
    mkdir("savedata/saves", 0777);
    load_expect();
    out("%d regions to check per round\n", n_regions);

    run_step("1 base", 2);

    doing("init_audio_driver (libsd + audsrv)");
    t0 = now_us();
    rc = init_audio_driver();
    out("init_audio_driver: %d (%llu ms)\n", rc, (now_us() - t0) / 1000);
    run_step("2 audio", 2);

    doing("audsrv_set_format 44100/16/2 and the feeding thread");
    fmt.bits = 16;
    fmt.freq = 44100;
    fmt.channels = 2;
    rc = audsrv_set_format(&fmt);
    audsrv_set_volume(MAX_VOLUME);
    out("audsrv_set_format: %d; feeding thread %d\n", rc, start_thread(audio_thread, 32, audio_stack, sizeof(audio_stack)));
    run_step("3 +play", 3);
    out("  audio submissions so far: %lu\n", audio_submits);

    doing("init_joystick_driver(true) (sio2man/padman/mtapman)");
    t0 = now_us();
    rc = init_joystick_driver(true);
    out("init_joystick_driver: %d (%llu ms)\n", rc, (now_us() - t0) / 1000);
    run_step("4 joy", 2);

    doing("mtapPortOpen + padPortOpen on 2 ports x 4 slots");
    for (port = 0; port < 2; port++)
        mtapPortOpen(port);
    for (slot = 0; slot < 4; slot++)
        for (port = 0; port < 2; port++)
            if (padPortOpen(port, slot, pad_bufs[n_pads]) > 0) {
                pad_port[n_pads] = port;
                pad_slot[n_pads] = slot;
                n_pads++;
            }
    out("pads opened: %d; polling thread %d\n", n_pads, start_thread(pad_thread, 40, pad_stack, sizeof(pad_stack)));
    run_step("5 +pads", 3);

    out("6: 20 more rounds with everything running (screen: problems only)\n");
    quiet = 1;
    run_step("6 soak", 20);
    quiet = 0;
    out("audio submissions %lu, pad polls %lu\n", audio_submits, pad_polls);
    out("DONE: no hang. Log: io_bisect.log\n");
    save_log();
    doing("finished - photograph the screen, then reset");
    SleepThread();
    return 0;
}
