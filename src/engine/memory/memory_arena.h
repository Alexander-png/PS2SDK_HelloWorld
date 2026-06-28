#ifndef MEMORY_ARENA_H
#define MEMORY_ARENA_H

#include "engine/memory/memory.h"

typedef struct mem_arena {
    unsigned char *base;
    u32 capacity;
    u32 offset;
    u32 peak;
    mem_tag_t tag;
    int owns_memory;
} mem_arena_t;

typedef struct mem_arena_mark {
    u32 offset;
} mem_arena_mark_t;

int mem_arena_init(mem_arena_t *arena, u32 capacity, mem_tag_t tag);
int mem_arena_init_from_buffer(mem_arena_t *arena, void *buffer, u32 capacity, mem_tag_t tag);

void mem_arena_destroy(mem_arena_t *arena);
void mem_arena_reset(mem_arena_t *arena);

void *mem_arena_alloc(mem_arena_t *arena, u32 size, u32 align);
void *mem_arena_calloc(mem_arena_t *arena, u32 count, u32 size, u32 align);

mem_arena_mark_t mem_arena_get_mark(const mem_arena_t *arena);
void mem_arena_release(mem_arena_t *arena, mem_arena_mark_t mark);

u32 mem_arena_used(const mem_arena_t *arena);
u32 mem_arena_remaining(const mem_arena_t *arena);
u32 mem_arena_capacity(const mem_arena_t *arena);
u32 mem_arena_peak(const mem_arena_t *arena);

#endif /* MEMORY_ARENA_H */