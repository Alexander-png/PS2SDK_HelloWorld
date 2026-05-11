#include "streaming.h"
#include "engine/logging/log.h"

#include <string.h>
#include <fcntl.h>
#include <unistd.h>

typedef struct stream_request {
    int used;
    u16 generation;

    char path[128];
    u32 offset;
    u32 size;
    void *dst;

    stream_priority_t priority;
    stream_status_t status;

    int bytes_read;
    int result;

    stream_callback_t callback;
    void *userdata;
} stream_request_t;

typedef struct stream_system {
    int initialized;
    stream_request_t requests[STREAMING_MAX_REQUESTS];
} stream_system_t;

static stream_system_t g_streaming;

static stream_handle_t stream_make_invalid_handle(void)
{
    stream_handle_t h;
    h.index = 0xffffu;
    h.generation = 0;
    return h;
}

static int stream_handle_to_index(stream_handle_t handle)
{
    stream_request_t *r;

    if (handle.index >= STREAMING_MAX_REQUESTS)
        return -1;

    r = &g_streaming.requests[handle.index];

    if (!r->used)
        return -1;

    if (r->generation != handle.generation)
        return -1;

    return (int)handle.index;
}

static void stream_finish_request(stream_request_t *r,
                                  stream_handle_t handle,
                                  stream_status_t status,
                                  int result)
{
    r->status = status;
    r->result = result;

    if (r->callback)
        r->callback(handle, status, r->bytes_read, r->userdata);
}

static void stream_process_request(int index)
{
    stream_request_t *r;
    stream_handle_t handle;
    int fd;
    int remaining;
    unsigned char *dst;

    r = &g_streaming.requests[index];

    if (!r->used || r->status != STREAM_STATUS_QUEUED)
        return;

    handle.index = (u16)index;
    handle.generation = r->generation;

    if (!r->dst || r->size == 0 || r->path[0] == '\0') {
        stream_finish_request(r, handle, STREAM_STATUS_FAILED, -1);
        return;
    }

    r->status = STREAM_STATUS_READING;
    r->bytes_read = 0;
    r->result = 0;

    fd = open(r->path, O_RDONLY);
    if (fd < 0) {
        LOGLN("[streaming] open failed: %s", r->path);
        stream_finish_request(r, handle, STREAM_STATUS_FAILED, -2);
        return;
    }

    if (r->offset > 0) {
        if (lseek(fd, (off_t)r->offset, SEEK_SET) < 0) {
            close(fd);
            LOGLN("[streaming] seek failed: %s offset=%u", r->path, r->offset);
            stream_finish_request(r, handle, STREAM_STATUS_FAILED, -3);
            return;
        }
    }

    remaining = (int)r->size;
    dst = (unsigned char *)r->dst;

    while (remaining > 0) {
        int chunk;
        int rd;

        chunk = remaining;
        if (chunk > 64 * 1024)
            chunk = 64 * 1024;

        rd = read(fd, dst + r->bytes_read, chunk);
        if (rd <= 0)
            break;

        r->bytes_read += rd;
        remaining -= rd;
    }

    close(fd);

    if (r->bytes_read == (int)r->size) {
        stream_finish_request(r, handle, STREAM_STATUS_READY, r->bytes_read);
    } else {
        LOGLN("[streaming] short read: %s got=%d wanted=%u",
              r->path,
              r->bytes_read,
              r->size);
        stream_finish_request(r, handle, STREAM_STATUS_FAILED, -4);
    }
}

static int stream_find_request_to_process(void)
{
    int i;
    int best;
    int best_priority;

    best = -1;
    best_priority = -1;

    for (i = 0; i < STREAMING_MAX_REQUESTS; i++) {
        stream_request_t *r = &g_streaming.requests[i];

        if (!r->used || r->status != STREAM_STATUS_QUEUED)
            continue;

        if ((int)r->priority > best_priority) {
            best = i;
            best_priority = (int)r->priority;
        }
    }

    return best;
}

int streaming_init(void)
{
    memset(&g_streaming, 0, sizeof(g_streaming));
    g_streaming.initialized = 1;

    LOGLN("[streaming] init max_requests=%d", STREAMING_MAX_REQUESTS);
    return 0;
}

void streaming_shutdown(void)
{
    if (!g_streaming.initialized)
        return;

    LOGLN("[streaming] shutdown");
    memset(&g_streaming, 0, sizeof(g_streaming));
}

void streaming_update(void)
{
    int index;

    if (!g_streaming.initialized)
        return;

    /*
     * First version: process at most one queued request per frame.
     * This API is async-style, but the actual read is currently synchronous.
     * Later this can move to a worker thread or CD/DVD streaming backend
     * without changing callers.
     */
    index = stream_find_request_to_process();
    if (index >= 0)
        stream_process_request(index);
}

stream_handle_t streaming_request_file(const stream_request_desc_t *desc)
{
    int i;
    stream_request_t *r;
    stream_handle_t h;

    if (!g_streaming.initialized || !desc)
        return stream_make_invalid_handle();

    if (!desc->path || !desc->dst || desc->size == 0)
        return stream_make_invalid_handle();

    for (i = 0; i < STREAMING_MAX_REQUESTS; i++) {
        if (!g_streaming.requests[i].used)
            break;
    }

    if (i >= STREAMING_MAX_REQUESTS)
        return stream_make_invalid_handle();

    r = &g_streaming.requests[i];
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

    r->offset = desc->offset;
    r->size = desc->size;
    r->dst = desc->dst;
    r->priority = desc->priority;
    r->status = STREAM_STATUS_QUEUED;
    r->callback = desc->callback;
    r->userdata = desc->userdata;

    h.index = (u16)i;
    h.generation = r->generation;

    LOGLN("[streaming] request index=%d gen=%u path=%s offset=%u size=%u priority=%d",
          i,
          h.generation,
          r->path,
          r->offset,
          r->size,
          r->priority);

    return h;
}

void streaming_cancel(stream_handle_t handle)
{
    int index;
    stream_request_t *r;

    index = stream_handle_to_index(handle);
    if (index < 0)
        return;

    r = &g_streaming.requests[index];

    if (r->status == STREAM_STATUS_READY ||
        r->status == STREAM_STATUS_FAILED ||
        r->status == STREAM_STATUS_CANCELLED)
        return;

    r->status = STREAM_STATUS_CANCELLED;
    r->result = -5;

    if (r->callback)
        r->callback(handle, r->status, r->bytes_read, r->userdata);
}

void streaming_release(stream_handle_t handle)
{
    int index;
    stream_request_t *r;
    u16 generation;

    index = stream_handle_to_index(handle);
    if (index < 0)
        return;

    r = &g_streaming.requests[index];

    if (r->status == STREAM_STATUS_READING)
        return;

    generation = r->generation;
    memset(r, 0, sizeof(*r));
    r->generation = generation;
}

stream_status_t streaming_status(stream_handle_t handle)
{
    int index;

    index = stream_handle_to_index(handle);
    if (index < 0)
        return STREAM_STATUS_UNUSED;

    return g_streaming.requests[index].status;
}

int streaming_bytes_read(stream_handle_t handle)
{
    int index;

    index = stream_handle_to_index(handle);
    if (index < 0)
        return 0;

    return g_streaming.requests[index].bytes_read;
}

int streaming_is_valid(stream_handle_t handle)
{
    return stream_handle_to_index(handle) >= 0;
}

const char *streaming_status_name(stream_status_t status)
{
    switch (status) {
    case STREAM_STATUS_UNUSED:    return "unused";
    case STREAM_STATUS_QUEUED:    return "queued";
    case STREAM_STATUS_READING:   return "reading";
    case STREAM_STATUS_READY:     return "ready";
    case STREAM_STATUS_FAILED:    return "failed";
    case STREAM_STATUS_CANCELLED: return "cancelled";
    default:                      return "unknown";
    }
}
