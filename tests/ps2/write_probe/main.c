/* write_probe: why are the game's save files empty on a real PS2?
 *
 * With the EINTR fix (P3.4.2) the game runs from a USB stick, but every save
 * file is created with 0 bytes, with no error logged: PHYSFS_writeBytes
 * returned the full length and PHYSFS_close succeeded. The game's own log
 * file (open O_WRONLY|O_CREAT|O_TRUNC 0644, write, close) is written fine.
 * PhysicsFS differs: O_CLOEXEC, mode 0600, fcntl(F_GETFL) and fsync() before
 * close (physfs_platform_posix.c).
 *
 * Each variant writes 3000 bytes to its own file, first as a new file and
 * then over the existing one, and reads it back (stat size, read length, CRC).
 * One line per variant, written before the calls, so a hang shows too.
 * write_probe.log is written at the end.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <physfs.h>

#include <debug.h>
#include <iopcontrol.h>
#include <kernel.h>
#include <ps2_filesystem_driver.h>
#include <sbv_patches.h>
#include <sifrpc.h>

#define LEN 3000
static char log_buf[16384];
static int log_len, row;
static unsigned char data[LEN], back[LEN + 16];

static void say(const char *fmt, ...)
{
    char text[200];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text, sizeof(text), fmt, ap);
    va_end(ap);
    scr_printf("%s", text);
    if (log_len + (int)strlen(text) < (int)sizeof(log_buf)) {
        memcpy(log_buf + log_len, text, strlen(text));
        log_len += (int)strlen(text);
    }
}

static void new_line(void)
{
    if (log_len < (int)sizeof(log_buf))
        log_buf[log_len++] = '\n';
    scr_setXY(0, ++row);
}

/* read the file back: "size S read N ok|BAD" */
static int verify(const char *path)
{
    struct stat st;
    int fd, n = -1, ok;
    long size = stat(path, &st) == 0 ? (long)st.st_size : -1;
    fd = open(path, O_RDONLY);
    if (fd >= 0) {
        n = read(fd, back, sizeof(back));
        close(fd);
    }
    ok = size == LEN && n == LEN && memcmp(back, data, LEN) == 0;
    say("| size %ld read %d %s", size, n, ok ? "ok" : "BAD");
    return ok;
}

enum { W_CLOEXEC = 1, W_MODE600 = 2, W_FSYNC = 4 };

static int raw_variant(const char *tag, const char *path, int how, int pass)
{
    int flags = O_WRONLY | O_CREAT | O_TRUNC | ((how & W_CLOEXEC) ? O_CLOEXEC : 0);
    int fd, w, fl = 0, fs = 0, c;
    say("%s %s: ", tag, pass ? "over" : "new ");
    errno = 0;
    fd = open(path, flags, (how & W_MODE600) ? 0600 : 0644);
    if (fd < 0) {
        say("open -1 e%d ", errno);
        return 0;
    }
    w = write(fd, data, LEN);
    if (how & W_FSYNC) {
        fl = fcntl(fd, F_GETFL);
        errno = 0;
        fs = fsync(fd);
        say("w %d getfl %x fsync %d e%d ", w, fl, fs, fs < 0 ? errno : 0);
    } else {
        say("w %d ", w);
    }
    c = close(fd);
    say("close %d ", c);
    return verify(path);
}

static int physfs_variant(int pass)
{
    PHYSFS_File *f;
    PHYSFS_sint64 w = -1;
    int c = -1;
    say("G PhysicsFS %s: ", pass ? "over" : "new ");
    f = PHYSFS_openWrite("wp_g.txt");
    if (!f) {
        say("openWrite failed: %s ", PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
        return 0;
    }
    w = PHYSFS_writeBytes(f, data, LEN);
    c = PHYSFS_close(f);
    say("w %ld close %d ", (long)w, c);
    return 1;
}

int main(int argc, char *argv[])
{
    static const struct {
        const char *tag, *file;
        int how;
    } v[] = {
        {"A 0644          ", "wp_a.txt", 0},
        {"B 0600          ", "wp_b.txt", W_MODE600},
        {"C 0644 CLOEXEC  ", "wp_c.txt", W_CLOEXEC},
        {"D 0600 CLOEXEC  ", "wp_d.txt", W_CLOEXEC | W_MODE600},
        {"E 0644 fsync    ", "wp_e.txt", W_FSYNC},
        {"F like PhysicsFS", "wp_f.txt", W_CLOEXEC | W_MODE600 | W_FSYNC},
    };
    char cwd[256], dir[300], path[340];
    const char *sep;
    int i, pass, rc;

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
    if (!getcwd(cwd, sizeof(cwd)))
        strcpy(cwd, "?");
    say("write_probe %s ready %d", argc > 0 ? argv[0] : "?", waitUntilDeviceIsReady(cwd));
    new_line();
    sep = cwd[strlen(cwd) - 1] == '/' ? "" : "/";
    snprintf(dir, sizeof(dir), "%s%swrite_probe/", cwd, sep);
    mkdir(dir, 0777);
    say("files in %s", dir);
    new_line();

    for (pass = 0; pass < 2; pass++) {
        for (i = 0; i < LEN; i++)
            data[i] = (unsigned char)(i * 13 + pass * 101 + 7);
        for (i = 0; i < (int)(sizeof(v) / sizeof(v[0])); i++) {
            snprintf(path, sizeof(path), "%s%s", dir, v[i].file);
            raw_variant(v[i].tag, path, v[i].how, pass);
            new_line();
        }
        if (pass == 0) {
            rc = PHYSFS_init(argc > 0 ? argv[0] : NULL);
            rc = rc && PHYSFS_setWriteDir(dir) && PHYSFS_mount(dir, NULL, 0);
            say("PhysicsFS write dir: %s", rc ? "ok" : PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
            new_line();
        }
        if (physfs_variant(pass)) {
            snprintf(path, sizeof(path), "%swp_g.txt", dir);
            verify(path);
        }
        new_line();
    }

    say("DONE: no hang");
    new_line();
    {
        int fd = open("write_probe.log", O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0) {
            write(fd, log_buf, log_len);
            close(fd);
            say("log written: write_probe.log");
        }
    }
    SleepThread();
    return 0;
}
