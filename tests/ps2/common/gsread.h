/* Read back what the GS actually drew, for automated output checks.
 *
 * SDL 2.32.10's PS2 renderer (stock and patched) sets up gsKit with a
 * 640x448 GS_PSM_CT24 double-buffered framebuffer allocated first in VRAM:
 * buffer 0 at byte 0, buffer 1 at 640*448*4 = 1146880 (page aligned).
 * These addresses are an assumption about gsKit's allocator; callers render
 * the same frame several times and require both buffers to agree. */
#ifndef PS2TEST_GSREAD_H
#define PS2TEST_GSREAD_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GSREAD_W 640
#define GSREAD_H 448

/* Reads one framebuffer as packed RGB (3 bytes/pixel, GSREAD_W*GSREAD_H).
 * `dst` must be 64-byte aligned. Returns 0 on success, -1 if unsupported. */
int gsread_framebuffer(int buffer_index, uint8_t *dst);

/* 1 if every byte is zero. Observed with Play! 0.72 for every readback
 * (presumably no GS->EE local->host transfer support there). Treat as
 * "no data", not as a black frame. */
int gsread_is_blank(const uint8_t *buf);

#ifdef __cplusplus
}
#endif

#endif
