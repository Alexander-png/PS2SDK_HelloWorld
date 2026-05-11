#include "audio_mixer_stream.h"
#include "engine/platform/platform.h"

#include <audsrv.h>
#include <kernel.h>
#include <malloc.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#ifndef AUDIO_MIXER_THREAD_PRIO
#define AUDIO_MIXER_THREAD_PRIO 32
#endif

#ifndef AUDIO_MIXER_STACK_SIZE
#define AUDIO_MIXER_STACK_SIZE 0x3000
#endif

#ifndef AUDIO_MIXER_IDLE_US
#define AUDIO_MIXER_IDLE_US 2000
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

static int parse_wav_header(audio_mix_stream_t *s, int fd)
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

static int refill_io_buf(audio_mix_stream_t *s, u32 wanted_frame)
{
    int rd;
    u32 wanted_byte;
    u32 max_bytes;
    u32 frames_per_buf;
    u32 aligned_frame;

    if (!s || s->fd < 0)
        return -1;
    if (wanted_frame >= s->total_frames)
        return -2;

    frames_per_buf = (u32)s->io_buf_size / AUDIO_FRAME_BYTES;
    if (frames_per_buf == 0)
        return -3;

    aligned_frame = (wanted_frame / frames_per_buf) * frames_per_buf;
    wanted_byte = aligned_frame * AUDIO_FRAME_BYTES;

    if (lseek(s->fd, (off_t)(s->data_offset + wanted_byte), SEEK_SET) < 0)
        return -4;

    max_bytes = s->data_size - wanted_byte;
    if (max_bytes > (u32)s->io_buf_size)
        max_bytes = (u32)s->io_buf_size;

    rd = read(s->fd, s->io_buf, max_bytes);
    if (rd <= 0)
        return -5;

    s->io_buf_start_frame = aligned_frame;
    s->io_buf_frames = (u32)rd / AUDIO_FRAME_BYTES;
    return (s->io_buf_frames > 0) ? 0 : -6;
}

static int get_frame_pair(audio_mix_stream_t *s, u32 frame, s16 *l, s16 *r)
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

static void mix_stream_into(audio_mixer_t *m, int handle, audio_mix_stream_t *s, s32 *accum_l, s32 *accum_r, int frames)
{
    const double rate_ratio = (double)s->src_rate / (double)AUDIO_OUTPUT_RATE;
    int i;
    int gain;

    if (!s->used || !s->playing || s->paused || s->total_frames == 0)
        return;

    gain = (s->volume * 256) / 100;

    for (i = 0; i < frames; i++) {
        u32 f0, f1;
        float t;
        s16 l0, r0, l1, r1;
        s32 l, r;

        while (s->src_pos >= (double)s->total_frames) {
            if (s->loop) {
                s->src_pos -= (double)s->total_frames;
            } else {
                s->playing = 0;
                if (!s->stopped_notified && s->on_stopped) {
                    s->stopped_notified = 1;
                    s->on_stopped(m, handle, s, s->callback_userdata);
                }
                goto done;
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
            if (!s->stopped_notified && s->on_stopped) {
                s->stopped_notified = 1;
                s->on_stopped(m, handle, s, s->callback_userdata);
            }
            goto done;
        }

        t = (float)(s->src_pos - (double)f0);
        l = (s32)((1.0f - t) * l0 + t * l1);
        r = (s32)((1.0f - t) * r0 + t * r1);

        accum_l[i] += (l * gain) >> 8;
        accum_r[i] += (r * gain) >> 8;

        s->src_pos += (double)s->speed * rate_ratio;
    }

done:
    ;
}

static void mixer_render(audio_mixer_t *m)
{
    int i, j;

    memset(m->accum_l, 0, m->mixbuf_frames * sizeof(s32));
    memset(m->accum_r, 0, m->mixbuf_frames * sizeof(s32));

    for (j = 0; j < AUDIO_MIXER_MAX_STREAMS; j++)
        mix_stream_into(m, j, &m->streams[j], m->accum_l, m->accum_r, m->mixbuf_frames);

    for (i = 0; i < m->mixbuf_frames; i++) {
        s32 l = m->accum_l[i];
        s32 r = m->accum_r[i];

        if (l < -32768) l = -32768;
        if (l >  32767) l =  32767;
        if (r < -32768) r = -32768;
        if (r >  32767) r =  32767;

        m->mixbuf[i * 2 + 0] = (s16)l;
        m->mixbuf[i * 2 + 1] = (s16)r;
    }
}

static int mixer_has_active_streams(const audio_mixer_t *m)
{
    int i;
    for (i = 0; i < AUDIO_MIXER_MAX_STREAMS; i++) {
        if (m->streams[i].used && m->streams[i].playing)
            return 1;
    }
    return 0;
}

static void audio_mixer_thread(void *arg)
{
    audio_mixer_t *m = (audio_mixer_t *)arg;
    const int bytes = m->mixbuf_frames * 2 * (int)sizeof(s16);

    m->thread_running = 1;
    m->output_active = 0;

    while (!m->thread_exit) {
        if (!mixer_has_active_streams(m)) {
            if (m->output_active) {
                audsrv_stop_audio();
                m->output_active = 0;
            }
            platform_delay_us(AUDIO_MIXER_IDLE_US);
            continue;
        }

        mixer_render(m);
        audsrv_wait_audio(bytes);
        if (m->thread_exit)
            break;
        audsrv_play_audio((const char *)m->mixbuf, bytes);
        m->output_active = 1;
    }

    m->thread_running = 0;
    ExitDeleteThread();
}

int audio_mixer_init(audio_mixer_t *m, int mixbuf_frames)
{
    ee_thread_t th;

    if (!m)
        return -1;

    memset(m, 0, sizeof(*m));
    m->thread_id = -1;

    if (mixbuf_frames <= 0)
        mixbuf_frames = 1024;
    m->mixbuf_frames = mixbuf_frames;

    m->mixbuf = (s16 *)memalign(64, (size_t)(mixbuf_frames * 2 * sizeof(s16)));
    if (!m->mixbuf)
        return -2;

    m->accum_l = (s32 *)memalign(64, (size_t)(mixbuf_frames * sizeof(s32)));
    if (!m->accum_l) {
        free(m->mixbuf);
        m->mixbuf = NULL;
        return -3;
    }

    m->accum_r = (s32 *)memalign(64, (size_t)(mixbuf_frames * sizeof(s32)));
    if (!m->accum_r) {
        free(m->accum_l);
        free(m->mixbuf);
        m->accum_l = NULL;
        m->mixbuf = NULL;
        return -4;
    }

    m->thread_stack = memalign(16, AUDIO_MIXER_STACK_SIZE);
    if (!m->thread_stack) {
        free(m->accum_r);
        free(m->accum_l);
        free(m->mixbuf);
        m->accum_r = NULL;
        m->accum_l = NULL;
        m->mixbuf = NULL;
        return -5;
    }

    memset(&th, 0, sizeof(th));
    th.func = audio_mixer_thread;
    th.stack = m->thread_stack;
    th.stack_size = AUDIO_MIXER_STACK_SIZE;
    th.gp_reg = &_gp;
    th.initial_priority = AUDIO_MIXER_THREAD_PRIO;

    m->thread_id = CreateThread(&th);
    if (m->thread_id < 0) {
        free(m->thread_stack);
        free(m->accum_r);
        free(m->accum_l);
        free(m->mixbuf);
        m->thread_stack = NULL;
        m->accum_r = NULL;
        m->accum_l = NULL;
        m->mixbuf = NULL;
        return -6;
    }

    StartThread(m->thread_id, m);
    return 0;
}

void audio_mixer_destroy(audio_mixer_t *m)
{
    int i, t;

    if (!m)
        return;

    m->thread_exit = 1;
    audsrv_stop_audio();

    for (t = 0; t < 100 && m->thread_running; t++)
        platform_delay_us(1000);

    if (m->thread_running && m->thread_id >= 0) {
        TerminateThread(m->thread_id);
        DeleteThread(m->thread_id);
    }

    for (i = 0; i < AUDIO_MIXER_MAX_STREAMS; i++)
        audio_mixer_remove_stream(m, i);

    if (m->thread_stack)
        free(m->thread_stack);
    if (m->accum_r)
        free(m->accum_r);
    if (m->accum_l)
        free(m->accum_l);
    if (m->mixbuf)
        free(m->mixbuf);

    memset(m, 0, sizeof(*m));
    m->thread_id = -1;
}

int audio_mixer_add_stream(audio_mixer_t *m, const char *wav_path, int io_buf_bytes)
{
    int i, ret;
    audio_mix_stream_t *s;

    if (!m || !wav_path)
        return -1;

    for (i = 0; i < AUDIO_MIXER_MAX_STREAMS; i++) {
        if (!m->streams[i].used)
            break;
    }
    if (i >= AUDIO_MIXER_MAX_STREAMS)
        return -2;

    s = &m->streams[i];
    memset(s, 0, sizeof(*s));
    s->fd = -1;
    s->volume = 100;
    s->speed = 1.0f;

    s->fd = open(wav_path, O_RDONLY);
    if (s->fd < 0)
        return -3;

    ret = parse_wav_header(s, s->fd);
    if (ret < 0) {
        close(s->fd);
        s->fd = -1;
        return ret;
    }

    if (io_buf_bytes <= 0)
        io_buf_bytes = 128 * 1024;
    io_buf_bytes &= ~((int)AUDIO_FRAME_BYTES - 1);
    if (io_buf_bytes < 4096)
        io_buf_bytes = 4096;

    s->io_buf = (u8 *)memalign(64, (size_t)io_buf_bytes);
    if (!s->io_buf) {
        close(s->fd);
        s->fd = -1;
        return -4;
    }

    s->io_buf_size = io_buf_bytes;
    s->io_buf_start_frame = 0;
    s->io_buf_frames = 0;
    s->used = 1;
    return i;
}

void audio_mixer_remove_stream(audio_mixer_t *m, int handle)
{
    audio_mix_stream_t *s;

    if (!m || handle < 0 || handle >= AUDIO_MIXER_MAX_STREAMS)
        return;

    s = &m->streams[handle];
    if (!s->used)
        return;

    s->playing = 0;
    s->paused = 0;

    if (s->io_buf)
        free(s->io_buf);
    if (s->fd >= 0)
        close(s->fd);

    memset(s, 0, sizeof(*s));
    s->fd = -1;
}

int audio_mixer_play(audio_mixer_t *m, int handle, int loop)
{
    audio_mix_stream_t *s;

    if (!m || handle < 0 || handle >= AUDIO_MIXER_MAX_STREAMS)
        return -1;

    s = &m->streams[handle];
    if (!s->used)
        return -2;

    s->loop = loop ? 1 : 0;
    s->paused = 0;
    s->playing = 1;
    s->src_pos = 0.0;
    s->io_buf_start_frame = 0;
    s->io_buf_frames = 0;

    s->started_notified = 0;
    s->stopped_notified = 0;

    if (!s->started_notified && s->on_started) {
        s->started_notified = 1;
        s->on_started(m, handle, s, s->callback_userdata);
    }

    return 0;
}

void audio_mixer_pause(audio_mixer_t *m, int handle)
{
    if (!m || handle < 0 || handle >= AUDIO_MIXER_MAX_STREAMS)
        return;
    if (m->streams[handle].used)
        m->streams[handle].paused = 1;
}

void audio_mixer_resume(audio_mixer_t *m, int handle)
{
    if (!m || handle < 0 || handle >= AUDIO_MIXER_MAX_STREAMS)
        return;
    if (m->streams[handle].used && m->streams[handle].playing)
        m->streams[handle].paused = 0;
}

void audio_mixer_stop(audio_mixer_t *m, int handle)
{
    audio_mix_stream_t *s;

    if (!m || handle < 0 || handle >= AUDIO_MIXER_MAX_STREAMS)
        return;

    s = &m->streams[handle];
    if (!s->used)
        return;

    if (s->playing) {
        s->playing = 0;
        s->paused = 0;
        s->src_pos = 0.0;
        s->io_buf_start_frame = 0;
        s->io_buf_frames = 0;

        if (!s->stopped_notified && s->on_stopped) {
            s->stopped_notified = 1;
            s->on_stopped(m, handle, s, s->callback_userdata);
        }
    }
}

void audio_mixer_set_volume(audio_mixer_t *m, int handle, int percent)
{
    audio_mix_stream_t *s;

    if (!m || handle < 0 || handle >= AUDIO_MIXER_MAX_STREAMS)
        return;

    s = &m->streams[handle];
    if (!s->used)
        return;

    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    s->volume = percent;
}

void audio_mixer_set_speed(audio_mixer_t *m, int handle, float speed)
{
    audio_mix_stream_t *s;

    if (!m || handle < 0 || handle >= AUDIO_MIXER_MAX_STREAMS)
        return;

    s = &m->streams[handle];
    if (!s->used)
        return;

    if (speed < (1.0f / 3.0f)) speed = (1.0f / 3.0f);
    if (speed > 3.0f) speed = 3.0f;
    s->speed = speed;
}

void audio_mixer_set_callbacks(audio_mixer_t *m, int handle,
                               audio_stream_callback_t on_started,
                               audio_stream_callback_t on_stopped,
                               void *userdata)
{
    audio_mix_stream_t *s;

    if (!m || handle < 0 || handle >= AUDIO_MIXER_MAX_STREAMS)
        return;

    s = &m->streams[handle];
    if (!s->used)
        return;

    s->on_started = on_started;
    s->on_stopped = on_stopped;
    s->callback_userdata = userdata;
}

int audio_mixer_is_playing(const audio_mixer_t *m, int handle)
{
    if (!m || handle < 0 || handle >= AUDIO_MIXER_MAX_STREAMS)
        return 0;
    return m->streams[handle].used ? m->streams[handle].playing : 0;
}

int audio_mixer_is_paused(const audio_mixer_t *m, int handle)
{
    if (!m || handle < 0 || handle >= AUDIO_MIXER_MAX_STREAMS)
        return 0;
    return m->streams[handle].used ? m->streams[handle].paused : 0;
}