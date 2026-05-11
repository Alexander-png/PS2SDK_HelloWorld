#ifndef RESOURCES_H
#define RESOURCES_H

#include <tamtypes.h>
#include "engine/streaming/streaming.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef RESOURCE_MAX_ITEMS
#define RESOURCE_MAX_ITEMS 32
#endif

typedef enum resource_type {
    RESOURCE_TYPE_RAW = 0,
    RESOURCE_TYPE_SPRITE_BANK,
    RESOURCE_TYPE_ANIM_BANK,
    RESOURCE_TYPE_ROOM,
    RESOURCE_TYPE_TEXTURE_PAGE,
    RESOURCE_TYPE_AUDIO_INFO
} resource_type_t;

typedef enum resource_status {
    RESOURCE_STATUS_UNUSED = 0,
    RESOURCE_STATUS_LOADING,
    RESOURCE_STATUS_READY,
    RESOURCE_STATUS_FAILED
} resource_status_t;

typedef struct resource_handle {
    u16 index;
    u16 generation;
} resource_handle_t;

typedef void (*resource_callback_t)(resource_handle_t handle,
                                    resource_status_t status,
                                    void *data,
                                    u32 size,
                                    void *userdata);

typedef struct resource_load_desc {
    const char *path;
    resource_type_t type;
    stream_priority_t priority;
    resource_callback_t callback;
    void *userdata;
} resource_load_desc_t;

int  resources_init(void);
void resources_shutdown(void);
void resources_update(void);

resource_handle_t resource_load_file(const resource_load_desc_t *desc);
void resource_release(resource_handle_t handle);

int resource_is_valid(resource_handle_t handle);
resource_status_t resource_status(resource_handle_t handle);
resource_type_t resource_type(resource_handle_t handle);

void *resource_data(resource_handle_t handle);
u32 resource_size(resource_handle_t handle);
const char *resource_path(resource_handle_t handle);

const char *resource_status_name(resource_status_t status);
const char *resource_type_name(resource_type_t type);

#ifdef __cplusplus
}
#endif

#endif /* RESOURCES_H */
