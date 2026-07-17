#include "engine/memory/memory.h"
#include "engine/logging/log.h"

#include <string.h>
#include <malloc.h>
#include <stddef.h>
#include <limits.h>
#include <stdint.h>

#ifndef MEMORY_BLOCK_MAGIC
#define MEMORY_BLOCK_MAGIC 0x4D454D31u /* 'MEM1' */
#endif

#ifndef MEMORY_BLOCK_FREED_MAGIC
#define MEMORY_BLOCK_FREED_MAGIC 0x46524545u /* 'FREE' */
#endif

#ifndef MEMORY_ALLOC_PATTERN
#define MEMORY_ALLOC_PATTERN 0xCD
#endif

#ifndef MEMORY_FREE_PATTERN
#define MEMORY_FREE_PATTERN 0xDD
#endif

#ifndef MEMORY_GUARD_PATTERN
#define MEMORY_GUARD_PATTERN 0xFD
#endif

#ifndef MEMORY_GUARD_SIZE
#define MEMORY_GUARD_SIZE 16u
#endif

typedef struct mem_block_header {
    u32 magic;
    u32 size;
    u32 tag;
    u32 alignment;
    void *base_ptr;
    void *user_ptr;
    struct mem_block_header *prev_live;
    struct mem_block_header *next_live;
} mem_block_header_t;

static mem_stats_t s_stats;
static mem_block_header_t *s_live_head = NULL;

static int mem_tag_valid(mem_tag_t tag)
{
    return tag >= 0 && tag < MEMTAG_COUNT;
}

static uintptr_t mem_align_up_ptr(uintptr_t value, u32 align)
{
    uintptr_t mask = (uintptr_t)(align - 1u);
    return (value + mask) & ~mask;
}

static const char *mem_tag_name(mem_tag_t tag)
{
    static const char *names[MEMTAG_COUNT] = {
        "SYSTEM",
        "THREAD_STACK",
        "STREAM",
        "RESOURCE",
        "AUDIO",
        "GFX",
        "TEMP",
        "STATE"
    };

    if (!mem_tag_valid(tag))
        return "INVALID";

    return names[tag];
}

static void mem_live_list_add(mem_block_header_t *hdr)
{
    hdr->prev_live = NULL;
    hdr->next_live = s_live_head;

    if (s_live_head)
        s_live_head->prev_live = hdr;

    s_live_head = hdr;
}

static void mem_live_list_remove(mem_block_header_t *hdr)
{
    if (hdr->prev_live)
        hdr->prev_live->next_live = hdr->next_live;
    else
        s_live_head = hdr->next_live;

    if (hdr->next_live)
        hdr->next_live->prev_live = hdr->prev_live;

    hdr->prev_live = NULL;
    hdr->next_live = NULL;
}

static unsigned char *mem_front_guard_ptr(const mem_block_header_t *hdr)
{
    return (unsigned char *)hdr->user_ptr - MEMORY_GUARD_SIZE;
}

static unsigned char *mem_back_guard_ptr(const mem_block_header_t *hdr)
{
    return (unsigned char *)hdr->user_ptr + hdr->size;
}

static int mem_check_guard_region(const unsigned char *p, u32 size)
{
    u32 i;
    for (i = 0; i < size; ++i) {
        if (p[i] != (unsigned char)MEMORY_GUARD_PATTERN)
            return 0;
    }
    return 1;
}

static int mem_validate_block(const mem_block_header_t *hdr)
{
    int front_ok;
    int back_ok;

    if (!hdr || hdr->magic != MEMORY_BLOCK_MAGIC)
        return 0;

    front_ok = mem_check_guard_region(mem_front_guard_ptr(hdr), MEMORY_GUARD_SIZE);
    back_ok  = mem_check_guard_region(mem_back_guard_ptr(hdr), MEMORY_GUARD_SIZE);

    if (!front_ok) {
        LOGLN("[memory] underrun ptr=%p size=%u tag=%s",
              hdr->user_ptr,
              hdr->size,
              mem_tag_name((mem_tag_t)hdr->tag));
    }

    if (!back_ok) {
        LOGLN("[memory] overrun ptr=%p size=%u tag=%s",
              hdr->user_ptr,
              hdr->size,
              mem_tag_name((mem_tag_t)hdr->tag));
    }

    return front_ok && back_ok;
}

static void mem_dump_leaks(void)
{
    mem_block_header_t *hdr;
    int leak_count = 0;
    u32 leak_bytes = 0;

    for (hdr = s_live_head; hdr; hdr = hdr->next_live) {
        int guards_ok = mem_validate_block(hdr);

        LOGLN("[memory] leak ptr=%p size=%u tag=%s align=%u guards=%s",
              hdr->user_ptr,
              hdr->size,
              mem_tag_name((mem_tag_t)hdr->tag),
              hdr->alignment,
              guards_ok ? "ok" : "corrupt");

        leak_count++;
        leak_bytes += hdr->size;
    }

    if (leak_count == 0)
        LOGLN("[memory] no leaks");
    else
        LOGLN("[memory] leaks total=%d bytes=%u", leak_count, leak_bytes);
}

static void mem_stats_on_alloc(mem_tag_t tag, u32 size)
{
    if (!mem_tag_valid(tag))
        return;

    s_stats.current[tag] += size;
    if (s_stats.current[tag] > s_stats.peak[tag])
        s_stats.peak[tag] = s_stats.current[tag];

    s_stats.total_current += size;
    if (s_stats.total_current > s_stats.total_peak)
        s_stats.total_peak = s_stats.total_current;

    s_stats.total_allocs[tag]++;
}

static void mem_stats_on_free(mem_tag_t tag, u32 size)
{
    if (!mem_tag_valid(tag))
        return;

    if (s_stats.current[tag] >= size)
        s_stats.current[tag] -= size;
    else
        s_stats.current[tag] = 0;

    if (s_stats.total_current >= size)
        s_stats.total_current -= size;
    else
        s_stats.total_current = 0;

    s_stats.total_frees[tag]++;
}

int memory_init(void)
{
    memset(&s_stats, 0, sizeof(s_stats));
    s_live_head = NULL;
    LOGLN("[memory] init");
    return 0;
}

void memory_shutdown(void)
{
    mem_dump_leaks();
    mem_dump_stats();
    LOGLN("[memory] shutdown");

    memset(&s_stats, 0, sizeof(s_stats));
    s_live_head = NULL;
}

void *mem_alloc(u32 size, u32 align, mem_tag_t tag)
{
    u32 effective_align;
    u32 total_size;
    unsigned char *base;
    unsigned char *raw;
    unsigned char *user_ptr;
    mem_block_header_t *hdr;

    if (size == 0)
        return NULL;

    if (!mem_tag_valid(tag)) {
        LOGLN("[memory] alloc invalid tag=%d size=%u", (int)tag, size);
        return NULL;
    }

    if (align == 0)
        align = (u32)sizeof(void *);

    if (align < (u32)sizeof(void *))
        align = (u32)sizeof(void *);

    if ((align & (align - 1u)) != 0) {
        LOGLN("[memory] alloc invalid alignment=%u size=%u tag=%s",
              align, size, mem_tag_name(tag));
        return NULL;
    }

    effective_align = align;

    if (size > UINT_MAX
             - (u32)sizeof(mem_block_header_t)
             - MEMORY_GUARD_SIZE
             - MEMORY_GUARD_SIZE
             - effective_align) {
        LOGLN("[memory] alloc overflow size=%u align=%u tag=%s",
              size, effective_align, mem_tag_name(tag));
        return NULL;
    }

    total_size = size
               + (u32)sizeof(mem_block_header_t)
               + MEMORY_GUARD_SIZE
               + MEMORY_GUARD_SIZE
               + effective_align;

    base = (unsigned char *)malloc(total_size);
    if (!base) {
        LOGLN("[memory] alloc failed size=%u align=%u tag=%s",
              size, effective_align, mem_tag_name(tag));
        return NULL;
    }

    raw = base + sizeof(mem_block_header_t) + MEMORY_GUARD_SIZE;
    user_ptr = (unsigned char *)mem_align_up_ptr((uintptr_t)raw, effective_align);
    hdr = (mem_block_header_t *)(user_ptr - MEMORY_GUARD_SIZE - sizeof(mem_block_header_t));

    hdr->magic = MEMORY_BLOCK_MAGIC;
    hdr->size = size;
    hdr->tag = (u32)tag;
    hdr->alignment = effective_align;
    hdr->base_ptr = base;
    hdr->user_ptr = user_ptr;
    hdr->prev_live = NULL;
    hdr->next_live = NULL;

    memset(mem_front_guard_ptr(hdr), MEMORY_GUARD_PATTERN, MEMORY_GUARD_SIZE);
    memset(mem_back_guard_ptr(hdr), MEMORY_GUARD_PATTERN, MEMORY_GUARD_SIZE);

    mem_live_list_add(hdr);
    memset(user_ptr, MEMORY_ALLOC_PATTERN, size);
    mem_stats_on_alloc(tag, size);

    return (void *)user_ptr;
}

void mem_free(void *ptr, mem_tag_t tag)
{
    mem_block_header_t *hdr;
    mem_tag_t real_tag;
    void *base_ptr;

    if (!ptr)
        return;

    hdr = (mem_block_header_t *)((unsigned char *)ptr - MEMORY_GUARD_SIZE - sizeof(mem_block_header_t));

    if (hdr->magic == MEMORY_BLOCK_FREED_MAGIC) {
        LOGLN("[memory] double free ptr=%p tag=%s",
              ptr, mem_tag_name(tag));
        return;
    }

    if (hdr->magic != MEMORY_BLOCK_MAGIC) {
        LOGLN("[memory] bad free ptr=%p tag=%s magic=%08x",
              ptr, mem_tag_name(tag), hdr->magic);
        return;
    }

    if (!mem_tag_valid((mem_tag_t)hdr->tag)) {
        LOGLN("[memory] bad header tag ptr=%p free_tag=%s header_tag=%u",
              ptr, mem_tag_name(tag), hdr->tag);
        return;
    }

    real_tag = (mem_tag_t)hdr->tag;

    if (real_tag != tag) {
        LOGLN("[memory] tag mismatch ptr=%p free_tag=%s header_tag=%s size=%u",
              ptr,
              mem_tag_name(tag),
              mem_tag_name(real_tag),
              hdr->size);
    }

    mem_validate_block(hdr);
    mem_live_list_remove(hdr);

    memset(ptr, MEMORY_FREE_PATTERN, hdr->size);
    memset(mem_front_guard_ptr(hdr), MEMORY_FREE_PATTERN, MEMORY_GUARD_SIZE);
    memset(mem_back_guard_ptr(hdr), MEMORY_FREE_PATTERN, MEMORY_GUARD_SIZE);

    mem_stats_on_free(real_tag, hdr->size);

    base_ptr = hdr->base_ptr;

    hdr->magic = MEMORY_BLOCK_FREED_MAGIC;
    hdr->size = 0;
    hdr->tag = 0xffffffffu;
    hdr->alignment = 0;
    hdr->base_ptr = NULL;
    hdr->user_ptr = NULL;
    hdr->prev_live = NULL;
    hdr->next_live = NULL;

    free(base_ptr);
}

void *mem_calloc(u32 count, u32 size, u32 align, mem_tag_t tag)
{
    void *ptr;
    u32 total;

    if (count == 0 || size == 0)
        return NULL;

    if (count > UINT_MAX / size) {
        LOGLN("[memory] calloc overflow count=%u size=%u tag=%s",
              count, size, mem_tag_name(tag));
        return NULL;
    }

    total = count * size;
    ptr = mem_alloc(total, align, tag);
    if (!ptr)
        return NULL;

    memset(ptr, 0, total);
    return ptr;
}

void mem_get_stats(mem_stats_t *out)
{
    if (!out)
        return;

    *out = s_stats;
}

void mem_dump_stats(void)
{
    int t;

    LOGLN("[memory] stats begin");
    for (t = 0; t < MEMTAG_COUNT; ++t) {
        LOGLN("[memory] tag=%s current=%u peak=%u allocs=%u frees=%u",
              mem_tag_name((mem_tag_t)t),
              s_stats.current[t],
              s_stats.peak[t],
              s_stats.total_allocs[t],
              s_stats.total_frees[t]);
    }
    LOGLN("[memory] stats end");
}

u32 mem_stats_total_current(void)
{
    u32 total = 0;
    int i;

    for (i = 0; i < MEMTAG_COUNT; ++i)
        total += s_stats.current[i];

    return total;
}

u32 mem_stats_total_peak(void)
{
    u32 total = 0;
    int i;

    for (i = 0; i < MEMTAG_COUNT; ++i)
        total += s_stats.peak[i];

    return total;
}