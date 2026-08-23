#include "engine/streaming/streaming.h"
#include "engine/logging/log.h"
#include "engine/platform/platform.h"
#include "engine/memory/memory.h"

#include <kernel.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#if (STREAMING_IO_CHUNK_BYTES <= 0)
#error "STREAMING_IO_CHUNK_BYTES must be positive"
#endif

#if ((STREAMING_IO_CHUNK_BYTES & 63) != 0)
#error "STREAMING_IO_CHUNK_BYTES must be 64-byte aligned"
#endif

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

    int fd;
    int worker_active;

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

    if (!r->used || r->generation != handle.generation)
        return -1;

    return (int)handle.index;
}

static int stream_status_is_terminal(stream_status_t status)
{
    return status == STREAM_STATUS_READY ||
           status == STREAM_STATUS_FAILED ||
           status == STREAM_STATUS_CANCELLED;
}

static void stream_close_request_fd_unsafe(stream_request_t *r)
{
    if (r->fd >= 0) {
        close(r->fd);
        r->fd = -1;
    }
}

/*
 * Publish a terminal outcome. This deliberately does not close fd: cancel
 * may race a worker blocked in read(), and only the worker may close its fd
 * after it has returned from that read.
 */
static void stream_queue_callback_unsafe(stream_request_t *r,
                                         stream_status_t status,
                                         int result)
{
    if (stream_status_is_terminal(r->status))
        return;

    r->status = status;
    r->result = result;
    r->callback_status = status;
    r->callback_pending = (r->callback != NULL) ? 1 : 0;
}

/* Called only after the worker no longer accesses the descriptor. */
static void stream_finish_request_unsafe(stream_request_t *r,
                                         stream_status_t status,
                                         int result)
{
    stream_close_request_fd_unsafe(r);
    stream_queue_callback_unsafe(r, status, result);
}

static void stream_release_worker_ownership_unsafe(stream_request_t *r)
{
    r->worker_active = 0;
}

static int stream_claim_request_unsafe(void)
{
    int i;
    int best = -1;
    int best_priority = -1;

    for (i = 0; i < STREAMING_MAX_REQUESTS; ++i) {
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
        g_streaming.requests[best].worker_active = 1;
    }

    return best;
}

static void stream_mark_failed(int index, u16 generation, int result)
{
    stream_lock();

    if (index >= 0 && index < STREAMING_MAX_REQUESTS &&
        g_streaming.requests[index].used &&
        g_streaming.requests[index].generation == generation) {
        stream_request_t *r = &g_streaming.requests[index];

        if (r->status == STREAM_STATUS_READING)
            stream_finish_request_unsafe(r, STREAM_STATUS_FAILED, result);

        stream_release_worker_ownership_unsafe(r);
    }

    stream_unlock();
}

/*
 * Reads one bounded chunk and then yields ownership back to the request
 * scheduler. Snapshot data is copied under lock; no request memory is read
 * while the lock is released for open/lseek/read.
 */
static void stream_process_one_chunk(int index)
{
    char path[sizeof(g_streaming.requests[0].path)];
    u16 generation;
    int fd;
    u32 offset;
    u32 size;
    int bytes_read;
    void *dst;
    int chunk;
    int rd;
    int needs_open;
    int need_seek;

    stream_lock();

    if (index < 0 || index >= STREAMING_MAX_REQUESTS ||
        !g_streaming.requests[index].used ||
        g_streaming.requests[index].status != STREAM_STATUS_READING ||
        !g_streaming.requests[index].worker_active) {
        stream_unlock();
        return;
    }

    {
        stream_request_t *r = &g_streaming.requests[index];

        generation = r->generation;
        fd = r->fd;
        offset = r->offset;
        size = r->size;
        bytes_read = r->bytes_read;
        dst = r->dst;
        memcpy(path, r->path, sizeof(path));
        path[sizeof(path) - 1] = '\0';

        if (!dst || size == 0 || bytes_read < 0 ||
            (u32)bytes_read > size || path[0] == '\0') {
            stream_finish_request_unsafe(r, STREAM_STATUS_FAILED, -1);
            stream_release_worker_ownership_unsafe(r);
            stream_unlock();
            return;
        }
    }

    needs_open = (fd < 0);
    need_seek = needs_open && offset != 0;
    stream_unlock();

    if (needs_open) {
        fd = open(path, O_RDONLY);
        if (fd < 0) {
            LOGLNC(LOGCAT_STREAMING, "[streaming] open failed: %s", path);
            stream_mark_failed(index, generation, -2);
            return;
        }

        if (need_seek && lseek(fd, (off_t)offset, SEEK_SET) < 0) {
            close(fd);
            LOGLNC(LOGCAT_STREAMING,
                   "[streaming] seek failed: %s offset=%u",
                   path,
                   (unsigned int)offset);
            stream_mark_failed(index, generation, -3);
            return;
        }

        stream_lock();

        if (!g_streaming.requests[index].used ||
            g_streaming.requests[index].generation != generation) {
            stream_unlock();
            close(fd);
            return;
        }

        {
            stream_request_t *r = &g_streaming.requests[index];

            if (r->status == STREAM_STATUS_CANCELLED) {
                stream_release_worker_ownership_unsafe(r);
                stream_unlock();
                close(fd);
                return;
            }

            if (r->status != STREAM_STATUS_READING || !r->worker_active) {
                stream_unlock();
                close(fd);
                return;
            }

            r->fd = fd;
        }

        stream_unlock();
    }

    chunk = (int)(size - (u32)bytes_read);
    if (chunk > STREAMING_IO_CHUNK_BYTES)
        chunk = STREAMING_IO_CHUNK_BYTES;

    if (chunk <= 0) {
        stream_lock();

        if (g_streaming.requests[index].used &&
            g_streaming.requests[index].generation == generation) {
            stream_request_t *r = &g_streaming.requests[index];

            if (r->status == STREAM_STATUS_READING) {
                stream_finish_request_unsafe(r,
                                             STREAM_STATUS_READY,
                                             bytes_read);
            }

            stream_release_worker_ownership_unsafe(r);
        }

        stream_unlock();
        return;
    }

    rd = read(fd, (u8 *)dst + bytes_read, chunk);

    stream_lock();

    if (!g_streaming.requests[index].used ||
        g_streaming.requests[index].generation != generation) {
        stream_unlock();
        return;
    }

    {
        stream_request_t *r = &g_streaming.requests[index];

        if (r->status == STREAM_STATUS_CANCELLED) {
            stream_close_request_fd_unsafe(r);
            stream_release_worker_ownership_unsafe(r);
            stream_unlock();
            return;
        }

        if (r->status != STREAM_STATUS_READING || !r->worker_active) {
            stream_release_worker_ownership_unsafe(r);
            stream_unlock();
            return;
        }

        if (rd <= 0) {
            LOGLNC(LOGCAT_STREAMING,
                   "[streaming] short read: %s got=%d wanted=%u",
                   path,
                   bytes_read,
                   (unsigned int)size);
            stream_finish_request_unsafe(r, STREAM_STATUS_FAILED, -4);
            stream_release_worker_ownership_unsafe(r);
            stream_unlock();
            return;
        }

        bytes_read += rd;
        r->bytes_read = bytes_read;

        if ((u32)bytes_read == size) {
            stream_finish_request_unsafe(r,
                                         STREAM_STATUS_READY,
                                         bytes_read);
            stream_release_worker_ownership_unsafe(r);
            stream_unlock();
            return;
        }

        if ((u32)bytes_read > size) {
            stream_finish_request_unsafe(r, STREAM_STATUS_FAILED, -5);
            stream_release_worker_ownership_unsafe(r);
            stream_unlock();
            return;
        }

        /*
         * Return this incomplete request to the scheduler. A higher-priority
         * request may be selected before its next chunk.
         */
        r->status = STREAM_STATUS_QUEUED;
        stream_release_worker_ownership_unsafe(r);
    }

    stream_unlock();

    /* Keep the queued request runnable for its next bounded read. */
    SignalSema(g_streaming.work_sema);
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
            stream_process_one_chunk(index);
    }

    ExitDeleteThread();
}

static void stream_dispatch_callbacks(void)
{
    int i;

    for (i = 0; i < STREAMING_MAX_REQUESTS; ++i) {
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

    for (i = 0; i < STREAMING_WORKER_COUNT; ++i) {
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

    for (i = 0; i < STREAMING_WORKER_COUNT; ++i) {
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
        ++g_streaming.worker_count;
    }

    g_streaming.initialized = 1;

    LOGLNC(LOGCAT_STREAMING,
           "[streaming] init max_requests=%d workers=%d chunk=%d",
           STREAMING_MAX_REQUESTS,
           g_streaming.worker_count,
           STREAMING_IO_CHUNK_BYTES);
    return 0;
}

void streaming_shutdown(void)
{
    int i;
    int t;

    if (g_streaming.work_sema >= 0)
        g_streaming.shutdown = 1;

    for (i = 0; i < g_streaming.worker_count; ++i)
        SignalSema(g_streaming.work_sema);

    for (t = 0; t < 100; ++t)
        platform_delay_us(1000);

    for (i = 0; i < g_streaming.worker_count; ++i) {
        if (g_streaming.worker_ids[i] >= 0) {
            TerminateThread(g_streaming.worker_ids[i]);
            DeleteThread(g_streaming.worker_ids[i]);
            g_streaming.worker_ids[i] = -1;
        }
    }

    if (g_streaming.lock_sema >= 0) {
        stream_lock();
        for (i = 0; i < STREAMING_MAX_REQUESTS; ++i)
            stream_close_request_fd_unsafe(&g_streaming.requests[i]);
        stream_unlock();
    }

    if (g_streaming.work_sema >= 0)
        DeleteSema(g_streaming.work_sema);
    if (g_streaming.lock_sema >= 0)
        DeleteSema(g_streaming.lock_sema);

    for (i = 0; i < STREAMING_WORKER_COUNT; ++i) {
        if (g_streaming.worker_stacks[i]) {
            mem_free(g_streaming.worker_stacks[i], MEMTAG_THREAD_STACK);
            g_streaming.worker_stacks[i] = NULL;
        }
    }

    LOGLNC(LOGCAT_STREAMING,
           "[streaming] shutdown workers=%d",
           g_streaming.worker_count);

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
    stream_request_t *r;
    stream_handle_t h;

    if (!g_streaming.initialized || !desc ||
        !desc->path || !desc->dst || desc->size == 0)
        return stream_make_invalid_handle();

    stream_lock();

    for (i = 0; i < STREAMING_MAX_REQUESTS; ++i) {
        if (!g_streaming.requests[i].used)
            break;
    }

    if (i >= STREAMING_MAX_REQUESTS) {
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
    r->fd = -1;
    r->worker_active = 0;
    strncpy(r->path, desc->path, sizeof(r->path) - 1);
    r->path[sizeof(r->path) - 1] = '\0';
    r->offset = desc->offset;
    r->size = desc->size;
    r->dst = desc->dst;
    r->priority = desc->priority;
    r->status = STREAM_STATUS_QUEUED;
    r->callback = desc->callback;
    r->userdata = desc->userdata;
    r->callback_status = STREAM_STATUS_UNUSED;

    h.index = (u16)i;
    h.generation = r->generation;

    stream_unlock();

    LOGLNC(LOGCAT_STREAMING,
           "[streaming] request index=%d gen=%u path=%s offset=%u size=%u priority=%d",
           i,
           (unsigned int)h.generation,
           desc->path,
           (unsigned int)desc->offset,
           (unsigned int)desc->size,
           (int)desc->priority);

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

        if (!stream_status_is_terminal(r->status))
            stream_queue_callback_unsafe(r, STREAM_STATUS_CANCELLED, -5);
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

        /* Never recycle a slot while the worker still has local snapshots. */
        if (!r->worker_active) {
            stream_close_request_fd_unsafe(r);
            generation = r->generation;
            memset(r, 0, sizeof(*r));
            r->generation = generation;
            r->fd = -1;
        }
    }

    stream_unlock();
}

stream_status_t streaming_status(stream_handle_t handle)
{
    int index;
    stream_status_t status = STREAM_STATUS_UNUSED;

    stream_lock();
    index = stream_handle_to_index_unsafe(handle);
    if (index >= 0)
        status = g_streaming.requests[index].status;
    stream_unlock();

    return status;
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

int streaming_is_complete(stream_handle_t handle)
{
    return stream_status_is_terminal(streaming_status(handle));
}

int streaming_take_completion(stream_handle_t handle,
                              stream_status_t *status,
                              int *bytes_read)
{
    int index;
    stream_request_t *r;

    if (status)
        *status = STREAM_STATUS_UNUSED;

    if (bytes_read)
        *bytes_read = 0;

    stream_lock();

    index = stream_handle_to_index_unsafe(handle);
    if (index < 0) {
        stream_unlock();
        return 0;
    }

    r = &g_streaming.requests[index];

    if (!stream_status_is_terminal(r->status)) {
        stream_unlock();
        return 0;
    }

    /* A polling client owns terminal processing from this point. */
    r->callback_pending = 0;

    if (status)
        *status = r->status;

    if (bytes_read)
        *bytes_read = r->bytes_read;

    stream_unlock();
    return 1;
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