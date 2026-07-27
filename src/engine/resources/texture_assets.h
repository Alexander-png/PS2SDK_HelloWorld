#ifndef TEXTURE_ASSETS_H
#define TEXTURE_ASSETS_H

#include <tamtypes.h>
#include "engine/resources/resources.h"
#include "engine/gfx/texture.h"

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
void texture_assets_update(void);

texture_handle_t texture_invalid_handle(void);
texture_handle_t texture_load_png(const char *path, stream_priority_t prio);
void             texture_release(texture_handle_t handle);

int              texture_is_valid(texture_handle_t handle);
texture_status_t texture_status(texture_handle_t handle);
const char      *texture_path(texture_handle_t handle);

int              texture_get_gfx_handle(texture_handle_t handle,
                                        gfx_texture_handle_t *out_handle);

int              texture_touch(texture_handle_t handle);
int              texture_size(texture_handle_t handle, int *out_w, int *out_h);

#ifdef __cplusplus
}
#endif

#endif