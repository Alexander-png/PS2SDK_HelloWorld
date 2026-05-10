#ifndef GFX_H
#define GFX_H

#include <stdint.h>

/* Initialize GS in NTSC 640x240 non-interlaced, allocate framebuffer +
 * zbuffer, and reserve a 512x256 PSMCT32 texture region for video frames.
 * Call once at startup. */
void gfx_init(void);

/* Upload a 352x240 RGBA buffer as the current video texture and draw it
 * stretched to fill the 640x240 visible area. Blocks until the GIF DMA
 * completes; caller is then free to overwrite the buffer.
 * `rgba` must be 64-byte aligned and 352*240*4 bytes long. */
void gfx_present_frame(const uint8_t *rgba, int width, int height);

/* Wait for the next vsync (vertical retrace). */
void gfx_wait_vsync(void);

#endif