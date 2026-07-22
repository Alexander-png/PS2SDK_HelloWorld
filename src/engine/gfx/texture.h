#ifndef GFX_TEXTURE_H
#define GFX_TEXTURE_H

#include <tamtypes.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef GFX_TEXTURE_MAX_ITEMS
#define GFX_TEXTURE_MAX_ITEMS 32
#endif

typedef struct gfx_texture_handle {
    u16 index;
    u16 generation;
} gfx_texture_handle_t;

typedef enum gfx_texture_flags {
    GFX_TEXTURE_FLAG_NONE          = 0,
    GFX_TEXTURE_FLAG_KEEP_CPU_COPY = 1 << 0,
    GFX_TEXTURE_FLAG_PINNED        = 1 << 1,
    GFX_TEXTURE_FLAG_STREAMING     = 1 << 2
} gfx_texture_flags_t;

typedef struct gfx_texture_desc {
    u32 flags;
} gfx_texture_desc_t;

typedef struct gfx_texture_stats {
    u32 total_bytes;
    u32 used_bytes;
    u32 free_bytes;
    u32 texture_count;
} gfx_texture_stats_t;

int  gfx_texture_system_init(void);
void gfx_texture_system_shutdown(void);

gfx_texture_handle_t gfx_texture_invalid(void);
int  gfx_texture_is_valid(gfx_texture_handle_t handle);

int  gfx_texture_create_from_png_data(const void *data,
                                      u32 size,
                                      const gfx_texture_desc_t *desc,
                                      gfx_texture_handle_t *out_handle);

void gfx_texture_release(gfx_texture_handle_t handle);

int  gfx_texture_touch(gfx_texture_handle_t handle);
int  gfx_texture_get_size(gfx_texture_handle_t handle, int *out_w, int *out_h);
int  gfx_texture_get_stats(gfx_texture_stats_t *out);

#ifdef __cplusplus
}
#endif

#endif /* GFX_TEXTURE_H */