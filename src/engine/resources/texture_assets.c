#include "engine/resources/texture_assets.h"
#include "engine/logging/log.h"

#include <string.h>

typedef struct texture_item {
    int used;
    u16 generation;
    u16 ref_count;

    char path[128];

    resource_handle_t resource;
    texture_status_t  status;

    gfx_texture_handle_t gfx_texture;
} texture_item_t;

typedef struct texture_system {
    int initialized;
    texture_item_t items[TEXTURE_MAX_ITEMS];
} texture_system_t;

typedef struct texture_load_ctx {
    u16 index;
    u16 generation;
} texture_load_ctx_t;

static texture_system_t g_texturesys;
static texture_load_ctx_t g_load_ctx[TEXTURE_MAX_ITEMS];

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

static int texture_find_by_path(const char *path)
{
    int i;

    if (!path || !path[0])
        return -1;

    for (i = 0; i < TEXTURE_MAX_ITEMS; ++i) {
        texture_item_t *t = &g_texturesys.items[i];
        if (!t->used)
            continue;

        if (strcmp(t->path, path) == 0)
            return i;
    }

    return -1;
}

static void texture_reset_item(texture_item_t *t, int preserve_generation)
{
    u16 generation = 0;

    if (!t)
        return;

    if (preserve_generation)
        generation = t->generation;

    memset(t, 0, sizeof(*t));

    if (preserve_generation)
        t->generation = generation;
}

static void texture_resource_cb(resource_handle_t rhandle,
                                resource_status_t rstatus,
                                void *data,
                                u32 size,
                                void *userdata)
{
    texture_load_ctx_t ctx;
    int index;
    texture_item_t *t;
    gfx_texture_desc_t desc;

    (void)rhandle;

    if (!userdata)
        return;

    ctx = *(texture_load_ctx_t *)userdata;

    if (ctx.index >= TEXTURE_MAX_ITEMS)
        return;

    t = &g_texturesys.items[ctx.index];

    if (!t->used)
        return;

    if (t->generation != ctx.generation)
        return;

    index = (int)ctx.index;

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
        LOGLNC(LOGCAT_RESOURCES, "[tex] resource ready but empty index=%d path=%s",
              index,
              t->path);
        return;
    }

    memset(&desc, 0, sizeof(desc));
    desc.flags = GFX_TEXTURE_FLAG_KEEP_CPU_COPY;

    if (gfx_texture_create_from_png_data(data, size, &desc, &t->gfx_texture) < 0) {
        t->status = TEXTURE_STATUS_FAILED;
        LOGLNC(LOGCAT_RESOURCES, "[tex] gfx_texture_create_from_png_data failed index=%d path=%s",
              index,
              t->path);
        return;
    }

    t->status = TEXTURE_STATUS_READY;

    LOGLNC(LOGCAT_RESOURCES, "[tex] ready index=%d path=%s size=%u",
          index,
          t->path,
          size);
}

int texture_assets_init(void)
{
    memset(&g_texturesys, 0, sizeof(g_texturesys));
    memset(g_load_ctx, 0, sizeof(g_load_ctx));
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
    memset(g_load_ctx, 0, sizeof(g_load_ctx));
}

void texture_assets_update(void)
{
}

texture_handle_t texture_invalid_handle(void)
{
    texture_handle_t h;
    h.index = 0xffffu;
    h.generation = 0;
    return h;
}

texture_handle_t texture_load_png(const char *path, stream_priority_t prio)
{
    int i;
    int existing;
    texture_item_t *t;
    texture_handle_t th;
    resource_load_desc_t desc;
    resource_handle_t rh;

    if (!g_texturesys.initialized || !path || !path[0])
        return texture_invalid_handle();

    existing = texture_find_by_path(path);
    if (existing >= 0) {
        texture_item_t *it = &g_texturesys.items[existing];
        texture_handle_t h;

        if (it->ref_count != 0xffffu)
            ++it->ref_count;

        h.index = (u16)existing;
        h.generation = it->generation;

        LOGLNC(LOGCAT_RESOURCES, "[tex] reuse index=%d gen=%u refs=%u path=%s",
              existing,
              (unsigned int)h.generation,
              (unsigned int)it->ref_count,
              it->path);

        return h;
    }

    for (i = 0; i < TEXTURE_MAX_ITEMS; ++i) {
        if (!g_texturesys.items[i].used)
            break;
    }

    if (i >= TEXTURE_MAX_ITEMS) {
        LOGLNC(LOGCAT_RESOURCES, "[tex] no free slots path=%s", path);
        return texture_invalid_handle();
    }

    t = &g_texturesys.items[i];
    {
        u16 generation = t->generation;
        texture_reset_item(t, 0);
        t->generation = generation + 1;
        if (t->generation == 0)
            t->generation = 1;
    }

    t->used = 1;
    t->ref_count = 1;
    strncpy(t->path, path, sizeof(t->path) - 1);
    t->path[sizeof(t->path) - 1] = '\0';
    t->status = TEXTURE_STATUS_LOADING;
    t->resource = (resource_handle_t){0xffffu, 0};
    t->gfx_texture = gfx_texture_invalid();

    th.index = (u16)i;
    th.generation = t->generation;

    memset(&desc, 0, sizeof(desc));
    desc.path = t->path;
    desc.type = RESOURCE_TYPE_TEXTURE_PAGE;
    desc.priority = prio;

    g_load_ctx[i].index = (u16)i;
    g_load_ctx[i].generation = t->generation;

    desc.callback = texture_resource_cb;
    desc.userdata = &g_load_ctx[i];

    rh = resource_load_file(&desc);
    if (!resource_is_valid(rh)) {
        u16 generation = t->generation;
        LOGLNC(LOGCAT_RESOURCES, "[tex] resource_load_file failed path=%s", t->path);
        texture_reset_item(t, 0);
        t->generation = generation;
        return texture_invalid_handle();
    }

    t->resource = rh;

    LOGLNC(LOGCAT_RESOURCES, "[tex] load index=%d gen=%u refs=%u path=%s",
          i,
          (unsigned int)th.generation,
          (unsigned int)t->ref_count,
          t->path);

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

    if (t->ref_count > 1) {
        --t->ref_count;
        LOGLNC(LOGCAT_RESOURCES, "[tex] release dec_ref index=%d refs=%u path=%s",
              index,
              (unsigned int)t->ref_count,
              t->path);
        return;
    }

    if (resource_is_valid(t->resource)) {
        resource_release(t->resource);
        t->resource = (resource_handle_t){0xffffu, 0};
    }

    if (gfx_texture_is_valid(t->gfx_texture)) {
        gfx_texture_release(t->gfx_texture);
        t->gfx_texture = gfx_texture_invalid();
    }

    generation = t->generation;
    texture_reset_item(t, 0);
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

const char *texture_path(texture_handle_t handle)
{
    int index = texture_handle_to_index(handle);
    if (index < 0)
        return "";
    return g_texturesys.items[index].path;
}

int texture_get_gfx_handle(texture_handle_t handle, gfx_texture_handle_t *out_handle)
{
    int index = texture_handle_to_index(handle);

    if (!out_handle)
        return -1;

    *out_handle = gfx_texture_invalid();

    if (index < 0)
        return -1;

    if (g_texturesys.items[index].status != TEXTURE_STATUS_READY)
        return -1;

    *out_handle = g_texturesys.items[index].gfx_texture;
    return 0;
}

int texture_touch(texture_handle_t handle)
{
    int index = texture_handle_to_index(handle);

    if (index < 0)
        return -1;

    if (g_texturesys.items[index].status != TEXTURE_STATUS_READY)
        return -1;

    return gfx_texture_touch(g_texturesys.items[index].gfx_texture);
}

int texture_size(texture_handle_t handle, int *out_w, int *out_h)
{
    int index = texture_handle_to_index(handle);

    if (index < 0)
        return -1;

    if (g_texturesys.items[index].status != TEXTURE_STATUS_READY)
        return -1;

    return gfx_texture_get_size(g_texturesys.items[index].gfx_texture, out_w, out_h);
}