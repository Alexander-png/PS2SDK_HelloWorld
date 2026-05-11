#include "resources.h"
#include "engine/logging/log.h"

#include <string.h>
#include <malloc.h>
#include <fcntl.h>
#include <unistd.h>

typedef struct resource_item {
    int used;
    u16 generation;

    char path[128];
    resource_type_t type;
    resource_status_t status;

    void *data;
    u32 size;

    stream_handle_t stream;

    resource_callback_t callback;
    void *userdata;
} resource_item_t;

typedef struct resource_system {
    int initialized;
    resource_item_t items[RESOURCE_MAX_ITEMS];
} resource_system_t;

static resource_system_t g_resources;

static resource_handle_t resource_invalid_handle(void)
{
    resource_handle_t h;
    h.index = 0xffffu;
    h.generation = 0;
    return h;
}

static int resource_handle_to_index(resource_handle_t handle)
{
    resource_item_t *r;

    if (handle.index >= RESOURCE_MAX_ITEMS)
        return -1;

    r = &g_resources.items[handle.index];

    if (!r->used)
        return -1;

    if (r->generation != handle.generation)
        return -1;

    return (int)handle.index;
}

static int resource_file_size(const char *path, u32 *out_size)
{
    int fd;
    off_t end;

    if (!path || !out_size)
        return -1;

    fd = open(path, O_RDONLY);
    if (fd < 0)
        return -2;

    end = lseek(fd, 0, SEEK_END);
    close(fd);

    if (end <= 0)
        return -3;

    *out_size = (u32)end;
    return 0;
}

static void resource_stream_callback(stream_handle_t stream,
                                     stream_status_t stream_status,
                                     int bytes_read,
                                     void *userdata)
{
    resource_handle_t handle;
    int index;
    resource_item_t *r;

    (void)stream;

    handle = *(resource_handle_t *)userdata;
    index = resource_handle_to_index(handle);
    if (index < 0)
        return;

    r = &g_resources.items[index];

    if (stream_status == STREAM_STATUS_READY &&
        bytes_read == (int)r->size) {
        r->status = RESOURCE_STATUS_READY;
        LOGLN("[resources] ready index=%d type=%s path=%s size=%u",
              index,
              resource_type_name(r->type),
              r->path,
              r->size);
    } else {
        r->status = RESOURCE_STATUS_FAILED;
        LOGLN("[resources] failed index=%d stream_status=%s path=%s",
              index,
              streaming_status_name(stream_status),
              r->path);
    }

    streaming_release(r->stream);

    if (r->callback)
        r->callback(handle, r->status, r->data, r->size, r->userdata);
}

int resources_init(void)
{
    memset(&g_resources, 0, sizeof(g_resources));
    g_resources.initialized = 1;

    LOGLN("[resources] init max_items=%d", RESOURCE_MAX_ITEMS);
    return 0;
}

void resources_shutdown(void)
{
    int i;

    if (!g_resources.initialized)
        return;

    LOGLN("[resources] shutdown");

    for (i = 0; i < RESOURCE_MAX_ITEMS; i++) {
        if (g_resources.items[i].used) {
            resource_handle_t h;
            h.index = (u16)i;
            h.generation = g_resources.items[i].generation;
            resource_release(h);
        }
    }

    memset(&g_resources, 0, sizeof(g_resources));
}

void resources_update(void)
{
    /*
     * Placeholder.
     *
     * Current streaming callbacks run during streaming_update(), so no work
     * is required here yet. Later this can dispatch queued resource events,
     * upload texture pages, parse sprite banks, or finalize room data.
     */
}

resource_handle_t resource_load_file(const resource_load_desc_t *desc)
{
    int i;
    int rc;
    u32 size;
    void *data;
    resource_item_t *r;
    resource_handle_t h;
    stream_request_desc_t stream_desc;

    if (!g_resources.initialized || !desc || !desc->path)
        return resource_invalid_handle();

    rc = resource_file_size(desc->path, &size);
    if (rc < 0) {
        LOGLN("[resources] size failed rc=%d path=%s", rc, desc->path);
        return resource_invalid_handle();
    }

    data = memalign(64, size);
    if (!data) {
        LOGLN("[resources] alloc failed size=%u path=%s", size, desc->path);
        return resource_invalid_handle();
    }

    for (i = 0; i < RESOURCE_MAX_ITEMS; i++) {
        if (!g_resources.items[i].used)
            break;
    }

    if (i >= RESOURCE_MAX_ITEMS) {
        free(data);
        LOGLN("[resources] no free slots");
        return resource_invalid_handle();
    }

    r = &g_resources.items[i];
    {
        u16 generation = r->generation;
        memset(r, 0, sizeof(*r));
        r->generation = generation + 1;
        if (r->generation == 0)
            r->generation = 1;
    }

    r->used = 1;
    strncpy(r->path, desc->path, sizeof(r->path) - 1);
    r->path[sizeof(r->path) - 1] = '\0';
    r->type = desc->type;
    r->status = RESOURCE_STATUS_LOADING;
    r->data = data;
    r->size = size;
    r->callback = desc->callback;
    r->userdata = desc->userdata;

    h.index = (u16)i;
    h.generation = r->generation;

    memset(&stream_desc, 0, sizeof(stream_desc));
    stream_desc.path = r->path;
    stream_desc.offset = 0;
    stream_desc.size = r->size;
    stream_desc.dst = r->data;
    stream_desc.priority = desc->priority;
    stream_desc.callback = resource_stream_callback;
    stream_desc.userdata = &g_resources.items[i].stream;

    /*
     * Store the resource handle in memory reserved for callback userdata.
     * We use the stream field storage temporarily only until the request is
     * created below, so pass a stable static slot-local handle instead.
     */
    {
        static resource_handle_t callback_handles[RESOURCE_MAX_ITEMS];
        callback_handles[i] = h;
        stream_desc.userdata = &callback_handles[i];
    }

    r->stream = streaming_request_file(&stream_desc);
    if (!streaming_is_valid(r->stream)) {
        LOGLN("[resources] stream request failed path=%s", r->path);
        free(r->data);
        memset(r, 0, sizeof(*r));
        return resource_invalid_handle();
    }

    LOGLN("[resources] load index=%d gen=%u type=%s path=%s size=%u",
          i,
          h.generation,
          resource_type_name(r->type),
          r->path,
          r->size);

    return h;
}

void resource_release(resource_handle_t handle)
{
    int index;
    resource_item_t *r;
    u16 generation;

    index = resource_handle_to_index(handle);
    if (index < 0)
        return;

    r = &g_resources.items[index];

    if (r->status == RESOURCE_STATUS_LOADING)
        streaming_cancel(r->stream);

    streaming_release(r->stream);

    if (r->data)
        free(r->data);

    generation = r->generation;
    memset(r, 0, sizeof(*r));
    r->generation = generation;

    LOGLN("[resources] released index=%d", index);
}

int resource_is_valid(resource_handle_t handle)
{
    return resource_handle_to_index(handle) >= 0;
}

resource_status_t resource_status(resource_handle_t handle)
{
    int index;

    index = resource_handle_to_index(handle);
    if (index < 0)
        return RESOURCE_STATUS_UNUSED;

    return g_resources.items[index].status;
}

resource_type_t resource_type(resource_handle_t handle)
{
    int index;

    index = resource_handle_to_index(handle);
    if (index < 0)
        return RESOURCE_TYPE_RAW;

    return g_resources.items[index].type;
}

void *resource_data(resource_handle_t handle)
{
    int index;

    index = resource_handle_to_index(handle);
    if (index < 0)
        return NULL;

    return g_resources.items[index].data;
}

u32 resource_size(resource_handle_t handle)
{
    int index;

    index = resource_handle_to_index(handle);
    if (index < 0)
        return 0;

    return g_resources.items[index].size;
}

const char *resource_path(resource_handle_t handle)
{
    int index;

    index = resource_handle_to_index(handle);
    if (index < 0)
        return "";

    return g_resources.items[index].path;
}

const char *resource_status_name(resource_status_t status)
{
    switch (status) {
    case RESOURCE_STATUS_UNUSED:  return "unused";
    case RESOURCE_STATUS_LOADING: return "loading";
    case RESOURCE_STATUS_READY:   return "ready";
    case RESOURCE_STATUS_FAILED:  return "failed";
    default:                      return "unknown";
    }
}

const char *resource_type_name(resource_type_t type)
{
    switch (type) {
    case RESOURCE_TYPE_RAW:          return "raw";
    case RESOURCE_TYPE_SPRITE_BANK:  return "sprite_bank";
    case RESOURCE_TYPE_ANIM_BANK:    return "anim_bank";
    case RESOURCE_TYPE_ROOM:         return "room";
    case RESOURCE_TYPE_TEXTURE_PAGE: return "texture_page";
    case RESOURCE_TYPE_AUDIO_INFO:   return "audio_info";
    default:                         return "unknown";
    }
}
