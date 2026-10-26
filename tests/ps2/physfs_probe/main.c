/* physfs_probe: which file system call hangs under PhysicsFS on a real PS2?
 *
 * sdl_bisect (2026-10-26, console) hung on its first PHYSFS_openRead of a
 * missing file, before SDL_Init. stat_probe then showed that open() and stat()
 * of missing files work on their own. What PhysicsFS had done before that
 * call: kept data-ps2.zip open (and read its central directory through that
 * descriptor), fstat'ed it, enumerated directories that do not exist in
 * savedata/, mkdir, access.
 *
 * Every libc file call (from PhysicsFS and from this file) goes through a
 * --wrap shim that prints the call on screen before making it and the result
 * after, so a photo shows the call that never returned (the screen restarts
 * at the top when full). Part 1 repeats the suspect calls by hand with
 * data-ps2.zip held open; part 2 is sdl_bisect's PhysicsFS sequence.
 * physfs_probe.log is written only at the end.
 */
#include <dirent.h>
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

#define ROWS 26
static char log_buf[64 * 1024];
static int log_len, row, n_calls;

static void emit(const char *text, int new_line)
{
    if (new_line) {
        if (row >= ROWS) {
            scr_clear();
            row = 0;
        }
        scr_setXY(0, row++);
    }
    scr_printf("%s", text);
    if (log_len + (int)strlen(text) + 1 < (int)sizeof(log_buf)) {
        if (new_line && log_len)
            log_buf[log_len++] = '\n';
        memcpy(log_buf + log_len, text, strlen(text));
        log_len += (int)strlen(text);
    }
}

static void note(const char *fmt, ...)
{
    char text[128];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text, sizeof(text), fmt, ap);
    va_end(ap);
    emit(text, 1);
}

/* the last 34 characters of a path: enough to tell the calls apart */
static const char *tail(const char *path)
{
    size_t n = path ? strlen(path) : 0;
    return !path ? "(null)" : n > 34 ? path + n - 34 : path;
}

static void call_begin(const char *fmt, ...)
{
    char text[128];
    int k;
    va_list ap;
    k = snprintf(text, sizeof(text), "%4d ", ++n_calls);
    va_start(ap, fmt);
    vsnprintf(text + k, sizeof(text) - (size_t)k, fmt, ap);
    va_end(ap);
    emit(text, 1);
}

static void call_end(long rc, int err)
{
    char text[40];
    snprintf(text, sizeof(text), " = %ld e%d", rc, rc < 0 ? err : 0);
    emit(text, 0);
}

/* ---- --wrap shims ---- */
int __real_open(const char *path, int flags, ...);
int __real_close(int fd);
int __real_read(int fd, void *buf, size_t n);
int __real_write(int fd, const void *buf, size_t n);
off_t __real_lseek(int fd, off_t off, int whence);
int __real_stat(const char *path, struct stat *st);
int __real_lstat(const char *path, struct stat *st);
int __real_fstat(int fd, struct stat *st);
DIR *__real_opendir(const char *path);
struct dirent *__real_readdir(DIR *dir);
int __real_closedir(DIR *dir);
int __real_mkdir(const char *path, mode_t mode);
int __real_access(const char *path, int mode);
int __real_fcntl(int fd, int cmd, ...);
int __real_fsync(int fd);
int __real_remove(const char *path);

int __wrap_open(const char *path, int flags, ...)
{
    va_list ap;
    int mode, rc;
    va_start(ap, flags);
    mode = va_arg(ap, int);
    va_end(ap);
    call_begin("open %s %x", tail(path), flags);
    errno = 0;
    rc = __real_open(path, flags, mode);
    call_end(rc, errno);
    return rc;
}

int __wrap_close(int fd)
{
    int rc;
    call_begin("close %d", fd);
    rc = __real_close(fd);
    call_end(rc, errno);
    return rc;
}

int __wrap_read(int fd, void *buf, size_t n)
{
    int rc;
    call_begin("read %d %u", fd, (unsigned)n);
    errno = 0;
    rc = __real_read(fd, buf, n);
    call_end(rc, errno);
    return rc;
}

int __wrap_write(int fd, const void *buf, size_t n)
{
    int rc;
    call_begin("write %d %u", fd, (unsigned)n);
    errno = 0;
    rc = __real_write(fd, buf, n);
    call_end(rc, errno);
    return rc;
}

off_t __wrap_lseek(int fd, off_t off, int whence)
{
    off_t rc;
    call_begin("lseek %d %ld %d", fd, (long)off, whence);
    errno = 0;
    rc = __real_lseek(fd, off, whence);
    call_end((long)rc, errno);
    return rc;
}

int __wrap_stat(const char *path, struct stat *st)
{
    int rc;
    call_begin("stat %s", tail(path));
    errno = 0;
    rc = __real_stat(path, st);
    call_end(rc, errno);
    return rc;
}

int __wrap_lstat(const char *path, struct stat *st)
{
    int rc;
    call_begin("lstat %s", tail(path));
    errno = 0;
    rc = __real_lstat(path, st);
    call_end(rc, errno);
    return rc;
}

int __wrap_fstat(int fd, struct stat *st)
{
    int rc;
    call_begin("fstat %d", fd);
    errno = 0;
    rc = __real_fstat(fd, st);
    call_end(rc == 0 ? (long)st->st_size : rc, errno);
    return rc;
}

DIR *__wrap_opendir(const char *path)
{
    DIR *d;
    call_begin("opendir %s", tail(path));
    errno = 0;
    d = __real_opendir(path);
    call_end(d ? 1 : -1, errno);
    return d;
}

struct dirent *__wrap_readdir(DIR *dir)
{
    struct dirent *e;
    call_begin("readdir");
    e = __real_readdir(dir);
    if (e)
        emit(" ", 0), emit(e->d_name, 0);
    else
        call_end(0, 0);
    return e;
}

int __wrap_closedir(DIR *dir)
{
    int rc;
    call_begin("closedir");
    rc = __real_closedir(dir);
    call_end(rc, errno);
    return rc;
}

int __wrap_mkdir(const char *path, mode_t mode)
{
    int rc;
    call_begin("mkdir %s", tail(path));
    errno = 0;
    rc = __real_mkdir(path, mode);
    call_end(rc, errno);
    return rc;
}

int __wrap_access(const char *path, int mode)
{
    int rc;
    call_begin("access %s %d", tail(path), mode);
    errno = 0;
    rc = __real_access(path, mode);
    call_end(rc, errno);
    return rc;
}

int __wrap_fcntl(int fd, int cmd, ...)
{
    va_list ap;
    int arg, rc;
    va_start(ap, cmd);
    arg = va_arg(ap, int);
    va_end(ap);
    call_begin("fcntl %d %d %d", fd, cmd, arg);
    errno = 0;
    rc = __real_fcntl(fd, cmd, arg);
    call_end(rc, errno);
    return rc;
}

int __wrap_fsync(int fd)
{
    int rc;
    call_begin("fsync %d", fd);
    errno = 0;
    rc = __real_fsync(fd);
    call_end(rc, errno);
    return rc;
}

int __wrap_remove(const char *path)
{
    int rc;
    call_begin("remove %s", tail(path));
    errno = 0;
    rc = __real_remove(path);
    call_end(rc, errno);
    return rc;
}

/* ---- part 1: the suspects by hand, with data-ps2.zip held open ---- */
static void manual(const char *cwd)
{
    static char buf[4096];
    char missing[320], missing_dir[320];
    struct stat st;
    const char *sep = cwd[strlen(cwd) - 1] == '/' ? "" : "/";
    int zip, fd;
    DIR *d;

    snprintf(missing, sizeof(missing), "%s%ssavedata/saves/qsave.vvv", cwd, sep);
    snprintf(missing_dir, sizeof(missing_dir), "%s%ssavedata/graphics/", cwd, sep);
    note("-- part 1: data-ps2.zip held open --");
    zip = open("data-ps2.zip", O_RDONLY);
    fstat(zip, &st);
    lseek(zip, -4096, SEEK_END);
    read(zip, buf, sizeof(buf));
    open(missing, O_RDONLY);
    read(zip, buf, 512);
    stat(missing, &st);
    lseek(zip, 0, SEEK_SET);
    read(zip, buf, 512);
    d = opendir(missing_dir);
    if (d)
        closedir(d);
    open(missing, O_RDONLY | O_CLOEXEC, 0600);
    lstat(missing, &st);
    fd = open("fs_smoke_expect.txt", O_RDONLY);
    read(fd, buf, sizeof(buf));
    close(fd);
    read(zip, buf, 512);
    close(zip);
    note("-- part 1 done --");
}

int main(int argc, char *argv[])
{
    char cwd[256], path[320];
    const char *sep;
    PHYSFS_File *f;
    char **list;
    int rc;

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
    note("physfs_probe %s ready %d", argc > 0 ? tail(argv[0]) : "?", waitUntilDeviceIsReady(cwd));

    manual(cwd);

    /* ---- part 2: sdl_bisect's PhysicsFS sequence ---- */
    note("-- part 2: PhysicsFS --");
    sep = cwd[strlen(cwd) - 1] == '/' ? "" : "/";
    note("PHYSFS_init");
    rc = PHYSFS_init(argc > 0 ? argv[0] : NULL);
    PHYSFS_permitSymbolicLinks(1);
    snprintf(path, sizeof(path), "%s%ssavedata/", cwd, sep);
    mkdir(path, 0777);
    note("PHYSFS_mount savedata");
    rc = rc && PHYSFS_mount(path, NULL, 0);
    note("PHYSFS_setWriteDir");
    rc = rc && PHYSFS_setWriteDir(path);
    snprintf(path, sizeof(path), "%s%ssavedata/saves/", cwd, sep);
    mkdir(path, 0777);
    snprintf(path, sizeof(path), "%s%sdata-ps2.zip", cwd, sep);
    note("PHYSFS_mount zip");
    rc = rc && PHYSFS_mount(path, NULL, 1);
    note("mounts %s", rc ? "ok" : PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
    note("PHYSFS_enumerateFiles graphics");
    list = PHYSFS_enumerateFiles("graphics");
    PHYSFS_freeList(list);
    note("PHYSFS_enumerateFiles sounds");
    list = PHYSFS_enumerateFiles("sounds");
    PHYSFS_freeList(list);
    note("PHYSFS_openRead saves/qsave.vvv (missing)");
    f = PHYSFS_openRead("saves/qsave.vvv");
    if (f)
        PHYSFS_close(f);
    note("PHYSFS_openRead graphics/entcolours.png");
    f = PHYSFS_openRead("graphics/entcolours.png");
    if (f) {
        static char buf[4096];
        note("PHYSFS_readBytes %ld", (long)PHYSFS_readBytes(f, buf, sizeof(buf)));
        PHYSFS_close(f);
    }
    note("PHYSFS_openRead saves/qsave.vvv (missing) again");
    f = PHYSFS_openRead("saves/qsave.vvv");
    if (f)
        PHYSFS_close(f);

    note("DONE: no hang (%d calls)", n_calls);
    {
        int fd = __real_open("physfs_probe.log", O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0) {
            __real_write(fd, log_buf, (size_t)log_len);
            __real_close(fd);
            note("log written: physfs_probe.log");
        }
    }
    SleepThread();
    return 0;
}
