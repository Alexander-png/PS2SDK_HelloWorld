#ifndef STREAMING_H
#define STREAMING_H

#include <tamtypes.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef STREAMING_MAX_REQUESTS
#define STREAMING_MAX_REQUESTS 16
#endif

/* One physical DVD/HDD backend: serialize file I/O by default. */
#ifndef STREAMING_WORKER_COUNT
#define STREAMING_WORKER_COUNT 1
#endif

#ifndef STREAMING_THREAD_PRIO
#define STREAMING_THREAD_PRIO 40
#endif

#ifndef STREAMING_THREAD_STACK_SIZE
#define STREAMING_THREAD_STACK_SIZE 0x2000
#endif

/* Must remain modest and 64-byte aligned for predictable I/O/copy behavior. */
#ifndef STREAMING_IO_CHUNK_BYTES
#define STREAMING_IO_CHUNK_BYTES 8 * 1024
#endif

typedef enum stream_priority {
    STREAM_PRIORITY_LOW = 0,
    STREAM_PRIORITY_NORMAL,
    STREAM_PRIORITY_HIGH,
    STREAM_PRIORITY_CRITICAL
} stream_priority_t;

typedef enum stream_status {
    STREAM_STATUS_UNUSED = 0,
    STREAM_STATUS_QUEUED,
    STREAM_STATUS_READING,
    STREAM_STATUS_READY,
    STREAM_STATUS_FAILED,
    STREAM_STATUS_CANCELLED
} stream_status_t;

typedef struct stream_handle {
    u16 index;
    u16 generation;
} stream_handle_t;

typedef void (*stream_callback_t)(stream_handle_t handle,
                                  stream_status_t status,
                                  int bytes_read,
                                  void *userdata);

typedef struct stream_request_desc {
    const char *path;
    u32 offset;
    u32 size;
    void *dst;
    stream_priority_t priority;
    stream_callback_t callback;
    void *userdata;
} stream_request_desc_t;

int  streaming_init(void);
void streaming_shutdown(void);
void streaming_update(void);

stream_handle_t streaming_request_file(const stream_request_desc_t *desc);
void streaming_cancel(stream_handle_t handle);
void streaming_release(stream_handle_t handle);

stream_status_t streaming_status(stream_handle_t handle);
int streaming_bytes_read(stream_handle_t handle);
int streaming_is_valid(stream_handle_t handle);
int streaming_is_complete(stream_handle_t handle);

/*
 * Atomically claims terminal completion for clients that poll it.
 *
 * Returns 1 only for READY, FAILED, or CANCELLED.
 * On success, the normal main-thread callback is suppressed.
 * This does not release the request; caller must call
 * streaming_release() after consuming dst/result.
 */
int streaming_take_completion(stream_handle_t handle,
                              stream_status_t *status,
                              int *bytes_read);

const char *streaming_status_name(stream_status_t status);

#ifdef __cplusplus
}
#endif

#endif /* STREAMING_H */