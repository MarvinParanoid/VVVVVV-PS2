/* fs_smoke: first-boot filesystem check for the VVVVVV PS2 USB package.
 *
 * Standalone (no SDL, no game code). Start it from wLaunchELF in the same
 * folder as VVVVVV.elf and data-ps2.zip. It brings the IOP up the way SDL's
 * PS2 main() does (IOP reset, then the ps2_drivers USB/memory card/HDD
 * modules), then reports on screen and in fs_smoke.log next to itself:
 *   - argv[0], the working directory and how long the device took to appear
 *   - the folder listing and whether the files the game needs are present
 *   - data-ps2.zip: size, music blob stored (not deflated), sequential and
 *     random read speed (the game streams music from it at ~25 KB/s)
 *   - savedata/: directory creation, write/read-back/delete of a test file,
 *     and the time a 96 KB rewrite takes (the size of the game's log file)
 *   - fs_smoke_expect.txt (written by make-usb-package.sh): CRC32 of whole
 *     files (incl. VVVVVV.elf) and of 24 fixed chunks across data-ps2.zip,
 *     re-read from the stick with seeks, so a bad copy or a flaky stick or
 *     driver shows up as a CRC mismatch
 * The program then waits forever so the screen can be read or photographed.
 */
#include <dirent.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <debug.h>
#include <malloc.h>
#include <screenshot.h>
#include <iopcontrol.h>
#include <kernel.h>
#include <ps2_filesystem_driver.h>
#include <sbv_patches.h>
#include <sifrpc.h>
#include <timer.h>

#define LOG_CAP (32 * 1024)
static char log_buf[LOG_CAP];
static int log_len;
static int failures;
/* names of failed checks, repeated at the end: the 40-line text screen wraps */
static char failed_names[512];

static unsigned long long now_us(void)
{
    return GetTimerSystemTime() * 1000ULL / (kBUSCLK / 1000ULL);
}

static void out(const char *fmt, ...)
{
    char line[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    scr_printf("%s", line);
    printf("%s", line);
    if (log_len + (int)strlen(line) < LOG_CAP) {
        memcpy(log_buf + log_len, line, strlen(line));
        log_len += (int)strlen(line);
    }
}

static void check(int ok, const char *what)
{
    out("  [%s] %s\n", ok ? " OK " : "FAIL", what);
    if (!ok && strlen(failed_names) + strlen(what) + 4 < sizeof(failed_names)) {
        strcat(failed_names, "  - ");
        strcat(failed_names, what);
        strcat(failed_names, "\n");
    }
    failures += !ok;
}

static void save_log(void)
{
    int fd = open("fs_smoke.log", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        write(fd, log_buf, log_len);
        close(fd);
    }
}

static long file_size(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 ? (long)st.st_size : -1;
}

/* Find the compression method of a zip entry from the central directory. */
static int zip_method(int fd, long size, const char *entry)
{
    static unsigned char tail[66 * 1024];
    long start = size > (long)sizeof(tail) ? size - (long)sizeof(tail) : 0;
    int n, i, method = -1;
    unsigned long cd_off, cd_size;
    unsigned char *cd;
    if (lseek(fd, start, SEEK_SET) < 0 || (n = read(fd, tail, size - start)) < 22)
        return -2;
    for (i = n - 22; i >= 0; i--)
        if (tail[i] == 'P' && tail[i + 1] == 'K' && tail[i + 2] == 5 && tail[i + 3] == 6)
            break;
    if (i < 0)
        return -3;
    cd_size = tail[i + 12] | tail[i + 13] << 8 | tail[i + 14] << 16 | (unsigned long)tail[i + 15] << 24;
    cd_off = tail[i + 16] | tail[i + 17] << 8 | tail[i + 18] << 16 | (unsigned long)tail[i + 19] << 24;
    cd = malloc(cd_size);
    if (!cd || lseek(fd, (long)cd_off, SEEK_SET) < 0 || read(fd, cd, cd_size) != (int)cd_size) {
        free(cd);
        return -4;
    }
    for (i = 0; i + 46 <= (int)cd_size;) {
        int name_len = cd[i + 28] | cd[i + 29] << 8;
        int extra = cd[i + 30] | cd[i + 31] << 8, comment = cd[i + 32] | cd[i + 33] << 8;
        if (name_len == (int)strlen(entry) && memcmp(cd + i + 46, entry, name_len) == 0) {
            method = cd[i + 10] | cd[i + 11] << 8;
            break;
        }
        i += 46 + name_len + extra + comment;
    }
    free(cd);
    return method;
}

static void test_zip(const char *path)
{
    static unsigned char buf[32768];
    long size = file_size(path);
    int fd, i, method;
    unsigned long long t0, total = 0, worst = 0;
    long done;
    out("\n%s: %ld bytes\n", path, size);
    if (size <= 0) {
        check(0, "data-ps2.zip present (make it with scripts/make-usb-package.sh)");
        return;
    }
    fd = open(path, O_RDONLY);
    check(fd >= 0, "open");
    if (fd < 0)
        return;
    method = zip_method(fd, size, "vvvvvvmusic.vvv");
    out("  vvvvvvmusic.vvv zip method %d (0 = stored)\n", method);
    check(method == 0, "music blob stored, so it can be streamed");

    lseek(fd, 0, SEEK_SET);
    t0 = now_us();
    for (done = 0; done < 4L * 1024 * 1024 && done < size; done += (long)sizeof(buf))
        if (read(fd, buf, sizeof(buf)) != (int)sizeof(buf))
            break;
    t0 = now_us() - t0;
    out("  sequential: %ld KB in %llu ms = %llu KB/s\n", done / 1024, t0 / 1000,
        t0 ? (unsigned long long)done * 1000000ULL / t0 / 1024 : 0);
    check(done >= 1024 * 1024 && t0 < 20000000ULL, "sequential read");

    srand(1);
    for (i = 0; i < 24; i++) {
        long off = (long)(((unsigned long long)rand() * 65536ULL + (unsigned)rand()) % (unsigned long long)(size - (long)sizeof(buf)));
        unsigned long long t = now_us();
        lseek(fd, off, SEEK_SET);
        read(fd, buf, sizeof(buf));
        t = now_us() - t;
        total += t;
        if (t > worst)
            worst = t;
    }
    out("  random 32 KB reads: avg %llu ms, worst %llu ms\n", total / 24 / 1000, worst / 1000);
    check(worst < 1000000ULL, "random read under 1 s (music ring holds 1 s)");
    close(fd);
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

/* Re-read what make-usb-package.sh recorded and compare CRC32s. */
static void test_expect(void)
{
    static unsigned char buf[32768];
    static char text[8192];
    int fd = open("fs_smoke_expect.txt", O_RDONLY), n, bad = 0, checked = 0;
    char *line, *save = NULL, unreadable[96] = "";
    unsigned long long t0 = now_us();
    out("\nchecksums (fs_smoke_expect.txt)\n");
    if (fd < 0) {
        check(0, "fs_smoke_expect.txt present (re-run make-usb-package.sh)");
        return;
    }
    n = read(fd, text, sizeof(text) - 1);
    close(fd);
    text[n > 0 ? n : 0] = '\0';
    for (line = strtok_r(text, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        char kind[8], path[96];
        long off = 0, len = 0;
        unsigned long want, crc = 0;
        int f, got = 0;
        if (line[0] == '#')
            continue;
        if (sscanf(line, "%7s %95s", kind, path) != 2)
            continue;
        if (strcmp(kind, "file") == 0) {
            if (sscanf(line, "%*s %*s %ld %lx", &len, &want) != 2)
                continue;
        } else if (sscanf(line, "%*s %*s %ld %ld %lx", &off, &len, &want) != 3) {
            continue;
        }
        if (strcmp(path, unreadable) == 0) {
            bad++;
            checked++;
            continue;
        }
        f = open(path, O_RDONLY);
        if (f < 0) {
            out("  cannot open %s\n", path);
            strcpy(unreadable, path);
            bad++;
            checked++;
            continue;
        }
        if (lseek(f, off, SEEK_SET) == off) {
            long left = len;
            while (left > 0) {
                int r = read(f, buf, left < (long)sizeof(buf) ? (int)left : (int)sizeof(buf));
                if (r <= 0)
                    break;
                crc = crc32_update(crc, buf, (size_t)r);
                left -= r;
                got += r;
            }
        }
        close(f);
        checked++;
        if (got != len || crc != want) {
            bad++;
            out("  MISMATCH %s @%ld: read %d of %ld bytes, crc %08lx, want %08lx\n", path, off, got, len, crc, want);
        }
    }
    out("  %d regions checked in %llu ms\n", checked, (now_us() - t0) / 1000);
    check(checked > 0 && bad == 0, "all files and data-ps2.zip chunks read back with the right CRC32");
}

static void test_savedata(void)
{
    static unsigned char buf[96 * 1024], back[96 * 1024];
    unsigned long long t;
    int fd, i, ok;
    out("\nsavedata/\n");
    ok = mkdir("savedata", 0777) == 0;
    out("  mkdir savedata: %s\n", ok ? "created" : "failed (exists already?)");
    for (i = 0; i < (int)sizeof(buf); i++)
        buf[i] = (unsigned char)(i * 7 + 3);
    t = now_us();
    fd = open("savedata/fs_smoke.tmp", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    ok = fd >= 0 && write(fd, buf, sizeof(buf)) == (int)sizeof(buf);
    if (fd >= 0)
        close(fd);
    t = now_us() - t;
    out("  96 KB create+write+close: %llu ms\n", t / 1000);
    check(ok, "write test file in savedata/");
    fd = open("savedata/fs_smoke.tmp", O_RDONLY);
    ok = fd >= 0 && read(fd, back, sizeof(back)) == (int)sizeof(back) && memcmp(buf, back, sizeof(buf)) == 0;
    if (fd >= 0)
        close(fd);
    check(ok, "read back identical");
    t = now_us();
    fd = open("savedata/fs_smoke.tmp", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        write(fd, buf, 1024);
        close(fd);
    }
    out("  1 KB rewrite: %llu ms\n", (now_us() - t) / 1000);
    {
        /* judge by the result: PCSX2's host: returns non-zero although it deletes */
        struct stat st;
        int rc = remove("savedata/fs_smoke.tmp");
        out("  remove() returned %d\n", rc);
        check(stat("savedata/fs_smoke.tmp", &st) != 0, "delete test file (gone afterwards)");
    }
}

/* PCSX2 (host:) only: save the text screen so automated runs can check what
 * is shown; read in bands (a single CT32 read returned only the top lines). */
static void save_screen_for_tests(void)
{
    static unsigned char row[640 * 3];
    unsigned char *b = memalign(64, 640 * 448 * 4);
    int fd, x, y;
    if (!b)
        return;
    for (y = 0; y < 448; y += 32)
        ps2_screenshot(b + y * 640 * 4, 0, 0, y, 640, 32, 0);
    fd = open("shot_fs_smoke.ppm", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        write(fd, "P6\n640 448\n255\n", 15);
        for (y = 0; y < 448; y++) {
            for (x = 0; x < 640; x++)
                memcpy(row + x * 3, b + (y * 640 + x) * 4, 3);
            write(fd, row, sizeof(row));
        }
        close(fd);
    }
    free(b);
}

int main(int argc, char *argv[])
{
    char cwd[256];
    unsigned long long t0;
    DIR *d;
    struct dirent *e;
    static const char *needed[] = {"VVVVVV.elf", "data-ps2.zip", "lang", "fonts"};
    int i;

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
    out("VVVVVV PS2 filesystem smoke test\n");
    out("argv[0] = %s\n", argc > 0 ? argv[0] : "(none)");
    if (!getcwd(cwd, sizeof(cwd)))
        strcpy(cwd, "?");
    out("cwd     = %s\n", cwd);
    t0 = now_us();
    check(waitUntilDeviceIsReady(cwd), "device ready");
    out("  device ready after %llu ms\n", (now_us() - t0) / 1000);

    out("\nlisting:\n");
    d = opendir(".");
    check(d != NULL, "opendir(\".\")");
    if (d) {
        for (i = 0; (e = readdir(d)) != NULL; i++)
            if (i < 8)
                out("  %s\n", e->d_name);
        if (i > 8)
            out("  ... %d entries in total\n", i);
        closedir(d);
    }
    for (i = 0; i < (int)(sizeof(needed) / sizeof(needed[0])); i++) {
        struct stat st;
        char what[80];
        snprintf(what, sizeof(what), "%s present", needed[i]);
        check(stat(needed[i], &st) == 0, what);
    }

    test_zip("data-ps2.zip");
    test_expect();
    test_savedata();

    out("\n%s: %d check(s) failed. Log: %sfs_smoke.log\n", failures ? "PROBLEMS" : "ALL OK", failures, cwd);
    if (failures)
        out("failed:\n%s", failed_names);
    save_log();
    if (strncmp(cwd, "host", 4) == 0)
        save_screen_for_tests();
    SleepThread();
    return 0;
}
