#include "harness.h"

#include <malloc.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#ifdef __ps2sdk__
#include <kernel.h>
#include <timer.h>
#else
#include <time.h>
#endif

/* The whole log is kept in RAM and the file is rewritten (O_TRUNC) after
 * every line. Under Play!'s host: HLE, data only reached the host file in
 * 4096-byte chunks or on close, and O_APPEND / lseek(SEEK_END) were not
 * honoured. Rewriting is O(n^2) but logs are a few KiB, and a test that hangs
 * still leaves everything up to the hang on disk. */
#define LOGBUF_SIZE (64 * 1024)
static char logbuf[LOGBUF_SIZE];
static int loglen;
static char logpath[256];
static int log_ok;
static int n_pass, n_fail;
static const char *test_name;

static void vlog(const char *fmt, va_list ap)
{
    char line[512];
    int n = vsnprintf(line, sizeof(line) - 1, fmt, ap);
    if (n < 0)
        return;
    if (n > (int)sizeof(line) - 2)
        n = (int)sizeof(line) - 2;
    line[n++] = '\n';
    line[n] = '\0';
    fputs(line, stdout);
    fflush(stdout);
    if (loglen + n <= LOGBUF_SIZE)
    {
        memcpy(logbuf + loglen, line, n);
        loglen += n;
    }
    if (log_ok)
    {
        int fd = open(logpath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0)
        {
            write(fd, logbuf, loglen);
            close(fd);
        }
    }
}

void th_log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vlog(fmt, ap);
    va_end(ap);
}

void th_init(const char *name, int argc, char **argv)
{
    char cwd[256];
    int fd;
    test_name = name;
    snprintf(logpath, sizeof(logpath), "%s.log", name);
    fd = open(logpath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0)
    {
        close(fd);
        log_ok = 1;
    }
    th_log("=== %s ===", name);
    th_log("argc=%d argv0=%s", argc, argc > 0 ? argv[0] : "(none)");
    th_log("cwd=%s", getcwd(cwd, sizeof(cwd)) ? cwd : "(getcwd failed)");
    th_log("logfile=%s (%s)", logpath, log_ok ? "ok" : "FAILED, stdout only");
#ifdef __ps2sdk__
    th_log("platform=PS2 EE");
#else
    th_log("platform=host");
#endif
}

uint64_t th_now_us(void)
{
#ifdef __ps2sdk__
    /* BUSCLK ticks -> microseconds; kBUSCLK = 147456000 */
    return GetTimerSystemTime() * 1000ULL / (kBUSCLK / 1000ULL);
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)(ts.tv_nsec / 1000);
#endif
}

extern char end[];

/* in_use: bytes handed out by malloc. headroom: what the heap can still grow
 * to, i.e. EndOfHeap() - sbrk(0) plus free space inside the arena.
 * (An earlier malloc() binary search was dropped: under Play! sbrk is bounded
 * by an EndOfHeap() beyond 32 MB, so it reported 64 MB "free".) */
void th_mem_report(const char *label)
{
    struct mallinfo mi = mallinfo();
    char *brk = (char *)sbrk(0);
#ifdef __ps2sdk__
    char *heap_end = (char *)EndOfHeap();
    unsigned long headroom = (unsigned long)(heap_end - brk) + (unsigned long)mi.fordblks;
    th_log("MEM %-24s in_use=%lu arena=%lu brk=%p end=%p EndOfHeap=%p GetMemorySize=%ld headroom=%lu",
           label, (unsigned long)mi.uordblks, (unsigned long)mi.arena, (void *)brk, (void *)end,
           (void *)heap_end, (long)GetMemorySize(), headroom);
#else
    th_log("MEM %-24s in_use=%lu arena=%lu", label, (unsigned long)mi.uordblks, (unsigned long)mi.arena);
#endif
}

void th_set_thread_priority(int prio)
{
#ifdef __ps2sdk__
    int id = GetThreadId();
    int old = ChangeThreadPriority(id, prio);
    th_log("thread %d priority -> %d (ChangeThreadPriority returned %d)", id, prio, old);
#else
    th_log("thread priority change to %d skipped on host", prio);
#endif
}

int th_check(const char *name, int ok, const char *fmt, ...)
{
    char detail[256] = "";
    if (fmt)
    {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(detail, sizeof(detail), fmt, ap);
        va_end(ap);
    }
    th_log("%s %-40s %s", ok ? "PASS" : "FAIL", name, detail);
    if (ok)
        n_pass++;
    else
        n_fail++;
    return ok;
}

static int summary_done;

void th_summary(void)
{
    summary_done = 1;
    th_log("RESULT %s: %d pass, %d fail", test_name, n_pass, n_fail);
    th_log("teardown: start");
}

int th_finish(void)
{
    if (!summary_done)
        th_summary();
    th_log("teardown: done");
    th_log("END");
    return n_fail ? 1 : 0;
}

static int cmp_u32(const void *a, const void *b)
{
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return (x > y) - (x < y);
}

void th_stats_add(th_stats *s, uint32_t us)
{
    if (s->count < (int)(sizeof(s->samples) / sizeof(s->samples[0])))
        s->samples[s->count++] = us;
}

void th_stats_report(const th_stats *s, const char *label)
{
    uint32_t sorted[1024];
    uint64_t sum = 0;
    int i;
    if (s->count == 0)
    {
        th_log("TIME %-28s no samples", label);
        return;
    }
    memcpy(sorted, s->samples, sizeof(uint32_t) * s->count);
    qsort(sorted, s->count, sizeof(uint32_t), cmp_u32);
    for (i = 0; i < s->count; i++)
        sum += sorted[i];
    th_log("TIME %-28s n=%d mean=%lu us p50=%lu p95=%lu max=%lu",
           label, s->count,
           (unsigned long)(sum / s->count),
           (unsigned long)sorted[s->count / 2],
           (unsigned long)sorted[(s->count * 95) / 100],
           (unsigned long)sorted[s->count - 1]);
}
