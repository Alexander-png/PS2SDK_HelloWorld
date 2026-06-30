#include "engine/memory/ring_buffer.h"

#include <string.h>

static u32 ring_buffer_min_u32(u32 a, u32 b)
{
    return (a < b) ? a : b;
}

int ring_buffer_init(ring_buffer_t *rb, void *buffer, u32 capacity)
{
    if (!rb || !buffer || capacity == 0)
        return -1;

    rb->data = (u8 *)buffer;
    rb->capacity = capacity;
    rb->read_pos = 0;
    rb->write_pos = 0;
    rb->size = 0;
    return 0;
}

void ring_buffer_reset(ring_buffer_t *rb)
{
    if (!rb)
        return;

    rb->read_pos = 0;
    rb->write_pos = 0;
    rb->size = 0;
}

u32 ring_buffer_size(const ring_buffer_t *rb)
{
    if (!rb)
        return 0;
    return rb->size;
}

u32 ring_buffer_free_space(const ring_buffer_t *rb)
{
    if (!rb || rb->capacity < rb->size)
        return 0;
    return rb->capacity - rb->size;
}

u32 ring_buffer_capacity(const ring_buffer_t *rb)
{
    if (!rb)
        return 0;
    return rb->capacity;
}

int ring_buffer_is_empty(const ring_buffer_t *rb)
{
    return ring_buffer_size(rb) == 0;
}

int ring_buffer_is_full(const ring_buffer_t *rb)
{
    if (!rb)
        return 0;
    return rb->size == rb->capacity;
}

u32 ring_buffer_write(ring_buffer_t *rb, const void *src, u32 bytes)
{
    const u8 *in;
    u32 writable;
    u32 first_part;
    u32 second_part;

    if (!rb || !rb->data || !src || bytes == 0 || rb->capacity == 0)
        return 0;

    writable = ring_buffer_min_u32(bytes, ring_buffer_free_space(rb));
    if (writable == 0)
        return 0;

    in = (const u8 *)src;

    first_part = ring_buffer_min_u32(writable, rb->capacity - rb->write_pos);
    second_part = writable - first_part;

    memcpy(rb->data + rb->write_pos, in, first_part);
    if (second_part > 0)
        memcpy(rb->data, in + first_part, second_part);

    rb->write_pos += first_part;
    if (rb->write_pos >= rb->capacity)
        rb->write_pos = 0;

    if (second_part > 0)
        rb->write_pos = second_part;

    rb->size += writable;
    return writable;
}

u32 ring_buffer_read(ring_buffer_t *rb, void *dst, u32 bytes)
{
    u8 *out;
    u32 readable;
    u32 first_part;
    u32 second_part;

    if (!rb || !rb->data || !dst || bytes == 0 || rb->capacity == 0)
        return 0;

    readable = ring_buffer_min_u32(bytes, rb->size);
    if (readable == 0)
        return 0;

    out = (u8 *)dst;

    first_part = ring_buffer_min_u32(readable, rb->capacity - rb->read_pos);
    second_part = readable - first_part;

    memcpy(out, rb->data + rb->read_pos, first_part);
    if (second_part > 0)
        memcpy(out + first_part, rb->data, second_part);

    rb->read_pos += first_part;
    if (rb->read_pos >= rb->capacity)
        rb->read_pos = 0;

    if (second_part > 0)
        rb->read_pos = second_part;

    rb->size -= readable;
    return readable;
}

u32 ring_buffer_peek(const ring_buffer_t *rb, void *dst, u32 bytes)
{
    u8 *out;
    u32 readable;
    u32 first_part;
    u32 second_part;

    if (!rb || !rb->data || !dst || bytes == 0 || rb->capacity == 0)
        return 0;

    readable = ring_buffer_min_u32(bytes, rb->size);
    if (readable == 0)
        return 0;

    out = (u8 *)dst;

    first_part = ring_buffer_min_u32(readable, rb->capacity - rb->read_pos);
    second_part = readable - first_part;

    memcpy(out, rb->data + rb->read_pos, first_part);
    if (second_part > 0)
        memcpy(out + first_part, rb->data, second_part);

    return readable;
}

u32 ring_buffer_peek_at(const ring_buffer_t *rb, u32 offset_bytes, void *out, u32 bytes)
{
    u32 available;
    u32 first_part;
    u32 pos;
    u8 *dst;

    if (!rb || !out || bytes == 0)
        return 0;

    if (offset_bytes >= rb->size)
        return 0;

    available = rb->size - offset_bytes;
    if (bytes > available)
        bytes = available;

    pos = (rb->read_pos + offset_bytes) % rb->capacity;
    dst = (u8 *)out;

    first_part = rb->capacity - pos;
    if (first_part > bytes)
        first_part = bytes;

    memcpy(dst, rb->data + pos, first_part);

    if (bytes > first_part)
        memcpy(dst + first_part, rb->data, bytes - first_part);

    return bytes;
}

u32 ring_buffer_skip(ring_buffer_t *rb, u32 bytes)
{
    u32 skip;

    if (!rb || rb->capacity == 0 || rb->size == 0 || bytes == 0)
        return 0;

    skip = bytes;
    if (skip > rb->size)
        skip = rb->size;

    rb->read_pos = (rb->read_pos + skip) % rb->capacity;
    rb->size -= skip;
    return skip;
}