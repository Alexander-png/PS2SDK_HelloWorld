#include "streaming.h"
#include "engine/logging/log.h"
#include "engine/platform/platform.h"
#include "engine/memory/memory.h"

#include <kernel.h>
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

    int callback_pending;
    stream_status_t callback_status;
} stream_request_t;

typedef struct stream_system {
    int initialized;
    volatile int shutdown;

    int lock_sema;
    int work_sema;

    int worker_ids[STREAMING_WORKER_COUNT];
    void *worker_stacks[STREAMING_WORKER_COUNT];
    int worker_count;

    stream_request_t requests[STREAMING_MAX_REQUESTS];
} stream_system_t;

static stream_system_t g_streaming;

static void stream_lock(void)
{
    if (g_streaming.lock_sema >= 0)
        WaitSema(g_streaming.lock_sema);
}

static void stream_unlock(void)
{
    if (g_streaming.lock_sema >= 0)
        SignalSema(g_streaming.lock_sema);
}

static stream_handle_t stream_make_invalid_handle(void)
{
    stream_handle_t h;
    h.index = 0xffffu;
    h.generation = 0;
    return h;
}

static int stream_handle_to_index_unsafe(stream_handle_t handle)
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

static void stream_queue_callback_unsafe(stream_request_t *r,
                                         stream_status_t status,
                                         int result)
{
    r->status = status;
    r->result = result;
    r->callback_status = status;
    r->callback_pending = 1;
}

static int stream_claim_request_unsafe(void)
{
    int i;
    int best = -1;
    int best_priority = -1;

    for (i = 0; i < STREAMING_MAX_REQUESTS; i++) {
        stream_request_t *r = &g_streaming.requests[i];

        if (!r->used || r->status != STREAM_STATUS_QUEUED)
            continue;

        if ((int)r->priority > best_priority) {
            best = i;
            best_priority = (int)r->priority;
        }
    }

    if (best >= 0) {
        g_streaming.requests[best].status = STREAM_STATUS_READING;
        g_streaming.requests[best].bytes_read = 0;
        g_streaming.requests[best].result = 0;
    }

    return best;
}

static void stream_process_request(int index)
{
    stream_request_t local;
    int fd;
    int remaining;
    unsigned char *dst;

    stream_lock();
    if (index < 0 || index >= STREAMING_MAX_REQUESTS ||
        !g_streaming.requests[index].used ||
        g_streaming.requests[index].status != STREAM_STATUS_READING) {
        stream_unlock();
        return;
    }

    local = g_streaming.requests[index];
    stream_unlock();

    if (!local.dst || local.size == 0 || local.path[0] == '\0') {
        stream_lock();
        if (g_streaming.requests[index].used &&
            g_streaming.requests[index].generation == local.generation &&
            g_streaming.requests[index].status == STREAM_STATUS_READING) {
            stream_queue_callback_unsafe(&g_streaming.requests[index], STREAM_STATUS_FAILED, -1);
        }
        stream_unlock();
        return;
    }

    fd = open(local.path, O_RDONLY);
    if (fd < 0) {
        LOGLN("[streaming] open failed: %s", local.path);
        stream_lock();
        if (g_streaming.requests[index].used &&
            g_streaming.requests[index].generation == local.generation &&
            g_streaming.requests[index].status == STREAM_STATUS_READING) {
            stream_queue_callback_unsafe(&g_streaming.requests[index], STREAM_STATUS_FAILED, -2);
        }
        stream_unlock();
        return;
    }

    if (local.offset > 0) {
        if (lseek(fd, (off_t)local.offset, SEEK_SET) < 0) {
            close(fd);
            LOGLN("[streaming] seek failed: %s offset=%u", local.path, local.offset);
            stream_lock();
            if (g_streaming.requests[index].used &&
                g_streaming.requests[index].generation == local.generation &&
                g_streaming.requests[index].status == STREAM_STATUS_READING) {
                stream_queue_callback_unsafe(&g_streaming.requests[index], STREAM_STATUS_FAILED, -3);
            }
            stream_unlock();
            return;
        }
    }

    remaining = (int)local.size;
    dst = (unsigned char *)local.dst;

    while (remaining > 0) {
        int chunk = remaining;
        int rd;
        int cancelled = 0;

        if (chunk > 64 * 1024)
            chunk = 64 * 1024;

        stream_lock();
        if (!g_streaming.requests[index].used ||
            g_streaming.requests[index].generation != local.generation ||
            g_streaming.requests[index].status == STREAM_STATUS_CANCELLED) {
            cancelled = 1;
        }
        stream_unlock();

        if (cancelled) {
            close(fd);
            return;
        }

        rd = read(fd, dst + local.bytes_read, chunk);
        if (rd <= 0)
            break;

        local.bytes_read += rd;
        remaining -= rd;

        stream_lock();
        if (g_streaming.requests[index].used &&
            g_streaming.requests[index].generation == local.generation &&
            g_streaming.requests[index].status == STREAM_STATUS_READING) {
            g_streaming.requests[index].bytes_read = local.bytes_read;
        }
        stream_unlock();
    }

    close(fd);

    stream_lock();

    if (!g_streaming.requests[index].used ||
        g_streaming.requests[index].generation != local.generation) {
        stream_unlock();
        return;
    }

    if (g_streaming.requests[index].status == STREAM_STATUS_CANCELLED) {
        stream_unlock();
        return;
    }

    g_streaming.requests[index].bytes_read = local.bytes_read;

    if (local.bytes_read == (int)local.size) {
        stream_queue_callback_unsafe(&g_streaming.requests[index],
                                     STREAM_STATUS_READY,
                                     local.bytes_read);
    } else {
        LOGLN("[streaming] short read: %s got=%d wanted=%u",
              local.path, local.bytes_read, local.size);
        stream_queue_callback_unsafe(&g_streaming.requests[index],
                                     STREAM_STATUS_FAILED,
                                     -4);
    }

    stream_unlock();
}

static void stream_worker_thread(void *arg)
{
    (void)arg;

    while (!g_streaming.shutdown) {
        int index;

        WaitSema(g_streaming.work_sema);

        if (g_streaming.shutdown)
            break;

        stream_lock();
        index = stream_claim_request_unsafe();
        stream_unlock();

        if (index >= 0)
            stream_process_request(index);
    }

    ExitDeleteThread();
}

static void stream_dispatch_callbacks(void)
{
    int i;

    for (i = 0; i < STREAMING_MAX_REQUESTS; i++) {
        stream_handle_t handle;
        stream_callback_t callback;
        void *userdata;
        stream_status_t status;
        int bytes_read;
        int do_call = 0;

        stream_lock();

        if (g_streaming.requests[i].used &&
            g_streaming.requests[i].callback_pending) {
            handle.index = (u16)i;
            handle.generation = g_streaming.requests[i].generation;
            callback = g_streaming.requests[i].callback;
            userdata = g_streaming.requests[i].userdata;
            status = g_streaming.requests[i].callback_status;
            bytes_read = g_streaming.requests[i].bytes_read;
            g_streaming.requests[i].callback_pending = 0;
            do_call = 1;
        }

        stream_unlock();

        if (do_call && callback)
            callback(handle, status, bytes_read, userdata);
    }
}

int streaming_init(void)
{
    int i;
    ee_sema_t sema;

    memset(&g_streaming, 0, sizeof(g_streaming));
    g_streaming.lock_sema = -1;
    g_streaming.work_sema = -1;
    for (i = 0; i < STREAMING_WORKER_COUNT; i++) {
        g_streaming.worker_ids[i] = -1;
        g_streaming.worker_stacks[i] = NULL;
    }

    memset(&sema, 0, sizeof(sema));
    sema.max_count = 1;
    sema.init_count = 1;
    g_streaming.lock_sema = CreateSema(&sema);
    if (g_streaming.lock_sema < 0)
        return -1;

    memset(&sema, 0, sizeof(sema));
    sema.max_count = STREAMING_MAX_REQUESTS;
    sema.init_count = 0;
    g_streaming.work_sema = CreateSema(&sema);
    if (g_streaming.work_sema < 0) {
        DeleteSema(g_streaming.lock_sema);
        g_streaming.lock_sema = -1;
        return -2;
    }

    for (i = 0; i < STREAMING_WORKER_COUNT; i++) {
        ee_thread_t th;

        g_streaming.worker_stacks[i] = mem_alloc(STREAMING_THREAD_STACK_SIZE,
                                         16,
                                         MEMTAG_THREAD_STACK);
        if (!g_streaming.worker_stacks[i]) {
            streaming_shutdown();
            return -3;
        }

        memset(&th, 0, sizeof(th));
        th.func = stream_worker_thread;
        th.stack = g_streaming.worker_stacks[i];
        th.stack_size = STREAMING_THREAD_STACK_SIZE;
        th.gp_reg = &_gp;
        th.initial_priority = STREAMING_THREAD_PRIO;

        g_streaming.worker_ids[i] = CreateThread(&th);
        if (g_streaming.worker_ids[i] < 0) {
            streaming_shutdown();
            return -4;
        }

        StartThread(g_streaming.worker_ids[i], NULL);
        g_streaming.worker_count++;
    }

    g_streaming.initialized = 1;
    LOGLN("[streaming] init max_requests=%d workers=%d",
          STREAMING_MAX_REQUESTS, g_streaming.worker_count);
    return 0;
}

void streaming_shutdown(void)
{
    int i, t;

    if (g_streaming.work_sema >= 0)
        g_streaming.shutdown = 1;

    for (i = 0; i < g_streaming.worker_count; i++)
        SignalSema(g_streaming.work_sema);

    for (t = 0; t < 100; t++)
        platform_delay_us(1000);

    for (i = 0; i < g_streaming.worker_count; i++) {
        if (g_streaming.worker_ids[i] >= 0) {
            TerminateThread(g_streaming.worker_ids[i]);
            DeleteThread(g_streaming.worker_ids[i]);
            g_streaming.worker_ids[i] = -1;
        }
    }

    if (g_streaming.work_sema >= 0)
        DeleteSema(g_streaming.work_sema);
    if (g_streaming.lock_sema >= 0)
        DeleteSema(g_streaming.lock_sema);

    for (i = 0; i < STREAMING_WORKER_COUNT; i++) {
        if (g_streaming.worker_stacks[i]) {
            mem_free(g_streaming.worker_stacks[i], MEMTAG_THREAD_STACK);
            g_streaming.worker_stacks[i] = NULL;
        }
    }

    memset(&g_streaming, 0, sizeof(g_streaming));
    g_streaming.lock_sema = -1;
    g_streaming.work_sema = -1;
}

void streaming_update(void)
{
    if (!g_streaming.initialized)
        return;

    stream_dispatch_callbacks();
}

stream_handle_t streaming_request_file(const stream_request_desc_t *desc)
{
    int i;
    //int j;
    //int used_count = 0;
    stream_request_t *r;
    stream_handle_t h;

    if (!g_streaming.initialized || !desc)
        return stream_make_invalid_handle();

    if (!desc->path || !desc->dst || desc->size == 0)
        return stream_make_invalid_handle();

    stream_lock();

    for (i = 0; i < STREAMING_MAX_REQUESTS; i++) {
        if (!g_streaming.requests[i].used)
            break;
    }

    // for (j = 0; j < STREAMING_MAX_REQUESTS; j++) {
    //     if (g_streaming.requests[j].used)
    //         used_count++;
    // }
    // LOGLN("[streaming] used=%d/%d before request", used_count, STREAMING_MAX_REQUESTS);

    if (i >= STREAMING_MAX_REQUESTS) {
        //LOGLN("[streaming] NO FREE REQUEST SLOTS");
        stream_unlock();
        return stream_make_invalid_handle();
    }

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
    r->callback_pending = 0;
    r->callback_status = STREAM_STATUS_UNUSED;
    r->bytes_read = 0;
    r->result = 0;

    h.index = (u16)i;
    h.generation = r->generation;

    stream_unlock();

    LOGLN("[streaming] request index=%d gen=%u path=%s offset=%u size=%u priority=%d",
          i, h.generation, desc->path, desc->offset, desc->size, desc->priority);

    SignalSema(g_streaming.work_sema);
    return h;
}

void streaming_cancel(stream_handle_t handle)
{
    int index;

    stream_lock();

    index = stream_handle_to_index_unsafe(handle);
    if (index >= 0) {
        stream_request_t *r = &g_streaming.requests[index];

        if (r->status != STREAM_STATUS_READY &&
            r->status != STREAM_STATUS_FAILED &&
            r->status != STREAM_STATUS_CANCELLED) {
            stream_queue_callback_unsafe(r, STREAM_STATUS_CANCELLED, -5);
        }
    }

    stream_unlock();
}

void streaming_release(stream_handle_t handle)
{
    int index;
    u16 generation;

    stream_lock();

    index = stream_handle_to_index_unsafe(handle);
    if (index >= 0) {
        stream_request_t *r = &g_streaming.requests[index];

        if (r->status != STREAM_STATUS_READING) {
            generation = r->generation;
            memset(r, 0, sizeof(*r));
            r->generation = generation;
        }
    }

    stream_unlock();
}

stream_status_t streaming_status(stream_handle_t handle)
{
    int index;
    stream_status_t st = STREAM_STATUS_UNUSED;

    stream_lock();
    index = stream_handle_to_index_unsafe(handle);
    if (index >= 0)
        st = g_streaming.requests[index].status;
    stream_unlock();

    return st;
}

int streaming_bytes_read(stream_handle_t handle)
{
    int index;
    int bytes = 0;

    stream_lock();
    index = stream_handle_to_index_unsafe(handle);
    if (index >= 0)
        bytes = g_streaming.requests[index].bytes_read;
    stream_unlock();

    return bytes;
}

int streaming_is_valid(stream_handle_t handle)
{
    return streaming_status(handle) != STREAM_STATUS_UNUSED;
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