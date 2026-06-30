#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include <tamtypes.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ring_buffer {
    u8 *data;
    u32 capacity;
    u32 read_pos;
    u32 write_pos;
    u32 size;
} ring_buffer_t;

int  ring_buffer_init(ring_buffer_t *rb, void *buffer, u32 capacity);
void ring_buffer_reset(ring_buffer_t *rb);

u32  ring_buffer_size(const ring_buffer_t *rb);
u32  ring_buffer_free_space(const ring_buffer_t *rb);
u32  ring_buffer_capacity(const ring_buffer_t *rb);

int  ring_buffer_is_empty(const ring_buffer_t *rb);
int  ring_buffer_is_full(const ring_buffer_t *rb);

u32  ring_buffer_write(ring_buffer_t *rb, const void *src, u32 bytes);
u32  ring_buffer_read(ring_buffer_t *rb, void *dst, u32 bytes);
u32  ring_buffer_peek(const ring_buffer_t *rb, void *dst, u32 bytes);
u32  ring_buffer_peek_at(const ring_buffer_t *rb, u32 offset_bytes, void *out, u32 bytes);
u32  ring_buffer_skip(ring_buffer_t *rb, u32 bytes);

#ifdef __cplusplus
}
#endif

#endif /* RING_BUFFER_H */