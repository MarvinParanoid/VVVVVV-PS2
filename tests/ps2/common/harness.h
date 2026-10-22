/* Minimal test harness shared by the PS2 validation ELFs.
 * Logs go to stdout and to <name>.log in the current directory
 * (host: under PCSX2 with host filesystem enabled, mass: on hardware). */
#ifndef PS2TEST_HARNESS_H
#define PS2TEST_HARNESS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void th_init(const char *name, int argc, char **argv);
void th_log(const char *fmt, ...);
uint64_t th_now_us(void);
void th_mem_report(const char *label);
int th_check(const char *name, int ok, const char *fmt, ...);
/* PS2: set the calling thread's EE priority (0 = highest, 127 = lowest);
 * no-op on host. Logs old/new values. */
void th_set_thread_priority(int prio);
/* Log the RESULT line; call before SDL teardown (which may hang). */
void th_summary(void);
int th_finish(void);

/* Frame-time statistics over a fixed sample buffer. */
typedef struct
{
    uint32_t samples[1024];
    int count;
} th_stats;

void th_stats_add(th_stats *s, uint32_t us);
void th_stats_report(const th_stats *s, const char *label);

#ifdef __cplusplus
}
#endif

#endif
