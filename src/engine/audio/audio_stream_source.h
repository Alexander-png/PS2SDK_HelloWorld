#ifndef AUDIO_STREAM_SOURCE_H
#define AUDIO_STREAM_SOURCE_H

#include <tamtypes.h>
#include "engine/streaming/streaming.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef AUDIO_STREAM_SOURCE_MAX_CHUNKS
#define AUDIO_STREAM_SOURCE_MAX_CHUNKS 2
#endif

typedef enum audio_stream_source_status {
    AUDIO_STREAM_SOURCE_STATUS_UNUSED = 0,
    AUDIO_STREAM_SOURCE_STATUS_READY,
    AUDIO_STREAM_SOURCE_STATUS_STREAMING,
    AUDIO_STREAM_SOURCE_STATUS_EOF,
    AUDIO_STREAM_SOURCE_STATUS_FAILED
} audio_stream_source_status_t;

typedef struct audio_stream_chunk {
    u8 *data;
    u32 capacity_bytes;

    u32 file_offset;
    u32 valid_bytes;
    u32 start_frame;
    u32 frame_count;

    volatile int ready;
    volatile int in_flight;
    volatile int failed;

    stream_handle_t req;
} audio_stream_chunk_t;

typedef struct audio_stream_source {
    int used;
    char path[128];

    u32 data_offset;
    u32 data_size;
    u32 total_frames;

    int src_rate;
    int channels;
    int bits;

    u32 chunk_frames;
    audio_stream_source_status_t status;

    audio_stream_chunk_t chunks[AUDIO_STREAM_SOURCE_MAX_CHUNKS];
} audio_stream_source_t;

int  audio_stream_source_init(audio_stream_source_t *src,
                              const char *wav_path,
                              u32 chunk_bytes);

void audio_stream_source_destroy(audio_stream_source_t *src);
void audio_stream_source_update(audio_stream_source_t *src, u32 wanted_frame);

int  audio_stream_source_prewarm(audio_stream_source_t *src, u32 start_frame, int chunks_ahead);
int  audio_stream_source_has_frame(const audio_stream_source_t *src, u32 frame);
int  audio_stream_source_is_prefilled(const audio_stream_source_t *src, u32 start_frame, int chunks_needed);

int  audio_stream_source_get_frame_pair(audio_stream_source_t *src,
                                        u32 frame,
                                        s16 *l,
                                        s16 *r);

int  audio_stream_source_is_ready(const audio_stream_source_t *src);
u32  audio_stream_source_total_frames(const audio_stream_source_t *src);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_STREAM_SOURCE_H */