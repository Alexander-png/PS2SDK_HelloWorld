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

#ifndef AUDIO_FRAME_BYTES
#define AUDIO_FRAME_BYTES (sizeof(s16) * 2)
#endif

#ifndef AUDIO_OUTPUT_RATE
#define AUDIO_OUTPUT_RATE 48000
#endif

#ifndef AUDIO_LOOP_XFADE_FRAMES
#define AUDIO_LOOP_XFADE_FRAMES 32
#endif

#ifndef AUDIO_START_MIN_READY_BYTES
#define AUDIO_START_MIN_READY_BYTES (AUDIO_FRAME_BYTES * 8)
#endif

#ifndef TAIL_FRAMES_TO_KEEP
#define TAIL_FRAMES_TO_KEEP 1
#endif

static void audio_mix_stream_reset_runtime(audio_mix_stream_t *s)
{
    if (!s)
        return;

    s->src_pos = 0.0;
    s->decode_frame = 0;
    s->eof_reached = 0;
    s->underrun_count = 0;
    s->underrun_count_logged = 0;
    s->play_cursor_frames = 0;
    s->play_frac = 0.0f;
    s->rb_base_frame = 0;
    s->startup_pending = 1;
    s->loop_fade_in_remaining = 0;
    s->loop_fade_in_total = 0;

    if (s->rb_storage && s->rb_capacity_bytes > 0)
        ring_buffer_reset(&s->rb);
}

static void audio_mix_stream_refill(audio_mix_stream_t *s)
{
    s16 temp[AUDIO_MIX_STREAM_REFILL_TEMP_FRAMES * 2];

    if (!s || !s->used)
        return;

    if (!audio_stream_source_is_ready(&s->source))
        return;

    while (ring_buffer_size(&s->rb) < s->rb_high_watermark_bytes) {
        u32 free_bytes;
        u32 free_frames;
        u32 frames_to_read;
        u32 got_frames;
        u32 bytes_to_write;
        u32 written_bytes;

        free_bytes = ring_buffer_free_space(&s->rb);
        if (free_bytes < AUDIO_FRAME_BYTES)
            break;

        free_frames = free_bytes / AUDIO_FRAME_BYTES;
        frames_to_read = free_frames;
        if (frames_to_read > AUDIO_MIX_STREAM_REFILL_TEMP_FRAMES)
            frames_to_read = AUDIO_MIX_STREAM_REFILL_TEMP_FRAMES;

        if (s->decode_frame >= s->source.total_frames) {
            if (s->loop) {
                s->decode_frame = 0;
            } else {
                s->eof_reached = 1;
                break;
            }
        }

        audio_stream_source_update(&s->source, s->decode_frame);

        got_frames = audio_stream_source_read_frames(&s->source,
                                                     s->decode_frame,
                                                     temp,
                                                     frames_to_read);
        if (got_frames == 0)
            break;

        bytes_to_write = got_frames * AUDIO_FRAME_BYTES;
        written_bytes = ring_buffer_write(&s->rb, temp, bytes_to_write);
        if (written_bytes == 0)
            break;

        s->decode_frame += written_bytes / AUDIO_FRAME_BYTES;

        if (written_bytes < bytes_to_write)
            break;
    }
}

static void mix_stream_into(audio_mixer_t *m, int handle,
                            audio_mix_stream_t *s,
                            s32 *accum_l, s32 *accum_r, int frames)
{
    const float rate_ratio = (float)s->source.src_rate / (float)AUDIO_OUTPUT_RATE;
    const float step = s->speed * rate_ratio;
    int gain;
    int i;

    if (!s->used || !s->playing || s->paused || s->source.total_frames == 0)
        return;

    gain = (s->volume * 256) / 100;

    for (i = 0; i < frames; i++) {
        u32 rb_frames;
        u32 local_index;
        u32 next_index;
        u32 byte_offset0, byte_offset1;
        s16 frame0[2];
        s16 frame1[2];
        u32 got0, got1;
        float t;
        s32 l, r;

        rb_frames = ring_buffer_size(&s->rb) / AUDIO_FRAME_BYTES;

        /* startup: как только FIFO хоть немного наполнился — считаем, что старт завершён */
        if (s->startup_pending && ring_buffer_size(&s->rb) >= AUDIO_START_MIN_READY_BYTES)
            s->startup_pending = 0;

        if (s->play_cursor_frames < s->rb_base_frame) {
            s->play_cursor_frames = s->rb_base_frame;
            s->play_frac = 0.0f;
        }

        local_index = s->play_cursor_frames - s->rb_base_frame;

        if (local_index >= rb_frames) {
            if (!s->startup_pending)
                s->underrun_count++;

            if (s->eof_reached && rb_frames == 0 && !s->loop) {
                s->playing = 0;
                s->paused = 0;
                s->play_cursor_frames = 0;
                s->play_frac = 0.0f;
                s->rb_base_frame = 0;

                if (!s->stopped_notified && s->on_stopped) {
                    s->stopped_notified = 1;
                    s->on_stopped(m, handle, s, s->callback_userdata);
                }
                return;
            }

            continue;
        }

        next_index = local_index + 1;
        if (next_index >= rb_frames) {
            if (s->eof_reached && !s->loop) {
                next_index = local_index;
            } else {
                if (!s->startup_pending)
                    s->underrun_count++;
                continue;
            }
        }

        byte_offset0 = local_index * AUDIO_FRAME_BYTES;
        byte_offset1 = next_index * AUDIO_FRAME_BYTES;

        got0 = ring_buffer_peek_at(&s->rb, byte_offset0, frame0, AUDIO_FRAME_BYTES);
        got1 = ring_buffer_peek_at(&s->rb, byte_offset1, frame1, AUDIO_FRAME_BYTES);

        if (got0 < AUDIO_FRAME_BYTES || got1 < AUDIO_FRAME_BYTES) {
            if (!s->startup_pending)
                s->underrun_count++;
            continue;
        }

        t = s->play_frac;
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;

        l = (s32)((1.0f - t) * (float)frame0[0] + t * (float)frame1[0]);
        r = (s32)((1.0f - t) * (float)frame0[1] + t * (float)frame1[1]);

        /* --- LOOP ANTI-CLICK FADE --- */
        {
            float env = 1.0f;
            u32 frames_left_to_end;

            if (s->play_cursor_frames < s->source.total_frames)
                frames_left_to_end = s->source.total_frames - s->play_cursor_frames;
            else
                frames_left_to_end = 0;

            /* fade-out на последних AUDIO_LOOP_XFADE_FRAMES перед концом */
            if (s->loop && frames_left_to_end <= AUDIO_LOOP_XFADE_FRAMES) {
                env = (float)frames_left_to_end / (float)AUDIO_LOOP_XFADE_FRAMES;
                if (env < 0.0f) env = 0.0f;
                if (env > 1.0f) env = 1.0f;
            }

            /* fade-in после wrap; уменьшаем env пока идёт fade-in */
            if (s->loop_fade_in_remaining > 0 && s->loop_fade_in_total > 0) {
                float in_env = 1.0f - ((float)s->loop_fade_in_remaining /
                                       (float)s->loop_fade_in_total);
                if (in_env < 0.0f) in_env = 0.0f;
                if (in_env > 1.0f) in_env = 1.0f;

                if (in_env < env)
                    env = in_env;
            }

            l = (s32)((float)l * env);
            r = (s32)((float)r * env);
        }
        /* --- END LOOP FADE --- */

        accum_l[i] += (l * gain) >> 8;
        accum_r[i] += (r * gain) >> 8;

        s->play_frac += step;
        while (s->play_frac >= 1.0f) {
            s->play_cursor_frames++;
            s->play_frac -= 1.0f;
        }

        while (s->play_frac < 0.0f) {
            if (s->play_cursor_frames > 0)
                s->play_cursor_frames--;
            s->play_frac += 1.0f;
        }

        if (s->loop_fade_in_remaining > 0)
            s->loop_fade_in_remaining--;

        if (s->play_cursor_frames >= s->source.total_frames) {
            if (s->loop) {
                s->play_cursor_frames = 0;
                s->play_frac = 0.0f;
                s->rb_base_frame = 0;

                /* подготовка fade-in для нового цикла */
                s->loop_fade_in_total = AUDIO_LOOP_XFADE_FRAMES;
                s->loop_fade_in_remaining = AUDIO_LOOP_XFADE_FRAMES;

                ring_buffer_reset(&s->rb);
                s->decode_frame = 0;
                s->eof_reached = 0;
                s->startup_pending = 1;

                audio_stream_source_prewarm(&s->source, 0, 2);
                audio_mix_stream_refill(s);
                audio_mix_stream_refill(s);
            } else if (s->eof_reached) {
                s->playing = 0;
                s->paused = 0;

                if (!s->stopped_notified && s->on_stopped) {
                    s->stopped_notified = 1;
                    s->on_stopped(m, handle, s, s->callback_userdata);
                }
                return;
            }
        }
    }

    if (s->play_cursor_frames > s->rb_base_frame) {
        u32 keep_tail_frames = TAIL_FRAMES_TO_KEEP;
        u32 consumed_frames = s->play_cursor_frames - s->rb_base_frame;

        if (consumed_frames > keep_tail_frames) {
            u32 skip_frames = consumed_frames - keep_tail_frames;
            u32 skip_bytes = skip_frames * AUDIO_FRAME_BYTES;
            u32 skipped = ring_buffer_skip(&s->rb, skip_bytes);
            u32 skipped_frames = skipped / AUDIO_FRAME_BYTES;
            s->rb_base_frame += skipped_frames;
        }
    }
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
        int j;

        if (!mixer_has_active_streams(m)) {
            memset(m->mixbuf, 0, bytes);
            audsrv_wait_audio(bytes);
            if (m->thread_exit)
                break;
            audsrv_play_audio((const char *)m->mixbuf, bytes);
            m->output_active = 1;
            continue;
        }

        for (j = 0; j < AUDIO_MIXER_MAX_STREAMS; j++) {
            audio_mix_stream_t *s = &m->streams[j];

            if (s->used && s->playing && !s->paused)
                audio_mix_stream_refill(s);

            if (s->used && s->underrun_count != s->underrun_count_logged) {
                s->underrun_count_logged = s->underrun_count;

                LOGLN("[audio:mixer] stream=%d underruns=%u eof=%d rb_size=%u rb_free=%u playing=%d paused=%d",
                    j,
                    s->underrun_count,
                    s->eof_reached,
                    ring_buffer_size(&s->rb),
                    ring_buffer_free_space(&s->rb),
                    s->playing,
                    s->paused);
            }
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

    s->rb_capacity_bytes = AUDIO_MIX_STREAM_RING_BUFFER_BYTES;
    s->rb_storage = (u8 *)mem_alloc(s->rb_capacity_bytes, 64, MEMTAG_AUDIO);
    if (!s->rb_storage) {
        audio_stream_source_destroy(&s->source);
        memset(s, 0, sizeof(*s));
        return -4;
    }

    if (ring_buffer_init(&s->rb, s->rb_storage, s->rb_capacity_bytes) < 0) {
        mem_free(s->rb_storage, MEMTAG_AUDIO);
        audio_stream_source_destroy(&s->source);
        memset(s, 0, sizeof(*s));
        return -5;
    }

    s->rb_low_watermark_bytes = s->rb_capacity_bytes / 4;
    s->rb_high_watermark_bytes = (s->rb_capacity_bytes * 3) / 4;
    audio_mix_stream_reset_runtime(s);

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

    if (s->rb_storage)
        mem_free(s->rb_storage, MEMTAG_AUDIO);
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

    int rc = audio_stream_source_prewarm(&s->source, 0, 2);
    if (rc < 0)
        return rc;

    audio_mix_stream_reset_runtime(s);
    audio_mix_stream_refill(s);
    return 0;
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
    s->src_pos = 0.0;

    audio_mix_stream_reset_runtime(s);
    audio_stream_source_prewarm(&s->source, 0, 2);
    audio_mix_stream_refill(s);
    // give a chance to fill FIFO. If source ready, this call is often free
    audio_mix_stream_refill(s); 

    s->playing = 1;

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

        audio_mix_stream_reset_runtime(s);

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