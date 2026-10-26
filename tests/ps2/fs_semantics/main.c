/* fs_semantics: host regression test for the PS2 file system fixes.
 *
 * The first console sessions (2026-10-26) found that ps2sdk on a USB stick
 * (mass:) reports errors differently from POSIX, and that PCSX2's host:
 * cannot show it. Traced in ps2sdk (329e2bb): bdmfs_fatfs returns -FRESULT,
 * libcglue turns that into errno; the fileXio stat helper sets ENOENT and
 * returns -1, which libcglue's _stat turns into errno 1; fsync is ENOSYS.
 *
 * This program builds the real PhysicsFS from the patched VVVVVV tree as the
 * PS2 platform (-D__ps2sdk__) and wraps the libc calls (-Wl,--wrap) so they
 * fail the way the console does:
 *   open of a missing path  -> errno 4 (FR_NO_FILE, == EINTR)
 *   stat/lstat failure      -> errno 1 (EPERM)
 *   mkdir of an existing dir-> errno 8 (FR_EXIST)
 *   fsync                   -> -1, ENOSYS
 * and checks what the game relies on:
 *   1. opening a missing file returns (no EINTR retry loop) - fix 1;
 *   2. a save written through PhysicsFS is closed and has its data - fix 2;
 *   3. a directory in a later mount (lang/en behind the write dir) is found,
 *      and the language list contains it - fix 3;
 *   4. missing files are "not found", existing ones are found;
 *   5. the save directory tree is created, and an existing directory is not
 *      an error (PS2Mkdir.h), while a file in the way still is - fix 4.
 * Results: fs_semantics.log in the current directory ("RESULT" line last).
 */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <physfs.h>

#include "PS2Mkdir.h"

static FILE *log_file;
static int passed, failed;
static char root[256];

static void say(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    va_end(ap);
    va_start(ap, fmt);
    vfprintf(log_file, fmt, ap);
    va_end(ap);
    fflush(log_file);
}

static void check(int ok, const char *what)
{
    say("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (ok)
        passed++;
    else
        failed++;
}

/* ---- ps2sdk mass: error emulation, only for paths under the test root ---- */
static int emulate;
static int open_calls_missing;

static int ours(const char *path)
{
    return emulate && path && strncmp(path, root, strlen(root)) == 0;
}

int __real_open(const char *path, int flags, ...);
int __real_stat(const char *path, struct stat *st);
int __real_lstat(const char *path, struct stat *st);
int __real_mkdir(const char *path, mode_t mode);
int __real_fsync(int fd);

int __wrap_open(const char *path, int flags, ...)
{
    va_list ap;
    int mode, fd;
    va_start(ap, flags);
    mode = va_arg(ap, int);
    va_end(ap);
    fd = __real_open(path, flags, mode);
    if (fd < 0 && ours(path) && (errno == ENOENT || errno == ENOTDIR)) {
        open_calls_missing++;
        errno = 4; /* -FR_NO_FILE through libcglue: reads as EINTR */
    }
    return fd;
}

int __wrap_stat(const char *path, struct stat *st)
{
    int rc = __real_stat(path, st);
    if (rc < 0 && ours(path))
        errno = 1; /* fileXio helper -1, then _stat's __transform_errno(-1) */
    return rc;
}

int __wrap_lstat(const char *path, struct stat *st)
{
    int rc = __real_lstat(path, st);
    if (rc < 0 && ours(path))
        errno = 1;
    return rc;
}

int __wrap_mkdir(const char *path, mode_t mode)
{
    int rc = __real_mkdir(path, mode);
    if (rc < 0 && ours(path) && errno == EEXIST)
        errno = 8; /* -FR_EXIST */
    return rc;
}

int __wrap_fsync(int fd)
{
    if (emulate) {
        errno = ENOSYS; /* libcglue: "not supported" */
        return -1;
    }
    return __real_fsync(fd);
}

static void on_alarm(int sig)
{
    (void)sig;
    say("FAIL timed out: a file call never returned (EINTR retry loop?)\n");
    say("RESULT fs_semantics: %d passed, %d failed + timeout\n", passed, failed + 1);
    _exit(1);
}

static void make_file(const char *rel, const char *text)
{
    char path[512];
    FILE *f;
    snprintf(path, sizeof(path), "%s/%s", root, rel);
    f = fopen(path, "w");
    if (f) {
        fputs(text, f);
        fclose(f);
    }
}

int main(int argc, char **argv)
{
    char path[512], save_dir[512], lang_dir[512];
    char **list, **i;
    PHYSFS_File *f;
    PHYSFS_Stat st;
    struct stat real;
    static char data[3000];
    int found_en, rc, k;

    log_file = fopen("fs_semantics.log", "w");
    if (!log_file)
        return 2;
    snprintf(root, sizeof(root), "/tmp/fs_semantics_%d", (int)getpid());
    snprintf(path, sizeof(path), "rm -rf '%s'", root);
    if (system(path) != 0)
        return 2;
    __real_mkdir(root, 0777);
    snprintf(lang_dir, sizeof(lang_dir), "%s/lang", root);
    __real_mkdir(lang_dir, 0777);
    snprintf(path, sizeof(path), "%s/lang/en", root);
    __real_mkdir(path, 0777);
    make_file("lang/en/meta.xml", "<meta/>\n");
    make_file("lang/README.txt", "not a language\n");
    snprintf(save_dir, sizeof(save_dir), "%s/savedata", root);
    signal(SIGALRM, on_alarm);
    alarm(20);
    emulate = 1;

    /* 5. save directory creation, as FILESYSTEM_init does it */
    check(ps2_mkdir_existing_ok(save_dir, 0777) == 0, "mkdir savedata: created");
    errno = 0;
    rc = ps2_mkdir_existing_ok(save_dir, 0777);
    check(rc == 1, "mkdir savedata again: existing directory is success (errno 8 underneath)");
    snprintf(path, sizeof(path), "%s/saves", save_dir);
    check(ps2_mkdir_existing_ok(path, 0777) == 0, "mkdir savedata/saves: created");
    check(ps2_mkdir_existing_ok(path, 0777) == 1, "mkdir savedata/saves again: success");
    make_file("savedata/afile", "x");
    snprintf(path, sizeof(path), "%s/afile", save_dir);
    errno = 0;
    rc = ps2_mkdir_existing_ok(path, 0777);
    check(rc == -1 && errno == 8, "mkdir over a file: still a failure, errno kept");
    snprintf(path, sizeof(path), "%s/no/such/parent", root);
    rc = ps2_mkdir_existing_ok(path, 0777);
    check(rc == -1, "mkdir with a missing parent: failure");

    /* the game's mounts: write dir first, then lang/ at "lang" */
    check(PHYSFS_init(argc > 0 ? argv[0] : NULL) != 0, "PHYSFS_init");
    snprintf(path, sizeof(path), "%s/", save_dir);
    check(PHYSFS_mount(path, NULL, 0) && PHYSFS_setWriteDir(path), "mount + write dir savedata/");
    check(PHYSFS_mount(lang_dir, "lang", 1) != 0, "mount lang/ at \"lang\"");

    /* 1. missing file: returns, one open per directory mount, no retries */
    open_calls_missing = 0;
    f = PHYSFS_openRead("saves/qsave.vvv");
    check(f == NULL, "openRead of a missing save returns NULL");
    say("     (%d failed open() calls, errno 4 each)\n", open_calls_missing);
    check(open_calls_missing >= 1 && open_calls_missing <= 2, "missing file: no open() retry loop");
    if (f)
        PHYSFS_close(f);

    /* 4. exists / not found */
    check(!PHYSFS_exists("saves/qsave.vvv"), "exists(missing save) is false");
    check(PHYSFS_exists("lang/en/meta.xml"), "exists(lang/en/meta.xml) in the later mount");
    memset(&st, 0, sizeof(st));
    check(!PHYSFS_stat("levels/nothing.vvvvvv", &st) && PHYSFS_getLastErrorCode() == PHYSFS_ERR_NOT_FOUND,
          "stat of a missing path reports NOT_FOUND, not PERMISSION");

    /* 3. lang/en behind the write dir; FILESYSTEM_enumerateLanguageCodes */
    memset(&st, 0, sizeof(st));
    check(PHYSFS_stat("lang/en", &st) && st.filetype == PHYSFS_FILETYPE_DIRECTORY, "stat(lang/en) is a directory");
    found_en = 0;
    list = PHYSFS_enumerateFiles("lang");
    for (i = list; i && *i; i++) {
        char full[128];
        snprintf(full, sizeof(full), "lang/%s", *i);
        if (PHYSFS_stat(full, &st) && st.filetype == PHYSFS_FILETYPE_DIRECTORY && (*i)[0] != '.')
            found_en += strcmp(*i, "en") == 0;
    }
    PHYSFS_freeList(list);
    check(found_en == 1, "language list contains en");

    /* 2. a save through PhysicsFS: written, closed, data on disk */
    for (k = 0; k < (int)sizeof(data); k++)
        data[k] = (char)('a' + k % 26);
    f = PHYSFS_openWrite("saves/qsave.vvv");
    check(f != NULL, "openWrite saves/qsave.vvv");
    if (f) {
        check(PHYSFS_writeBytes(f, data, sizeof(data)) == (PHYSFS_sint64)sizeof(data), "writeBytes 3000");
        check(PHYSFS_close(f) != 0, "PHYSFS_close succeeds although fsync is ENOSYS");
    }
    snprintf(path, sizeof(path), "%s/saves/qsave.vvv", save_dir);
    check(__real_stat(path, &real) == 0 && real.st_size == (off_t)sizeof(data), "save file on disk has 3000 bytes");
    f = PHYSFS_openRead("saves/qsave.vvv");
    check(f != NULL && PHYSFS_fileLength(f) == (PHYSFS_sint64)sizeof(data), "save reads back through PhysicsFS");
    if (f)
        PHYSFS_close(f);

    PHYSFS_deinit();
    emulate = 0;
    alarm(0);
    snprintf(path, sizeof(path), "rm -rf '%s'", root);
    if (system(path) != 0)
        say("note: could not remove %s\n", root);
    say("RESULT fs_semantics: %d passed, %d failed\n", passed, failed);
    fclose(log_file);
    return failed ? 1 : 0;
}
