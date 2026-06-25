typedef enum mem_tag {
    MEMTAG_SYSTEM = 0,       /* logger, platform runtime */
    MEMTAG_THREAD_STACK,     /* streaming/audio thread stacks */
    MEMTAG_STREAM,           /* streaming requests, chunk userdata */
    MEMTAG_RESOURCE,         /* resource buffers (resource data) */
    MEMTAG_AUDIO,            /* mixer buffers + audio chunks */
    MEMTAG_GFX,              /* EE texture payloads, CLUT */
    MEMTAG_TEMP,             /* PNG rows, temporary working buffers */
    MEMTAG_STATE,            /* future state-local data */
    MEMTAG_COUNT
} mem_tag_t;