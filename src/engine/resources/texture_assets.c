#include "engine/resources/texture_assets.h"
#include "engine/logging/log.h"

#include <string.h>

typedef struct texture_item {
    int used;
    u16 generation;

    char path[128];

    resource_handle_t resource;
    texture_status_t  status;

    int tex_id;
    int prewarmed;
} texture_item_t;

typedef struct texture_system {
    int initialized;
    texture_item_t items[TEXTURE_MAX_ITEMS];
} texture_system_t;

static texture_system_t g_texturesys;

static texture_handle_t texture_invalid_handle(void)
{
    texture_handle_t h;
    h.index = 0xffffu;
    h.generation = 0;
    return h;
}

static int texture_handle_to_index(texture_handle_t handle)
{
    texture_item_t *t;

    if (handle.index >= TEXTURE_MAX_ITEMS)
        return -1;

    t = &g_texturesys.items[handle.index];

    if (!t->used)
        return -1;

    if (t->generation != handle.generation)
        return -1;

    return (int)handle.index;
}

static void texture_resource_cb(resource_handle_t rhandle,
                                resource_status_t rstatus,
                                void *data,
                                u32 size,
                                void *userdata)
{
    texture_handle_t th = *(texture_handle_t *)userdata;
    int index = texture_handle_to_index(th);
    texture_item_t *t;
    int tex_id;

    if (index < 0)
        return;

    t = &g_texturesys.items[index];

    if (rstatus != RESOURCE_STATUS_READY) {
        t->status = TEXTURE_STATUS_FAILED;
        LOGLNC(LOGCAT_RESOURCES, "[tex] resource failed index=%d path=%s status=%s",
              index,
              t->path,
              resource_status_name(rstatus));
        return;
    }

    if (!data || size == 0) {
        t->status = TEXTURE_STATUS_FAILED;
        LOGLNC(LOGCAT_RESOURCES, "[tex] resource ready but empty index=%d path=%s", index, t->path);
        return;
    }

    if (gfx2d_create_texture_from_png_data(data, size, &tex_id) < 0) {
        t->status = TEXTURE_STATUS_FAILED;
        LOGLNC(LOGCAT_RESOURCES, "[tex] gfx2d_create_texture_from_png_data failed index=%d path=%s", index, t->path);
        return;
    }

    t->tex_id  = tex_id;
    t->status  = TEXTURE_STATUS_READY;
    t->prewarmed = 0;

    LOGLNC(LOGCAT_RESOURCES, "[tex] ready index=%d path=%s tex_id=%d size=%u",
          index, t->path, t->tex_id, size);
}

int texture_assets_init(void)
{
    memset(&g_texturesys, 0, sizeof(g_texturesys));
    g_texturesys.initialized = 1;
    LOGLNC(LOGCAT_RESOURCES, "[tex] init max_items=%d", TEXTURE_MAX_ITEMS);
    return 0;
}

void texture_assets_shutdown(void)
{
    int i;

    if (!g_texturesys.initialized)
        return;

    LOGLNC(LOGCAT_RESOURCES, "[tex] shutdown");

    for (i = 0; i < TEXTURE_MAX_ITEMS; ++i) {
        if (g_texturesys.items[i].used) {
            texture_handle_t h;
            h.index = (u16)i;
            h.generation = g_texturesys.items[i].generation;
            texture_release(h);
        }
    }

    memset(&g_texturesys, 0, sizeof(g_texturesys));
}

void texture_assets_update(void)
{

}

texture_handle_t texture_load_png(const char *path, stream_priority_t prio)
{
    int i;
    texture_item_t *t;
    texture_handle_t th;
    resource_load_desc_t desc;
    resource_handle_t rh;

    if (!g_texturesys.initialized || !path)
        return texture_invalid_handle();

    for (i = 0; i < TEXTURE_MAX_ITEMS; ++i) {
        if (!g_texturesys.items[i].used)
            break;
    }

    if (i >= TEXTURE_MAX_ITEMS) {
        LOGLNC(LOGCAT_RESOURCES, "[tex] no free slots");
        return texture_invalid_handle();
    }

    t = &g_texturesys.items[i];
    {
        u16 generation = t->generation;
        memset(t, 0, sizeof(*t));
        t->generation = generation + 1;
        if (t->generation == 0)
            t->generation = 1;
    }

    t->used = 1;
    strncpy(t->path, path, sizeof(t->path) - 1);
    t->path[sizeof(t->path) - 1] = '\0';
    t->status = TEXTURE_STATUS_LOADING;
    t->tex_id = -1;
    t->prewarmed = 0;
    t->resource = (resource_handle_t){0xffffu, 0};

    th.index = (u16)i;
    th.generation = t->generation;

    memset(&desc, 0, sizeof(desc));
    desc.path     = t->path;
    desc.type     = RESOURCE_TYPE_TEXTURE_PAGE;
    desc.priority = prio;

    {
        static texture_handle_t cb_handles[TEXTURE_MAX_ITEMS];
        cb_handles[i] = th;
        desc.callback = texture_resource_cb;
        desc.userdata = &cb_handles[i];
    }

    rh = resource_load_file(&desc);
    if (!resource_is_valid(rh)) {
        u16 generation = t->generation;
        LOGLNC(LOGCAT_RESOURCES, "[tex] resource_load_file failed path=%s", t->path);
        memset(t, 0, sizeof(*t));
        t->generation = generation;
        return texture_invalid_handle();
    }

    t->resource = rh;

    LOGLNC(LOGCAT_RESOURCES, "[tex] load index=%d gen=%u path=%s",
          i, th.generation, t->path);

    return th;
}

void texture_release(texture_handle_t handle)
{
    int index;
    texture_item_t *t;
    u16 generation;

    index = texture_handle_to_index(handle);
    if (index < 0)
        return;

    t = &g_texturesys.items[index];

    if (resource_is_valid(t->resource)) {
        resource_release(t->resource);
        t->resource = (resource_handle_t){0xffffu, 0};
    }

    if (t->tex_id >= 0) {
        gfx2d_free_texture(t->tex_id);
        t->tex_id = -1;
    }

    generation = t->generation;
    memset(t, 0, sizeof(*t));
    t->generation = generation;

    LOGLNC(LOGCAT_RESOURCES, "[tex] released index=%d", index);
}

int texture_is_valid(texture_handle_t handle)
{
    return texture_handle_to_index(handle) >= 0;
}

texture_status_t texture_status(texture_handle_t handle)
{
    int index = texture_handle_to_index(handle);
    if (index < 0)
        return TEXTURE_STATUS_UNUSED;
    return g_texturesys.items[index].status;
}

int texture_tex_id(texture_handle_t handle)
{
    int index = texture_handle_to_index(handle);
    if (index < 0)
        return -1;
    return g_texturesys.items[index].tex_id;
}

const char *texture_path(texture_handle_t handle)
{
    int index = texture_handle_to_index(handle);
    if (index < 0)
        return "";
    return g_texturesys.items[index].path;
}

int texture_prewarm(texture_handle_t handle)
{
    int index = texture_handle_to_index(handle);
    texture_item_t *t;
    int mask;

    if (index < 0)
        return -1;

    t = &g_texturesys.items[index];

    if (t->status != TEXTURE_STATUS_READY || t->tex_id < 0)
        return -1;

    mask = gfx2d_touch_texture(t->tex_id);
    if (mask >= 0)
        t->prewarmed = 1;

    return mask;
}