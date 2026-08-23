#include "engine/audio/audio_stream_source.h"
#include "engine/logging/log.h"
#include "engine/memory/memory.h"
#include "engine/audio/audio_wav.h"
#include "engine/platform/platform.h"

#include <string.h>

#ifndef AUDIO_FRAME_BYTES
#define AUDIO_FRAME_BYTES (sizeof(s16) * 2)
#endif

static int chunk_contains_frame(const audio_stream_chunk_t *c, u32 frame)
{
    if (!c->ready || c->frame_count == 0)
        return 0;
    return (frame >= c->start_frame &&
            frame < c->start_frame + c->frame_count);
}

static audio_stream_chunk_t *find_ready_chunk(audio_stream_source_t *src, u32 frame)
{
    int i;
    for (i = 0; i < AUDIO_STREAM_SOURCE_MAX_CHUNKS; i++) {
        if (chunk_contains_frame(&src->chunks[i], frame))
            return &src->chunks[i];
    }
    return NULL;
}

static int chunk_covers_start_frame(const audio_stream_chunk_t *c, u32 start_frame)
{
    if (c->in_flight && c->start_frame == start_frame)
        return 1;

    if (c->ready && c->frame_count > 0 && c->start_frame == start_frame)
        return 1;

    return 0;
}

static int source_has_chunk_for_start_frame(const audio_stream_source_t *src, u32 start_frame)
{
    int i;

    for (i = 0; i < AUDIO_STREAM_SOURCE_MAX_CHUNKS; i++) {
        if (chunk_covers_start_frame(&src->chunks[i], start_frame))
            return 1;
    }

    return 0;
}

static audio_stream_chunk_t *find_chunk_to_fill(audio_stream_source_t *src, u32 start_frame)
{
    int i;
    audio_stream_chunk_t *best = NULL;

    if (source_has_chunk_for_start_frame(src, start_frame))
        return NULL;

    for (i = 0; i < AUDIO_STREAM_SOURCE_MAX_CHUNKS; i++) {
        audio_stream_chunk_t *c = &src->chunks[i];

        if (c->in_flight)
            continue;

        if (!c->ready)
            return c;

        if (!best || c->start_frame < best->start_frame)
            best = c;
    }

    return best;
}

static stream_handle_t audio_stream_invalid_request(void)
{
    stream_handle_t h;

    h.index = 0xffffu;
    h.generation = 0;
    return h;
}

static void audio_stream_source_finish_chunk_request(audio_stream_source_t *src,
                                                      audio_stream_chunk_t *c,
                                                      stream_status_t status,
                                                      int bytes_read)
{
    c->in_flight = 0;

    if (status == STREAM_STATUS_READY && bytes_read > 0) {
        if ((u32)bytes_read > c->capacity_bytes)
            bytes_read = (int)c->capacity_bytes;

        bytes_read &= ~(AUDIO_FRAME_BYTES - 1);

        c->valid_bytes = (u32)bytes_read;
        c->frame_count = (u32)bytes_read / AUDIO_FRAME_BYTES;
        c->ready = c->frame_count > 0;
        c->failed = c->ready ? 0 : 1;

        src->status = c->ready
            ? AUDIO_STREAM_SOURCE_STATUS_READY
            : AUDIO_STREAM_SOURCE_STATUS_FAILED;

        LOGLNC(LOGCAT_STREAMING,
               "[audio:stream] chunk ready path=%s start=%u bytes=%d frames=%u",
               src->path,
               (unsigned int)c->start_frame,
               bytes_read,
               (unsigned int)c->frame_count);
    } else if (status == STREAM_STATUS_CANCELLED) {
        c->valid_bytes = 0;
        c->frame_count = 0;
        c->ready = 0;
        c->failed = 0;

        LOGLNC(LOGCAT_STREAMING,
               "[audio:stream] chunk cancelled path=%s start=%u",
               src->path,
               (unsigned int)c->start_frame);
    } else {
        c->valid_bytes = 0;
        c->frame_count = 0;
        c->ready = 0;
        c->failed = 1;
        src->status = AUDIO_STREAM_SOURCE_STATUS_FAILED;

        LOGLNC(LOGCAT_STREAMING,
               "[audio:stream] chunk failed path=%s start=%u status=%s bytes=%d",
               src->path,
               (unsigned int)c->start_frame,
               streaming_status_name(status),
               bytes_read);
    }

    c->req = audio_stream_invalid_request();
}

static void audio_stream_source_poll_completions(audio_stream_source_t *src)
{
    int i;

    if (!src || !src->used)
        return;

    for (i = 0; i < AUDIO_STREAM_SOURCE_MAX_CHUNKS; ++i) {
        audio_stream_chunk_t *c = &src->chunks[i];
        stream_handle_t completed_request;
        stream_status_t status;
        int bytes_read;

        if (!c->in_flight || !streaming_is_valid(c->req))
            continue;

        if (!streaming_take_completion(c->req, &status, &bytes_read))
            continue;

        /*
         * finish_chunk_request() invalidates c->req, поэтому сохраняем
         * current handle заранее.
         */
        completed_request = c->req;

        audio_stream_source_finish_chunk_request(src,
                                                  c,
                                                  status,
                                                  bytes_read);

        streaming_release(completed_request);
    }
}

static int submit_chunk_request(audio_stream_source_t *src,
                                audio_stream_chunk_t *c,
                                u32 start_frame)
{
    stream_request_desc_t desc;
    u32 file_byte_offset;
    u32 max_bytes;

    if (!src || !c)
        return -1;
    if (start_frame >= src->total_frames)
        return -2;

    file_byte_offset = start_frame * AUDIO_FRAME_BYTES;
    max_bytes = src->data_size - file_byte_offset;
    if (max_bytes > c->capacity_bytes)
        max_bytes = c->capacity_bytes;

    memset(c->data, 0, c->capacity_bytes);
    c->file_offset = src->data_offset + file_byte_offset;
    c->start_frame = start_frame;
    c->valid_bytes = 0;
    c->frame_count = 0;
    c->ready = 0;
    c->failed = 0;
    c->in_flight = 1;
    c->req.index = 0xffffu;
    c->req.generation = 0;

    memset(&desc, 0, sizeof(desc));
    desc.path = src->path;
    desc.offset = c->file_offset;
    desc.size = max_bytes;
    desc.dst = c->data;
    desc.priority = STREAM_PRIORITY_CRITICAL;
    desc.callback = NULL;
    desc.userdata = NULL;

    c->req = streaming_request_file(&desc);
    if (!streaming_is_valid(c->req)) {
        c->in_flight = 0;
        c->req = audio_stream_invalid_request();

        LOGLNC(LOGCAT_STREAMING,
               "[audio:stream] request failed path=%s offset=%u bytes=%u start=%u",
               src->path,
               (unsigned int)c->file_offset,
               (unsigned int)max_bytes,
               (unsigned int)start_frame);
        return -4;
    }

    src->status = AUDIO_STREAM_SOURCE_STATUS_STREAMING;
    LOGLNC(LOGCAT_STREAMING, "[audio:stream] request path=%s offset=%u bytes=%u start=%u",
        src->path,
        c->file_offset,
        max_bytes,
        start_frame);
    return 0;
}

static void maybe_queue_needed_chunks(audio_stream_source_t *src, u32 wanted_frame)
{
    u32 aligned;
    u32 next_start;
    audio_stream_chunk_t *c;

    if (!src || !src->used)
        return;

    if (wanted_frame >= src->total_frames) {
        src->status = AUDIO_STREAM_SOURCE_STATUS_EOF;
        return;
    }

    aligned = (wanted_frame / src->chunk_frames) * src->chunk_frames;

    if (!source_has_chunk_for_start_frame(src, aligned)) {
        c = find_chunk_to_fill(src, aligned);
        if (c)
            submit_chunk_request(src, c, aligned);
    }

    next_start = aligned + src->chunk_frames;
    if (next_start < src->total_frames &&
        !source_has_chunk_for_start_frame(src, next_start)) {
        c = find_chunk_to_fill(src, next_start);
        if (c)
            submit_chunk_request(src, c, next_start);
    }
}

int audio_stream_source_init(audio_stream_source_t *src,
                             const char *wav_path,
                             u32 chunk_bytes)
{
    int i;
    int rc;
    audio_wav_info_t info;

    if (!src || !wav_path)
        return -1;

    memset(src, 0, sizeof(*src));

    strncpy(src->path, wav_path, sizeof(src->path) - 1);
    src->path[sizeof(src->path) - 1] = '\0';

    memset(&info, 0, sizeof(info));
    rc = audio_wav_parse_file(src->path, &info);
    if (rc < 0) {
        LOGLNC(LOGCAT_STREAMING, "[audio:stream] wav parse failed rc=%d path=%s",
            rc,
            src->path);
            return rc;
    }

    src->data_offset  = info.data_offset;
    src->data_size    = info.data_size;
    src->total_frames = info.total_frames;
    src->src_rate     = info.src_rate;
    src->channels     = info.channels;
    src->bits         = info.bits;

    if (chunk_bytes == 0)
        chunk_bytes = 128 * 1024;

    chunk_bytes &= ~((u32)AUDIO_FRAME_BYTES - 1);
    if (chunk_bytes < 4096)
        chunk_bytes = 4096;

    src->chunk_frames = chunk_bytes / AUDIO_FRAME_BYTES;
    src->status = AUDIO_STREAM_SOURCE_STATUS_READY;

    for (i = 0; i < AUDIO_STREAM_SOURCE_MAX_CHUNKS; i++) {
        audio_stream_chunk_t *c = &src->chunks[i];

        c->data = (u8 *)mem_alloc(chunk_bytes, 64, MEMTAG_AUDIO);
        if (!c->data) {
            LOGLNC(LOGCAT_STREAMING, "[audio:stream] chunk buffer alloc failed path=%s chunk=%d bytes=%u",
                src->path,
                i,
                chunk_bytes);
            audio_stream_source_destroy(src);
            return -3;
        }

        c->capacity_bytes = chunk_bytes;
        c->file_offset = 0;
        c->valid_bytes = 0;
        c->start_frame = 0;
        c->frame_count = 0;
        c->ready = 0;
        c->in_flight = 0;
        c->failed = 0;
        c->req.index = 0xffffu;
        c->req.generation = 0;
    }

    src->used = 1;
    audio_stream_source_update(src, 0);
    LOGLNC(LOGCAT_STREAMING, "[audio:stream] source ready path=%s frames=%u chunk_bytes=%u chunk_frames=%u",
        src->path,
        src->total_frames,
        chunk_bytes,
        src->chunk_frames);
    return 0;
}

void audio_stream_source_destroy(audio_stream_source_t *src)
{
    int i;
    int wait;

    if (!src)
        return;

    for (i = 0; i < AUDIO_STREAM_SOURCE_MAX_CHUNKS; ++i) {
        audio_stream_chunk_t *c = &src->chunks[i];

        if (c->in_flight && streaming_is_valid(c->req))
            streaming_cancel(c->req);
    }

    for (wait = 0; wait < 200; ++wait) {
        int pending = 0;

        for (i = 0; i < AUDIO_STREAM_SOURCE_MAX_CHUNKS; ++i) {
            audio_stream_chunk_t *c = &src->chunks[i];
            stream_status_t status;
            int bytes_read;

            if (!c->in_flight || !streaming_is_valid(c->req))
                continue;

            if (!streaming_take_completion(c->req, &status, &bytes_read)) {
                pending = 1;
                continue;
            }

            {
                stream_handle_t completed_request = c->req;

                c->in_flight = 0;
                c->req = audio_stream_invalid_request();
                streaming_release(completed_request);
            }
        }

        if (!pending)
            break;

        platform_delay_us(1000);
    }

    for (i = 0; i < AUDIO_STREAM_SOURCE_MAX_CHUNKS; ++i) {
        audio_stream_chunk_t *c = &src->chunks[i];

        /* Must not free this source if a request remains active. */
        if (c->in_flight && streaming_is_valid(c->req)) {
            LOGLNC(LOGCAT_STREAMING,
                   "[audio:stream] destroy pending request path=%s start=%u",
                   src->path,
                   (unsigned int)c->start_frame);
            return;
        }
    }

    for (i = 0; i < AUDIO_STREAM_SOURCE_MAX_CHUNKS; ++i) {
        if (src->chunks[i].data)
            mem_free(src->chunks[i].data, MEMTAG_AUDIO);
    }

    LOGLNC(LOGCAT_STREAMING,
           "[audio:stream] source destroyed path=%s",
           src->path);
    memset(src, 0, sizeof(*src));
}

void audio_stream_source_update(audio_stream_source_t *src, u32 wanted_frame)
{
    if (!src || !src->used)
        return;

    audio_stream_source_poll_completions(src);
    maybe_queue_needed_chunks(src, wanted_frame);
}

int audio_stream_source_has_frame(const audio_stream_source_t *src, u32 frame)
{
    int i;

    if (!src || !src->used)
        return 0;
    if (frame >= src->total_frames)
        return 0;

    for (i = 0; i < AUDIO_STREAM_SOURCE_MAX_CHUNKS; i++) {
        const audio_stream_chunk_t *c = &src->chunks[i];

        if (!c->ready || c->frame_count == 0)
            continue;

        if (frame >= c->start_frame &&
            frame < c->start_frame + c->frame_count)
            return 1;
    }

    return 0;
}


int audio_stream_source_prewarm(audio_stream_source_t *src, u32 start_frame, int chunks_ahead)
{
    int i;
    u32 frame;

    if (!src || !src->used)
        return -1;
    if (start_frame >= src->total_frames)
        return -2;

    if (chunks_ahead < 1)
        chunks_ahead = 1;
    if (chunks_ahead > AUDIO_STREAM_SOURCE_MAX_CHUNKS)
        chunks_ahead = AUDIO_STREAM_SOURCE_MAX_CHUNKS;

    frame = start_frame;
    for (i = 0; i < chunks_ahead; i++) {
        audio_stream_source_update(src, frame);

        if (frame + src->chunk_frames >= src->total_frames)
            break;

        frame += src->chunk_frames;
    }

    return 0;
}


int audio_stream_source_is_prefilled(const audio_stream_source_t *src, u32 start_frame, int chunks_needed)
{
    int i;
    u32 frame;

    if (!src || !src->used)
        return 0;
    if (start_frame >= src->total_frames)
        return 0;

    if (chunks_needed < 1)
        chunks_needed = 1;
    if (chunks_needed > AUDIO_STREAM_SOURCE_MAX_CHUNKS)
        chunks_needed = AUDIO_STREAM_SOURCE_MAX_CHUNKS;

    frame = start_frame;
    for (i = 0; i < chunks_needed; i++) {
        if (!audio_stream_source_has_frame(src, frame))
            return 0;

        if (frame + src->chunk_frames >= src->total_frames)
            break;

        frame += src->chunk_frames;
    }

    return 1;
}

int audio_stream_source_get_frame_pair(audio_stream_source_t *src,
                                       u32 frame,
                                       s16 *l,
                                       s16 *r)
{
    audio_stream_chunk_t *c;
    u32 rel;
    const s16 *pcm;

    if (!src || !src->used || !l || !r)
        return -1;
    if (frame >= src->total_frames)
        return -2;

    c = find_ready_chunk(src, frame);
    if (!c) {
        audio_stream_source_update(src, frame);
        c = find_ready_chunk(src, frame);
        if (!c)
            return -3;
    }

    rel = frame - c->start_frame;
    if (rel >= c->frame_count)
        return -4;

    pcm = (const s16 *)c->data;
    *l = pcm[rel * 2 + 0];
    *r = pcm[rel * 2 + 1];
    return 0;
}

u32 audio_stream_source_read_frames(audio_stream_source_t *src,
                                    u32 start_frame,
                                    s16 *dst_interleaved,
                                    u32 max_frames)
{
    u32 total_copied_frames = 0;

    if (!src || !src->used || !dst_interleaved || max_frames == 0)
        return 0;

    if (start_frame >= src->total_frames)
        return 0;

    while (total_copied_frames < max_frames) {
        audio_stream_chunk_t *c;
        u32 frame;
        u32 rel_frame;
        u32 available_frames;
        u32 wanted_frames;
        u32 copy_frames;
        u32 copy_samples;

        frame = start_frame + total_copied_frames;
        if (frame >= src->total_frames)
            break;

        c = find_ready_chunk(src, frame);
        if (!c)
            break;

        rel_frame = frame - c->start_frame;
        if (rel_frame >= c->frame_count)
            break;

        available_frames = c->frame_count - rel_frame;
        wanted_frames = max_frames - total_copied_frames;
        copy_frames = (available_frames < wanted_frames) ? available_frames : wanted_frames;
        copy_samples = copy_frames * 2;

        memcpy(dst_interleaved + total_copied_frames * 2,
               ((const s16 *)c->data) + rel_frame * 2,
               copy_samples * sizeof(s16));

        total_copied_frames += copy_frames;
    }

    if (total_copied_frames == 0)
        audio_stream_source_update(src, start_frame);

    return total_copied_frames;
}

int audio_stream_source_is_ready(const audio_stream_source_t *src)
{
    if (!src || !src->used)
        return 0;
    return src->status == AUDIO_STREAM_SOURCE_STATUS_READY ||
           src->status == AUDIO_STREAM_SOURCE_STATUS_STREAMING;
}

u32 audio_stream_source_total_frames(const audio_stream_source_t *src)
{
    if (!src || !src->used)
        return 0;
    return src->total_frames;
}