#include "engine/memory/memory_arena.h"
#include "engine/logging/log.h"

#include <string.h>
#include <limits.h>
#include <stdint.h>

static int mem_arena_tag_valid(mem_tag_t tag)
{
    return tag >= 0 && tag < MEMTAG_COUNT;
}

static uintptr_t mem_arena_align_up_ptr(uintptr_t value, u32 align)
{
    uintptr_t mask = (uintptr_t)(align - 1u);
    return (value + mask) & ~mask;
}

static void mem_arena_clear(mem_arena_t *arena)
{
    if (!arena)
        return;

    arena->base = NULL;
    arena->capacity = 0;
    arena->offset = 0;
    arena->peak = 0;
    arena->tag = MEMTAG_SYSTEM;
    arena->owns_memory = 0;
}

int mem_arena_init(mem_arena_t *arena, u32 capacity, mem_tag_t tag)
{
    void *buffer;

    if (!arena)
        return -1;

    mem_arena_clear(arena);

    if (capacity == 0) {
        LOGLN("[arena] init failed: zero capacity");
        return -1;
    }

    if (!mem_arena_tag_valid(tag)) {
        LOGLN("[arena] init failed: invalid tag=%d capacity=%u", (int)tag, capacity);
        return -1;
    }

    buffer = mem_alloc(capacity, 16, tag);
    if (!buffer) {
        LOGLN("[arena] init failed: alloc capacity=%u tag=%d", capacity, (int)tag);
        return -1;
    }

    arena->base = (unsigned char *)buffer;
    arena->capacity = capacity;
    arena->offset = 0;
    arena->peak = 0;
    arena->tag = tag;
    arena->owns_memory = 1;

    LOGLN("[arena] init capacity=%u tag=%d", capacity, (int)tag);
    return 0;
}

int mem_arena_init_from_buffer(mem_arena_t *arena, void *buffer, u32 capacity, mem_tag_t tag)
{
    if (!arena)
        return -1;

    mem_arena_clear(arena);

    if (!buffer || capacity == 0) {
        LOGLN("[arena] init_from_buffer failed: buffer=%p capacity=%u", buffer, capacity);
        return -1;
    }

    if (!mem_arena_tag_valid(tag)) {
        LOGLN("[arena] init_from_buffer failed: invalid tag=%d capacity=%u", (int)tag, capacity);
        return -1;
    }

    arena->base = (unsigned char *)buffer;
    arena->capacity = capacity;
    arena->offset = 0;
    arena->peak = 0;
    arena->tag = tag;
    arena->owns_memory = 0;

    LOGLN("[arena] init_from_buffer capacity=%u tag=%d", capacity, (int)tag);
    return 0;
}

void mem_arena_destroy(mem_arena_t *arena)
{
    unsigned char *base;
    u32 used;
    u32 peak;
    u32 capacity;
    mem_tag_t tag;
    int owns_memory;

    if (!arena)
        return;

    if (!arena->base && arena->capacity == 0)
        return;

    base = arena->base;
    used = arena->offset;
    peak = arena->peak;
    capacity = arena->capacity;
    tag = arena->tag;
    owns_memory = arena->owns_memory;

    if (owns_memory && base)
        mem_free(base, tag);

    mem_arena_clear(arena);

    LOGLN("[arena] destroy used=%u peak=%u capacity=%u tag=%d",
          used,
          peak,
          capacity,
          (int)tag);
}

void mem_arena_reset(mem_arena_t *arena)
{
    if (!arena || !arena->base)
        return;

    arena->offset = 0;
}

void *mem_arena_alloc(mem_arena_t *arena, u32 size, u32 align)
{
    uintptr_t base_addr;
    uintptr_t current_addr;
    uintptr_t aligned_addr;
    u32 aligned_offset;
    u32 end_offset;

    if (!arena || !arena->base || size == 0)
        return NULL;

    if (align == 0)
        align = (u32)sizeof(void *);

    if (align < (u32)sizeof(void *))
        align = (u32)sizeof(void *);

    if ((align & (align - 1u)) != 0) {
        LOGLN("[arena] alloc invalid align=%u", align);
        return NULL;
    }

    base_addr = (uintptr_t)arena->base;
    current_addr = base_addr + arena->offset;
    aligned_addr = mem_arena_align_up_ptr(current_addr, align);

    if (aligned_addr < base_addr)
        return NULL;

    if (aligned_addr - base_addr > UINT_MAX)
        return NULL;

    aligned_offset = (u32)(aligned_addr - base_addr);

    if (size > UINT_MAX - aligned_offset)
        return NULL;

    end_offset = aligned_offset + size;

    if (end_offset > arena->capacity) {
        LOGLN("[arena] alloc failed size=%u align=%u used=%u capacity=%u tag=%d",
              size, align, arena->offset, arena->capacity, (int)arena->tag);
        return NULL;
    }

    arena->offset = end_offset;
    if (arena->offset > arena->peak)
        arena->peak = arena->offset;

    return (void *)aligned_addr;
}

void *mem_arena_calloc(mem_arena_t *arena, u32 count, u32 size, u32 align)
{
    void *ptr;
    u32 total;

    if (count == 0 || size == 0)
        return NULL;

    if (count > UINT_MAX / size)
        return NULL;

    total = count * size;
    ptr = mem_arena_alloc(arena, total, align);
    if (!ptr)
        return NULL;

    memset(ptr, 0, total);
    return ptr;
}

mem_arena_mark_t mem_arena_get_mark(const mem_arena_t *arena)
{
    mem_arena_mark_t mark;

    mark.offset = 0;

    if (!arena)
        return mark;

    mark.offset = arena->offset;
    return mark;
}

void mem_arena_release(mem_arena_t *arena, mem_arena_mark_t mark)
{
    if (!arena || !arena->base)
        return;

    if (mark.offset > arena->offset) {
        LOGLN("[arena] release invalid mark=%u current=%u", mark.offset, arena->offset);
        return;
    }

    arena->offset = mark.offset;
}

u32 mem_arena_used(const mem_arena_t *arena)
{
    if (!arena)
        return 0;
    return arena->offset;
}

u32 mem_arena_remaining(const mem_arena_t *arena)
{
    if (!arena || arena->capacity < arena->offset)
        return 0;
    return arena->capacity - arena->offset;
}

u32 mem_arena_capacity(const mem_arena_t *arena)
{
    if (!arena)
        return 0;
    return arena->capacity;
}

u32 mem_arena_peak(const mem_arena_t *arena)
{
    if (!arena)
        return 0;
    return arena->peak;
}