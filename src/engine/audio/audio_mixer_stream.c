#include "audio_mixer_stream.h"
#include "engine/platform/platform.h"
#include "engine/memory/memory.h"

#include <audsrv.h>
#include <kernel.h>
#include <string.h>

#ifndef AUDIO_MIXER_THREAD_PRIO
#define AUDIO_MIXER_THREAD_PRIO 32
#endif

#ifndef AUDIO_MIXER_STACK_SIZE
#define AUDIO_MIXER_STACK_SIZE 0x3000
#endif

#ifndef AUDIO_MIXER_IDLE_US
#define AUDIO_MIXER_IDLE_US 2000
#endif

#define AUDIO_OUTPUT_RATE 48000

static void mix_stream_into(audio_mixer_t *m, int handle, audio_mix_stream_t *s, s32 *accum_l, s32 *accum_r, int frames)
{
    const double rate_ratio = (double)s->source.src_rate / (double)AUDIO_OUTPUT_RATE;
    int i;
    int gain;

    if (!s->used || !s->playing || s->paused || s->source.total_frames == 0)
        return;

    gain = (s->volume * 256) / 100;

    for (i = 0; i < frames; i++) {
        u32 f0, f1;
        float t;
        s16 l0, r0, l1, r1;
        s32 l, r;

        while (s->src_pos >= (double)s->source.total_frames) {
            if (s->loop) {
                s->src_pos -= (double)s->source.total_frames;
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
        if (f1 >= s->source.total_frames)
            f1 = s->loop ? 0 : f0;

        audio_stream_source_update(&s->source, f0);

        if (audio_stream_source_get_frame_pair(&s->source, f0, &l0, &r0) < 0 ||
            audio_stream_source_get_frame_pair(&s->source, f1, &l1, &r1) < 0) {
            l0 = r0 = l1 = r1 = 0;
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
            memset(m->mixbuf, 0, bytes);
            audsrv_wait_audio(bytes);
            if (m->thread_exit)
                break;
            audsrv_play_audio((const char *)m->mixbuf, bytes);
            m->output_active = 1;
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

    m->mixbuf = (s16 *)mem_alloc(mixbuf_frames * 2 * sizeof(s16),
                                 64, MEMTAG_AUDIO);
    if (!m->mixbuf)
        return -2;

    m->accum_l = (s32 *)mem_alloc(mixbuf_frames * sizeof(s32),
                                  64, MEMTAG_AUDIO);
    if (!m->accum_l) {
        mem_free(m->mixbuf, MEMTAG_AUDIO);
        m->mixbuf = NULL;
        return -3;
    }

    m->accum_r = (s32 *)mem_alloc(mixbuf_frames * sizeof(s32),
                                  64, MEMTAG_AUDIO);
    if (!m->accum_r) {
        mem_free(m->accum_l, MEMTAG_AUDIO);
        mem_free(m->mixbuf, MEMTAG_AUDIO);
        m->accum_l = NULL;
        m->mixbuf = NULL;
        return -4;
    }

    m->thread_stack = mem_alloc(AUDIO_MIXER_STACK_SIZE,
                                16, MEMTAG_THREAD_STACK);
    if (!m->thread_stack) {
        mem_free(m->accum_r, MEMTAG_AUDIO);
        mem_free(m->accum_l, MEMTAG_AUDIO);
        mem_free(m->mixbuf, MEMTAG_AUDIO);
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
        mem_free(m->thread_stack, MEMTAG_THREAD_STACK);
        mem_free(m->accum_r, MEMTAG_AUDIO);
        mem_free(m->accum_l, MEMTAG_AUDIO);
        mem_free(m->mixbuf, MEMTAG_AUDIO);
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
        mem_free(m->thread_stack, MEMTAG_THREAD_STACK);
    if (m->accum_r)
        mem_free(m->accum_r, MEMTAG_AUDIO);
    if (m->accum_l)
        mem_free(m->accum_l, MEMTAG_AUDIO);
    if (m->mixbuf)
        mem_free(m->mixbuf, MEMTAG_AUDIO);

    memset(m, 0, sizeof(*m));
    m->thread_id = -1;
}

int audio_mixer_add_stream(audio_mixer_t *m, const char *wav_path, int io_buf_bytes)
{
    int i, rc;
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
    s->volume = 100;
    s->speed = 1.0f;

    rc = audio_stream_source_init(&s->source, wav_path, (u32)((io_buf_bytes > 0) ? io_buf_bytes : 0));
    if (rc < 0)
        return rc;

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

    audio_stream_source_destroy(&s->source);

    memset(s, 0, sizeof(*s));
}

int audio_mixer_preload(audio_mixer_t *m, int handle)
{
    audio_mix_stream_t *s;

    if (!m || handle < 0 || handle >= AUDIO_MIXER_MAX_STREAMS)
        return -1;

    s = &m->streams[handle];
    if (!s->used)
        return -2;

    return audio_stream_source_prewarm(&s->source, 0, 2);
}

int audio_mixer_stream_is_ready(const audio_mixer_t *m, int handle)
{
    const audio_mix_stream_t *s;

    if (!m || handle < 0 || handle >= AUDIO_MIXER_MAX_STREAMS)
        return 0;

    s = &m->streams[handle];
    if (!s->used)
        return 0;

    return audio_stream_source_is_prefilled(&s->source, 0, 1);
}

int audio_mixer_play(audio_mixer_t *m, int handle, int loop)
{
    audio_mix_stream_t *s;

    if (!m || handle < 0 || handle >= AUDIO_MIXER_MAX_STREAMS)
        return -1;

    s = &m->streams[handle];
    if (!s->used)
        return -2;

    if (!audio_stream_source_is_ready(&s->source))
        return -3;

    s->loop = loop ? 1 : 0;
    s->paused = 0;
    s->playing = 1;
    s->src_pos = 0.0;

    audio_stream_source_update(&s->source, 0);

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