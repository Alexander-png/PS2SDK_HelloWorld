#ifndef GFX_RENDERER_H
#define GFX_RENDERER_H

#include <tamtypes.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct gfx_renderer_stats {
    u32 frame_index;
    int enabled;
    int mode;
    int width;
    int height;
} gfx_renderer_stats_t;

int  renderer_init(void);
void renderer_shutdown(void);

void renderer_set_enabled(int enabled);
int  renderer_is_enabled(void);

void renderer_begin_frame(void);
void renderer_end_frame(void);

void renderer_set_clear_color_rgba(unsigned char r,
                                   unsigned char g,
                                   unsigned char b,
                                   unsigned char a);

int  renderer_get_stats(gfx_renderer_stats_t *out);

#ifdef __cplusplus
}
#endif

#endif /* GFX_RENDERER_H */