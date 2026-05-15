#include "audio_stream_source.h"
#include "engine/logging/log.h"

#include <malloc.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#ifndef WAV_RIFF
#define WAV_RIFF 0x46464952u
#endif

#ifndef WAV_WAVE
#define WAV_WAVE 0x45564157u
#endif

#ifndef WAV_FMT
#define WAV_FMT  0x20746d66u
#endif

#ifndef WAV_DATA 
#define WAV_DATA 0x61746164u
#endif

#ifndef AUDIO_FRAME_BYTES
#define AUDIO_FRAME_BYTES (sizeof(s16) * 2)
#endif

static u32 rd32(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static u16 rd16(const u8 *p)
{
    return (u16)p[0] | ((u16)p[1] << 8);
}

static int parse_wav_header(audio_stream_source_t *src, int fd)
{
    u8 head[12];
    int found_fmt = 0;
    int found_data = 0;
    u16 audio_format = 0;

    if (lseek(fd, 0, SEEK_SET) < 0)
        return -2;

    if (read(fd, head, sizeof(head)) != (int)sizeof(head))
        return -3;

    if (rd32(head + 0) != WAV_RIFF || rd32(head + 8) != WAV_WAVE)
        return -4;

    while (1) {
        u8 chunk_hdr[8];
        u32 id, chunk_size;
        off_t chunk_data_pos;

        if (read(fd, chunk_hdr, sizeof(chunk_hdr)) != (int)sizeof(chunk_hdr))
            break;

        id = rd32(chunk_hdr + 0);
        chunk_size = rd32(chunk_hdr + 4);
        chunk_data_pos = lseek(fd, 0, SEEK_CUR);
        if (chunk_data_pos < 0)
            return -5;

        if (id == WAV_FMT) {
            u8 fmtbuf[40];

            if (chunk_size < 16)
                return -6;

            if (chunk_size > sizeof(fmtbuf)) {
                if (read(fd, fmtbuf, sizeof(fmtbuf)) != (int)sizeof(fmtbuf))
                    return -7;
                if (lseek(fd, chunk_data_pos + chunk_size + (chunk_size & 1), SEEK_SET) < 0)
                    return -8;
            } else {
                if (read(fd, fmtbuf, chunk_size) != (int)chunk_size)
                    return -7;
                if (chunk_size & 1) {
                    if (lseek(fd, 1, SEEK_CUR) < 0)
                        return -8;
                }
            }

            audio_format    = rd16(fmtbuf + 0);
            src->channels   = rd16(fmtbuf + 2);
            src->src_rate   = (int)rd32(fmtbuf + 4);
            src->bits       = rd16(fmtbuf + 14);
            found_fmt = 1;
        }
        else if (id == WAV_DATA) {
            src->data_offset = (u32)chunk_data_pos;
            src->data_size   = chunk_size;

            if (lseek(fd, chunk_data_pos + chunk_size + (chunk_size & 1), SEEK_SET) < 0)
                return -9;

            found_data = 1;
        }
        else {
            if (lseek(fd, chunk_data_pos + chunk_size + (chunk_size & 1), SEEK_SET) < 0)
                return -10;
        }

        if (found_fmt && found_data)
            break;
    }

    if (!found_fmt || !found_data)
        return -11;
    if (audio_format != 1)
        return -12;
    if (src->channels != 2)
        return -13;
    if (src->bits != 16)
        return -14;
    if (src->src_rate <= 0)
        return -15;

    src->total_frames = src->data_size / AUDIO_FRAME_BYTES;
    return 0;
}

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

typedef struct audio_chunk_request_userdata {
    audio_stream_source_t *src;
    audio_stream_chunk_t *chunk;
} audio_chunk_request_userdata_t;

static void audio_stream_chunk_callback(stream_handle_t handle,
                                        stream_status_t status,
                                        int bytes_read,
                                        void *userdata)
{
    audio_chunk_request_userdata_t *ud = (audio_chunk_request_userdata_t *)userdata;
    audio_stream_chunk_t *c;

    if (!ud || !ud->src || !ud->chunk)
        return;

    c = ud->chunk;
    c->in_flight = 0;

    if (status == STREAM_STATUS_READY && bytes_read > 0) {
        c->valid_bytes = (u32)bytes_read;
        c->frame_count = (u32)bytes_read / AUDIO_FRAME_BYTES;
        c->ready = (c->frame_count > 0) ? 1 : 0;
        c->failed = 0;
        ud->src->status = c->ready ? AUDIO_STREAM_SOURCE_STATUS_READY
                                   : AUDIO_STREAM_SOURCE_STATUS_FAILED;
    } else if (status == STREAM_STATUS_CANCELLED) {
        c->ready = 0;
        c->failed = 0;
    } else {
        c->ready = 0;
        c->failed = 1;
        ud->src->status = AUDIO_STREAM_SOURCE_STATUS_FAILED;
    }

    if (streaming_is_valid(handle))
        streaming_release(handle);

    c->req.index = 0xffffu;
    c->req.generation = 0;

    free(ud);
}

static int submit_chunk_request(audio_stream_source_t *src,
                                audio_stream_chunk_t *c,
                                u32 start_frame)
{
    stream_request_desc_t desc;
    u32 file_byte_offset;
    u32 max_bytes;
    audio_chunk_request_userdata_t *ud;

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

    ud = (audio_chunk_request_userdata_t *)malloc(sizeof(*ud));
    if (!ud) {
        c->in_flight = 0;
        return -3;
    }

    ud->src = src;
    ud->chunk = c;

    memset(&desc, 0, sizeof(desc));
    desc.path = src->path;
    desc.offset = c->file_offset;
    desc.size = max_bytes;
    desc.dst = c->data;
    desc.priority = STREAM_PRIORITY_HIGH;
    desc.callback = audio_stream_chunk_callback;
    desc.userdata = ud;

    c->req = streaming_request_file(&desc);
    if (!streaming_is_valid(c->req)) {
        c->in_flight = 0;
        free(ud);
        return -4;
    }

    src->status = AUDIO_STREAM_SOURCE_STATUS_STREAMING;
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
    int fd;
    int i;
    int rc;

    if (!src || !wav_path)
        return -1;

    memset(src, 0, sizeof(*src));
    strncpy(src->path, wav_path, sizeof(src->path) - 1);
    src->path[sizeof(src->path) - 1] = '\0';

    fd = open(src->path, O_RDONLY);
    if (fd < 0)
        return -2;

    rc = parse_wav_header(src, fd);
    close(fd);
    if (rc < 0)
        return rc;

    if (chunk_bytes == 0)
        chunk_bytes = 128 * 1024;
    chunk_bytes &= ~((u32)AUDIO_FRAME_BYTES - 1);
    if (chunk_bytes < 4096)
        chunk_bytes = 4096;

    src->chunk_frames = chunk_bytes / AUDIO_FRAME_BYTES;
    src->status = AUDIO_STREAM_SOURCE_STATUS_READY;

    for (i = 0; i < AUDIO_STREAM_SOURCE_MAX_CHUNKS; i++) {
        src->chunks[i].data = (u8 *)memalign(64, chunk_bytes);
        if (!src->chunks[i].data) {
            audio_stream_source_destroy(src);
            return -3;
        }

        src->chunks[i].capacity_bytes = chunk_bytes;
        src->chunks[i].req.index = 0xffffu;
        src->chunks[i].req.generation = 0;
    }

    src->used = 1;
    audio_stream_source_update(src, 0);
    return 0;
}

void audio_stream_source_destroy(audio_stream_source_t *src)
{
    int i;

    if (!src)
        return;

    for (i = 0; i < AUDIO_STREAM_SOURCE_MAX_CHUNKS; i++) {
        audio_stream_chunk_t *c = &src->chunks[i];

        if (c->in_flight && streaming_is_valid(c->req))
            streaming_cancel(c->req);

        if (streaming_is_valid(c->req))
            streaming_release(c->req);

        if (c->data)
            free(c->data);
    }

    memset(src, 0, sizeof(*src));
}

void audio_stream_source_update(audio_stream_source_t *src, u32 wanted_frame)
{
    if (!src || !src->used)
        return;

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