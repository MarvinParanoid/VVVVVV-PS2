/* music_bench: research measurements for P2b (music strategy choice).
 *
 * Needs, next to the ELF (user-provided, never committed):
 *   data.zip         the user's data.zip (vvvvvvmusic.vvv is deflate-compressed)
 *   data-stored.zip  optional: same archive with vvvvvvmusic.vvv stored (zip -0)
 *
 * Measures on the EE:
 *  1. PhysicsFS-in-zip cost of reaching an offset inside vvvvvvmusic.vvv
 *     (fresh handle + PHYSFS_seek), compressed vs stored entry, and
 *     sequential read throughput.
 *  2. stb_vorbis (same build flags as VVVVVV: float output, pull API) decode
 *     cost of a whole track held in RAM: pure CPU, no I/O. Reported as
 *     microseconds per second of audio and as % of one EE core in real time.
 *  3. Peak heap use of one open stb_vorbis decoder. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>

#include <physfs.h>

#include "../common/harness.h"

/* same environment Music.cpp gives FAudio's stb_vorbis.h (it has its stdio
 * include compiled out and uses FAudio_alloca for temporaries) */
#include <alloca.h>
#include <assert.h>
#include <limits.h>
#include <math.h>
#define FAUDIOAPI
#define FAudio_alloca(x) alloca(x)
#define FAudio_dealloca(x) ((void)(x))
#define STB_VORBIS_NO_PUSHDATA_API 1
#define STB_VORBIS_NO_INTEGER_CONVERSION 1
#define STB_VORBIS_NO_STDIO 1
#include "stb_vorbis.h"

/* VVVVVV BinaryBlob.h layout */
typedef struct
{
    char name[48];
    int start_UNUSED;
    int size;
    unsigned char valid;
} resourceheader;
#define MAX_HEADERS 128

typedef struct
{
    char name[48];
    long long offset;
    int size;
} track;

static track tracks[MAX_HEADERS];
static int ntracks;

static int read_index(const char *what)
{
    static resourceheader h[MAX_HEADERS];
    PHYSFS_File *f = PHYSFS_openRead("vvvvvvmusic.vvv");
    long long off = sizeof(h);
    int i;
    ntracks = 0;
    if (!f)
    {
        th_log("%s: cannot open vvvvvvmusic.vvv: %s", what, PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
        return 0;
    }
    if (PHYSFS_readBytes(f, h, sizeof(h)) != (PHYSFS_sint64)sizeof(h))
    {
        PHYSFS_close(f);
        return 0;
    }
    for (i = 0; i < MAX_HEADERS; i++)
    {
        if (h[i].valid != 1 || h[i].size < 1)
            continue;
        h[i].name[47] = 0;
        strcpy(tracks[ntracks].name, h[i].name);
        tracks[ntracks].offset = off;
        tracks[ntracks].size = h[i].size;
        ntracks++;
        off += h[i].size;
    }
    th_log("%s: blob length %lld, %d tracks, header table %u B", what,
           (long long)PHYSFS_fileLength(f), ntracks, (unsigned)sizeof(h));
    PHYSFS_close(f);
    return ntracks;
}

static void seek_costs(const char *archive)
{
    static char buf[65536];
    int i, k;
    if (!PHYSFS_mount(archive, NULL, 0))
    {
        th_log("SKIP %s: %s", archive, PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
        return;
    }
    th_log("--- %s ---", archive);
    if (read_index(archive))
    {
        /* reach the start of several tracks with a fresh handle each time
         * (what a lazy per-track open would do), then read 64 KiB */
        int picks[4] = {0, ntracks / 3, (2 * ntracks) / 3, ntracks - 1};
        for (k = 0; k < 4; k++)
        {
            track *t = &tracks[picks[k]];
            PHYSFS_File *f = PHYSFS_openRead("vvvvvvmusic.vvv");
            uint64_t t0 = th_now_us(), t1, t2;
            PHYSFS_seek(f, (PHYSFS_uint64)t->offset);
            t1 = th_now_us();
            PHYSFS_readBytes(f, buf, sizeof(buf));
            t2 = th_now_us();
            th_log("SEEK %-12s %-44s offset %9lld: seek %8lu us, then 64 KiB read %6lu us", archive, t->name,
                   t->offset, (unsigned long)(t1 - t0), (unsigned long)(t2 - t1));
            /* loop back to the same track start from further in (backward seek) */
            PHYSFS_seek(f, (PHYSFS_uint64)t->offset + 2 * 1024 * 1024 < (PHYSFS_uint64)PHYSFS_fileLength(f)
                               ? (PHYSFS_uint64)t->offset + 2 * 1024 * 1024 : (PHYSFS_uint64)t->offset);
            t0 = th_now_us();
            PHYSFS_seek(f, (PHYSFS_uint64)t->offset);
            th_log("SEEKBACK %-8s %-44s 2 MiB back: %8lu us", archive, t->name, (unsigned long)(th_now_us() - t0));
            PHYSFS_close(f);
        }
        {
            /* sequential throughput over 4 MiB */
            PHYSFS_File *f = PHYSFS_openRead("vvvvvvmusic.vvv");
            uint64_t t0 = th_now_us();
            for (i = 0; i < 64; i++)
                PHYSFS_readBytes(f, buf, sizeof(buf));
            th_log("READ %-12s sequential 4 MiB: %lu us (%.2f MB/s)", archive, (unsigned long)(th_now_us() - t0),
                   4.0 * 1048576.0 / (double)(th_now_us() - t0));
            PHYSFS_close(f);
        }
    }
    PHYSFS_unmount(archive);
}

static void decode_costs(void)
{
    static float out[4096 * 2];
    int k;
    if (!PHYSFS_mount("data.zip", NULL, 0) || !read_index("data.zip"))
        return;
    th_log("--- stb_vorbis decode (track in RAM, CPU only) ---");
    for (k = 0; k < ntracks && k < 16; k += 5)
    {
        track *t = &tracks[k];
        unsigned char *mem = (unsigned char *)malloc(t->size);
        PHYSFS_File *f = PHYSFS_openRead("vvvvvvmusic.vvv");
        stb_vorbis *v;
        stb_vorbis_info info;
        struct mallinfo m0, m1;
        int err = 0, n;
        long long samples = 0;
        uint64_t t0, t_open, t_dec = 0, max_chunk = 0;
        if (!mem || !f)
            break;
        PHYSFS_seek(f, (PHYSFS_uint64)t->offset);
        PHYSFS_readBytes(f, mem, t->size);
        PHYSFS_close(f);

        m0 = mallinfo();
        t0 = th_now_us();
        v = stb_vorbis_open_memory(mem, t->size, &err, NULL);
        t_open = th_now_us() - t0;
        if (!v)
        {
            th_log("open failed for %s: %d", t->name, err);
            free(mem);
            continue;
        }
        m1 = mallinfo();
        info = stb_vorbis_get_info(v);
        /* decode up to 30 s in VVVVVV-sized chunks (50 ms) */
        for (;;)
        {
            uint64_t c0 = th_now_us(), dt;
            n = stb_vorbis_get_samples_float_interleaved(v, info.channels, out, info.sample_rate / 20 * info.channels);
            dt = th_now_us() - c0;
            if (n <= 0)
                break;
            t_dec += dt;
            if (dt > max_chunk)
                max_chunk = dt;
            samples += n;
            if (samples >= (long long)info.sample_rate * 30)
                break;
        }
        th_log("DECODE %-44s %d Hz %d ch, %d B, open %lu us, decoder heap %lu B, %.1f s decoded in %lu us "
               "=> %lu us per audio second (%.1f%% of real time), worst 50 ms chunk %lu us",
               t->name, info.sample_rate, info.channels, t->size, (unsigned long)t_open,
               (unsigned long)(m1.uordblks - m0.uordblks), samples / (double)info.sample_rate,
               (unsigned long)t_dec, (unsigned long)(t_dec * info.sample_rate / (samples ? samples : 1)),
               t_dec * 100.0 * info.sample_rate / 1e6 / (samples ? samples : 1), (unsigned long)max_chunk);
        stb_vorbis_close(v);
        free(mem);
    }
    PHYSFS_unmount("data.zip");
}

int main(int argc, char **argv)
{
    th_init("music_bench", argc, argv);
    if (!PHYSFS_init(argc > 0 ? argv[0] : NULL))
    {
        th_check("PHYSFS_init", 0, "%s", PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
        return th_finish();
    }
    th_mem_report("start");
    seek_costs("data.zip");
    seek_costs("data-stored.zip");
    decode_costs();
    th_mem_report("end");
    PHYSFS_deinit();
    th_summary();
    return th_finish();
}
