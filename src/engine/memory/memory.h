#ifndef MEMORY_H
#define MEMORY_H

#include <tamtypes.h>
#include "engine/memory/memory_tags.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mem_stats {
    u32 current[MEMTAG_COUNT];
    u32 peak[MEMTAG_COUNT];
    u32 total_allocs[MEMTAG_COUNT];
    u32 total_frees[MEMTAG_COUNT];

    u32 total_current;
    u32 total_peak;
} mem_stats_t;

int  memory_init(void);
void memory_shutdown(void);

/* alignment MUST be a power of two; align=0 → regular malloc */
void *mem_alloc(u32 size, u32 align, mem_tag_t tag);
/* tag is checked against stored header tag */
void  mem_free(void *ptr, mem_tag_t tag);

/* convenient for PNG rows and other temporary data */
void *mem_calloc(u32 count, u32 size, u32 align, mem_tag_t tag);

void  mem_get_stats(mem_stats_t *out);
void  mem_dump_stats(void); /* into sio/screen via logger*/

u32 mem_stats_total_current(void);
u32 mem_stats_total_peak(void);

#ifdef __cplusplus
}
#endif

#endif