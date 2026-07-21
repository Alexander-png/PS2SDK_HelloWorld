#ifndef GFX_TEXTURE_INTERNAL_H
#define GFX_TEXTURE_INTERNAL_H

#include "engine/gfx/texture.h"
#include <gsKit.h>

typedef struct gfx_texture_cpu_image {
    void *pixels;
    u32 size_bytes;
    u16 width;
    u16 height;
    u8 psm;
} gfx_texture_cpu_image_t;

int gfx_texture_get_gstexture_internal(gfx_texture_handle_t handle, GSTEXTURE **out_tex);

#endif