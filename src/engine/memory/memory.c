#include "engine/memory/memory.h"
#include "engine/logging/log.h"
#include <malloc.h>
#include <string.h>

static mem_stats_t s_stats;

int memory_init(void)
{
    memset(&s_stats, 0, sizeof(s_stats));
    LOGLN("[memory] init");
    return 0;
}

void memory_shutdown(void)
{
    mem_dump_stats();
    LOGLN("[memory] shutdown");
    memset(&s_stats, 0, sizeof(s_stats));
}

static void stats_add(mem_tag_t tag, s32 delta)
{
    if (tag < 0 || tag >= MEMTAG_COUNT)
        return;

    s_stats.current[tag] += delta;
    if (s_stats.current[tag] > s_stats.peak[tag])
        s_stats.peak[tag] = s_stats.current[tag];

    if (delta > 0)
        s_stats.total_allocs[tag]++;
    else if (delta < 0)
        s_stats.total_frees[tag]++;
}

void *mem_alloc(u32 size, u32 align, mem_tag_t tag)
{
    void *p;

    if (size == 0)
        return NULL;

    if (align && align > 1)
        p = memalign(align, size);
    else
        p = malloc(size);

    if (!p)
        return NULL;

    stats_add(tag, (s32)size);
    return p;
}

void mem_free(void *ptr, mem_tag_t tag)
{
    /* In the first version, we do not know the block size → we do not
       decrease current. This is fine: stats will be "up-only", but the
       mem_alloc/mem_free protocol is already unified. Later we can add
       our own header before the block to track size. */

    if (!ptr)
        return;

    free(ptr);
    stats_add(tag, 0); /* only alloc/free counters, without decreasing current */
}

void *mem_calloc(u32 count, u32 size, u32 align, mem_tag_t tag)
{
    u32 total = count * size;
    void *p = mem_alloc(total, align, tag);
    if (p)
        memset(p, 0, total);
    return p;
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
    static const char *names[MEMTAG_COUNT] = {
        "SYSTEM", "THREAD_STACK", "STREAM", "RESOURCE",
        "AUDIO", "GFX", "TEMP", "STATE"
    };

    LOGLN("[memory] stats:");
    for (t = 0; t < MEMTAG_COUNT; ++t) {
        LOGLN("[memory] tag=%s current=%u peak=%u allocs=%u frees=%u",
              names[t],
              s_stats.current[t],
              s_stats.peak[t],
              s_stats.total_allocs[t],
              s_stats.total_frees[t]);
    }
}