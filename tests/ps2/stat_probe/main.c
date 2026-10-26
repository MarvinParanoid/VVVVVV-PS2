/* stat_probe: does stat() of a missing file hang USB I/O on a real PS2?
 *
 * sdl_bisect (2026-10-26, console) hung on its very first operation, before
 * SDL_Init: PHYSFS_openRead of a missing file. When the native open() fails,
 * PhysicsFS's dir archiver calls lstat() on the same path (physfs_archiver_dir.c
 * doOpen, "FIXME: why are we stating here?"); in ps2sdk lstat is stat, i.e. a
 * fileXio getstat on the IOP's FAT driver. io_bisect, which passed, never
 * stats a missing file.
 *
 * One operation per screen line, the line written before the call, so the
 * photo shows which call never returned. The reads between the stats tell a
 * hang in stat itself from a driver left locked after it. No SDL, no
 * PhysicsFS; stat_probe.log is written only at the end (or not at all).
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <debug.h>
#include <iopcontrol.h>
#include <kernel.h>
#include <ps2_filesystem_driver.h>
#include <sbv_patches.h>
#include <sifrpc.h>

static char log_buf[8192];
static int log_len, row;

static void line(const char *fmt, const char *a, int b, int c)
{
    char text[160];
    snprintf(text, sizeof(text), fmt, a, b, c);
    scr_setXY(0, row++);
    scr_printf("%s", text);
    if (log_len + (int)strlen(text) < (int)sizeof(log_buf)) {
        memcpy(log_buf + log_len, text, strlen(text));
        log_len += (int)strlen(text);
    }
}

static int step;

static void begin(const char *what, const char *path)
{
    char text[160];
    snprintf(text, sizeof(text), "%2d %s %s ... ", ++step, what, path);
    scr_setXY(0, row);
    scr_printf("%s", text);
    if (log_len + (int)strlen(text) < (int)sizeof(log_buf)) {
        memcpy(log_buf + log_len, text, strlen(text));
        log_len += (int)strlen(text);
    }
    row++;
}

static void result(int rc, int err)
{
    row--;
    line("%src %d errno %d\n", "", rc, err);
}

static void do_stat(const char *path)
{
    struct stat st;
    int rc;
    begin("stat", path);
    errno = 0;
    rc = stat(path, &st);
    result(rc, errno);
}

static void do_open(const char *path, int flags)
{
    int fd;
    begin(flags & O_CLOEXEC ? "open CLOEXEC" : "open", path);
    errno = 0;
    fd = open(path, flags, 0600);
    result(fd, errno);
    if (fd >= 0)
        close(fd);
}

static void do_read(const char *path)
{
    static char buf[4096];
    int fd, n = -1;
    begin("read 4 KB", path);
    errno = 0;
    fd = open(path, O_RDONLY);
    if (fd >= 0) {
        n = read(fd, buf, sizeof(buf));
        close(fd);
    }
    result(n, errno);
}

int main(int argc, char *argv[])
{
    char cwd[256], abs_missing[320];
    int fd;

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
    line("stat_probe: argv[0] %s  device ready %d\n", argc > 0 ? argv[0] : "?", waitUntilDeviceIsReady(cwd), 0);
    snprintf(abs_missing, sizeof(abs_missing), "%s%ssavedata/saves/qsave.vvv", cwd,
             cwd[strlen(cwd) - 1] == '/' ? "" : "/");

    do_read("fs_smoke_expect.txt");
    do_open("savedata/saves/qsave.vvv", O_RDONLY);
    do_open(abs_missing, O_RDONLY | O_CLOEXEC);
    do_read("fs_smoke_expect.txt");
    do_stat("data-ps2.zip");
    do_stat("savedata");
    do_read("fs_smoke_expect.txt");
    do_stat(abs_missing); /* what PhysicsFS does after the failed open */
    do_read("fs_smoke_expect.txt");
    do_stat("savedata/no_such_dir/x.txt");
    do_read("fs_smoke_expect.txt");
    do_stat(abs_missing);
    do_read("fs_smoke_expect.txt");

    line("DONE: no hang%s\n", "", 0, 0);
    fd = open("stat_probe.log", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        write(fd, log_buf, log_len);
        close(fd);
        line("log written: stat_probe.log%s\n", "", 0, 0);
    }
    SleepThread();
    return 0;
}
