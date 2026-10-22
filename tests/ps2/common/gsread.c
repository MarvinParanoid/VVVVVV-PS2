#include "gsread.h"

#ifdef __ps2sdk__
#include <kernel.h>
#include <screenshot.h>
#include <dmaKit.h>
#include <gsKit.h>

#define GSREAD_BUFFER_BYTES (GSREAD_W * GSREAD_H * 4)
#define GSREAD_PSM_CT24 1

/* ps2_screenshot() clears the GS FINISH event on exit, but gsKit's next
 * queue exec waits for that event (gsKit_finish), which would then hang
 * forever. Send a FINISH through the GIF and wait for it, as gsKit would. */
static void rearm_gs_finish(void)
{
    static u64 packet[4] __attribute__((aligned(16)));
    packet[0] = GIF_TAG(1, 1, 0, 0, 0, 1);
    packet[1] = GIF_AD;
    packet[2] = 0;
    packet[3] = GS_FINISH;
    FlushCache(0);
    dmaKit_send(DMA_CHANNEL_GIF, packet, 2);
    dmaKit_wait_fast();
    while (!(GS_CSR_FINISH))
        ;
}

int gsread_framebuffer(int buffer_index, uint8_t *dst)
{
    /* BITBLTBUF.SBP is in units of 64 words (256 bytes) */
    unsigned int vram = (unsigned int)(buffer_index * GSREAD_BUFFER_BYTES) / 256;
    int rc = ps2_screenshot(dst, vram, 0, 0, GSREAD_W, GSREAD_H, GSREAD_PSM_CT24);
    /* DMA wrote around the data cache */
    FlushCache(0);
    rearm_gs_finish();
    return rc == 1 ? 0 : -1;
}
#else
int gsread_framebuffer(int buffer_index, uint8_t *dst)
{
    (void)buffer_index;
    (void)dst;
    return -1;
}
#endif

int gsread_is_blank(const uint8_t *buf)
{
    int i;
    for (i = 0; i < GSREAD_W * GSREAD_H * 3; i++)
        if (buf[i])
            return 0;
    return 1;
}
