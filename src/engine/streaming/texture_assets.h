#ifndef TEXTURE_ASSETS_H
#define TEXTURE_ASSETS_H

#include <tamtypes.h>
#include "engine/resources/resources.h"
#include "engine/gfx/gfx2d.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef TEXTURE_MAX_ITEMS
#define TEXTURE_MAX_ITEMS 32
#endif

typedef struct texture_handle {
    u16 index;
    u16 generation;
} texture_handle_t;

typedef enum texture_status {
    TEXTURE_STATUS_UNUSED = 0,
    TEXTURE_STATUS_LOADING,
    TEXTURE_STATUS_READY,
    TEXTURE_STATUS_FAILED
} texture_status_t;

int  texture_assets_init(void);
void texture_assets_shutdown(void);
void texture_assets_update(void); /* optional, if need to load by part */

texture_handle_t texture_load_png(const char *path, stream_priority_t prio);
void             texture_release(texture_handle_t handle);

int              texture_is_valid(texture_handle_t handle);
texture_status_t texture_status(texture_handle_t handle);
int              texture_tex_id(texture_handle_t handle);   /* -1, if no */
const char      *texture_path(texture_handle_t handle);

int              texture_prewarm(texture_handle_t handle);  /* calls gfx2d_touch_texture */

#ifdef __cplusplus
}
#endif

#endif /* TEXTURE_ASSETS_H */