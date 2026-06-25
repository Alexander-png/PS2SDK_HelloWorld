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
} mem_stats_t;

int  memory_init(void);
void memory_shutdown(void);

/* alignment MUST be a power of two; align=0 → regular malloc */
void *mem_alloc(u32 size, u32 align, mem_tag_t tag);

/* convenient for PNG rows and other temporary data */
void *mem_calloc(u32 count, u32 size, u32 align, mem_tag_t tag);

void  mem_dump_stats(void); /* logs via sio/screen */

void  mem_get_stats(mem_stats_t *out);
void  mem_dump_stats(void); /* логгером в sio/screen */

#ifdef __cplusplus
}
#endif

#endif