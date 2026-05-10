#include "audio_stream.h"

#include <audsrv.h>
#include <kernel.h>
#include <delaythread.h>
#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#ifndef AUDIO_STREAM_THREAD_PRIO
#define AUDIO_STREAM_THREAD_PRIO 32
#endif

#ifndef AUDIO_STREAM_STACK_SIZE
#define AUDIO_STREAM_STACK_SIZE 0x2000
#endif

#ifndef AUDIO_STREAM_IDLE_US
#define AUDIO_STREAM_IDLE_US 2000
#endif

#define WAV_RIFF 0x46464952u
#define WAV_WAVE 0x45564157u
#define WAV_FMT  0x20746d66u
#define WAV_DATA 0x61746164u
#define AUDIO_OUTPUT_RATE 48000
#define AUDIO_FRAME_BYTES (sizeof(s16) * 2)

static u32 rd32(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static u16 rd16(const u8 *p)
{
    return (u16)p[0] | ((u16)p[1] << 8);
}

static int parse_wav_header(audio_stream_t *s, int fd)
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

            audio_format = rd16(fmtbuf + 0);
            s->channels   = rd16(fmtbuf + 2);
            s->src_rate   = (int)rd32(fmtbuf + 4);
            s->bits       = rd16(fmtbuf + 14);
            found_fmt = 1;
        }
        else if (id == WAV_DATA) {
            s->data_offset = (u32)chunk_data_pos;
            s->data_size   = chunk_size;

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
    if (s->channels != 2)
        return -13;
    if (s->bits != 16)
        return -14;
    if (s->src_rate <= 0)
        return -15;

    s->total_frames = s->data_size / AUDIO_FRAME_BYTES;
    return 0;
}

static int refill_io_buf(audio_stream_t *s, u32 wanted_frame)
{
    int rd;
    u32 wanted_byte;
    u32 max_bytes;

    if (!s || s->fd < 0)
        return -1;
    if (wanted_frame >= s->total_frames)
        return -2;

    wanted_byte = wanted_frame * AUDIO_FRAME_BYTES;
    if (lseek(s->fd, (off_t)(s->data_offset + wanted_byte), SEEK_SET) < 0)
        return -3;

    max_bytes = s->data_size - wanted_byte;
    if (max_bytes > (u32)s->io_buf_size)
        max_bytes = (u32)s->io_buf_size;

    rd = read(s->fd, s->io_buf, max_bytes);
    if (rd <= 0)
        return -4;

    s->io_buf_start_frame = wanted_frame;
    s->io_buf_frames      = (u32)rd / AUDIO_FRAME_BYTES;
    return (s->io_buf_frames > 0) ? 0 : -5;
}

static int get_frame_pair(audio_stream_t *s, u32 frame, s16 *l, s16 *r)
{
    u32 rel;
    const s16 *src;
    int rc;

    if (frame >= s->total_frames)
        return -1;

    if (frame < s->io_buf_start_frame ||
        frame >= s->io_buf_start_frame + s->io_buf_frames) {

        rc = refill_io_buf(s, frame);
        if (rc < 0)
            return rc;
    }

    rel = frame - s->io_buf_start_frame;
    if (rel >= s->io_buf_frames)
        return -2;

    src = (const s16 *)s->io_buf;
    *l = src[rel * 2 + 0];
    *r = src[rel * 2 + 1];
    return 0;
}

static void fill_chunk(audio_stream_t *s, s16 *out, int out_frames)
{
    const double rate_ratio = (double)s->src_rate / (double)AUDIO_OUTPUT_RATE;
    int i;

    for (i = 0; i < out_frames; i++) {
        u32 f0, f1;
        float t;
        s16 l0, r0, l1, r1;
        s32 l, r;

        if (!s->playing || s->paused || s->total_frames == 0) {
            out[i * 2 + 0] = 0;
            out[i * 2 + 1] = 0;
            continue;
        }

        while (s->src_pos >= (double)s->total_frames) {
            if (s->loop) {
                s->src_pos -= (double)s->total_frames;
            } else {
                s->playing = 0;
                out[i * 2 + 0] = 0;
                out[i * 2 + 1] = 0;
                goto next_sample;
            }
        }

        if (s->src_pos < 0.0)
            s->src_pos = 0.0;

        f0 = (u32)s->src_pos;
        f1 = f0 + 1;
        if (f1 >= s->total_frames)
            f1 = s->loop ? 0 : f0;

        if (get_frame_pair(s, f0, &l0, &r0) < 0 ||
            get_frame_pair(s, f1, &l1, &r1) < 0) {
            s->playing = 0;
            out[i * 2 + 0] = 0;
            out[i * 2 + 1] = 0;
            goto next_sample;
        }

        t = (float)(s->src_pos - (double)f0);
        l = (s32)((1.0f - t) * l0 + t * l1);
        r = (s32)((1.0f - t) * r0 + t * r1);

        if (l < -32768) l = -32768;
        if (l >  32767) l =  32767;
        if (r < -32768) r = -32768;
        if (r >  32767) r =  32767;

        out[i * 2 + 0] = (s16)l;
        out[i * 2 + 1] = (s16)r;
        s->src_pos += (double)s->speed * rate_ratio;
        continue;

next_sample:
        ;
    }
}

static void audio_stream_thread(void *arg)
{
    audio_stream_t *s = (audio_stream_t *)arg;
    const int bytes = s->mixbuf_frames * 2 * (int)sizeof(s16);

    s->thread_running = 1;

    while (!s->thread_exit) {
        if (!s->playing) {
            DelayThread(AUDIO_STREAM_IDLE_US);
            continue;
        }

        if (s->paused) {
            memset(s->mixbuf, 0, bytes);
            audsrv_wait_audio(bytes);
            if (s->thread_exit)
                break;
            audsrv_play_audio((const char *)s->mixbuf, bytes);
            continue;
        }

        fill_chunk(s, s->mixbuf, s->mixbuf_frames);
        audsrv_wait_audio(bytes);
        if (s->thread_exit)
            break;
        audsrv_play_audio((const char *)s->mixbuf, bytes);
    }

    s->thread_running = 0;
    ExitDeleteThread();
}

int audio_stream_init(audio_stream_t *s, const char *wav_path,
                      int mixbuf_frames, int io_buf_bytes)
{
    int ret;
    ee_thread_t th;

    if (!s || !wav_path)
        return -1;

    memset(s, 0, sizeof(*s));
    s->fd        = -1;
    s->thread_id = -1;
    s->volume    = 100;
    s->speed     = 1.0f;

    s->fd = open(wav_path, O_RDONLY);
    if (s->fd < 0)
        return -2;

    ret = parse_wav_header(s, s->fd);
    if (ret < 0) {
        close(s->fd);
        s->fd = -1;
        return ret;
    }

    if (mixbuf_frames <= 0)
        mixbuf_frames = 1024;
    if (io_buf_bytes <= 0)
        io_buf_bytes = 64 * 1024;

    io_buf_bytes &= ~((int)AUDIO_FRAME_BYTES - 1);
    if (io_buf_bytes < 4096)
        io_buf_bytes = 4096;

    s->mixbuf = (s16 *)memalign(64,
                   (size_t)(mixbuf_frames * 2 * sizeof(s16)));
    if (!s->mixbuf) {
        close(s->fd);
        s->fd = -1;
        return -11;
    }
    s->mixbuf_frames = mixbuf_frames;

    s->io_buf = (u8 *)memalign(64, (size_t)io_buf_bytes);
    if (!s->io_buf) {
        free(s->mixbuf);
        close(s->fd);
        s->fd = -1;
        s->mixbuf = NULL;
        return -12;
    }
    s->io_buf_size       = io_buf_bytes;
    s->io_buf_start_frame = 0;
    s->io_buf_frames      = 0;

    s->thread_stack = memalign(16, AUDIO_STREAM_STACK_SIZE);
    if (!s->thread_stack) {
        free(s->io_buf);
        free(s->mixbuf);
        close(s->fd);
        memset(s, 0, sizeof(*s));
        s->fd = -1;
        s->thread_id = -1;
        return -13;
    }

    memset(&th, 0, sizeof(th));
    th.func             = audio_stream_thread;
    th.stack            = s->thread_stack;
    th.stack_size       = AUDIO_STREAM_STACK_SIZE;
    th.gp_reg           = &_gp;
    th.initial_priority = AUDIO_STREAM_THREAD_PRIO;

    s->thread_id = CreateThread(&th);
    if (s->thread_id < 0) {
        free(s->thread_stack);
        free(s->io_buf);
        free(s->mixbuf);
        close(s->fd);
        memset(s, 0, sizeof(*s));
        s->fd = -1;
        s->thread_id = -1;
        return -14;
    }

    StartThread(s->thread_id, s);
    audio_stream_set_volume(s, 100);
    return 0;
}

void audio_stream_destroy(audio_stream_t *s)
{
    int i;

    if (!s)
        return;

    audio_stream_stop(s);
    s->thread_exit = 1;
    audsrv_stop_audio();

    for (i = 0; i < 100 && s->thread_running; i++)
        DelayThread(1000);

    if (s->thread_running && s->thread_id >= 0) {
        TerminateThread(s->thread_id);
        DeleteThread(s->thread_id);
    }

    if (s->thread_stack)
        free(s->thread_stack);
    if (s->io_buf)
        free(s->io_buf);
    if (s->mixbuf)
        free(s->mixbuf);
    if (s->fd >= 0)
        close(s->fd);

    memset(s, 0, sizeof(*s));
    s->fd = -1;
    s->thread_id = -1;
}

int audio_stream_play(audio_stream_t *s, int loop)
{
    if (!s || s->fd < 0)
        return -1;

    s->loop    = loop ? 1 : 0;
    s->playing = 1;
    s->paused  = 0;
    return 0;
}

void audio_stream_pause(audio_stream_t *s)
{
    if (!s)
        return;

    s->paused = 1;
    audsrv_stop_audio();
}

void audio_stream_resume(audio_stream_t *s)
{
    if (!s)
        return;

    if (s->playing)
        s->paused = 0;
}

void audio_stream_stop(audio_stream_t *s)
{
    if (!s)
        return;

    s->playing = 0;
    s->paused  = 0;
    s->src_pos = 0.0;
    s->io_buf_start_frame = 0;
    s->io_buf_frames      = 0;
    audsrv_stop_audio();
}

void audio_stream_set_volume(audio_stream_t *s, int percent)
{
    int vol;

    if (!s)
        return;

    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    s->volume = percent;
    vol = (percent * 0x3fff) / 100;
    audsrv_set_volume(vol);
}

void audio_stream_set_speed(audio_stream_t *s, float speed)
{
    if (!s)
        return;

    if (speed < (1.0f / 3.0f)) speed = (1.0f / 3.0f);
    if (speed > 3.0f)          speed = 3.0f;
    s->speed = speed;
}

int audio_stream_is_playing(const audio_stream_t *s)
{
    return s ? s->playing : 0;
}

int audio_stream_is_paused(const audio_stream_t *s)
{
    return s ? s->paused : 0;
}