#include "audio_mixer.h"
#include "audio_driver.h"
#include "engine/platform/platform.h"
#include "engine/memory/memory.h"
#include "engine/logging/log.h"
#include "engine/audio/audio_sfx_bank.h"

#include <audsrv.h>
#include <kernel.h>
#include <string.h>

#ifndef AUDIO_MIXER_THREAD_PRIO
#define AUDIO_MIXER_THREAD_PRIO 32
#endif

#ifndef AUDIO_MIXER_STACK_SIZE
#define AUDIO_MIXER_STACK_SIZE 0x3000
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

#ifndef AUDIO_STREAM_RING_BUFFER_BYTES
#define AUDIO_STREAM_RING_BUFFER_BYTES (64 * 1024)
#endif

#ifndef AUDIO_STREAM_REFILL_TEMP_FRAMES
#define AUDIO_STREAM_REFILL_TEMP_FRAMES 1024
#endif

#ifndef TAIL_FRAMES_TO_KEEP
#define TAIL_FRAMES_TO_KEEP 1
#endif

static int mixer_stream_index_from_handle(int asset_handle)
{
    return asset_handle - AUDIO_ASSET_HANDLE_STREAM_BASE;
}

static int mixer_sfx_index_from_handle(int asset_handle)
{
    return asset_handle - AUDIO_ASSET_HANDLE_SFX_BASE;
}

static int mixer_is_stream_handle(int asset_handle)
{
    return asset_handle >= AUDIO_ASSET_HANDLE_STREAM_BASE &&
           asset_handle < AUDIO_ASSET_HANDLE_STREAM_BASE + AUDIO_MIXER_MAX_STREAM_ASSETS;
}

static int mixer_is_sfx_handle(int asset_handle)
{
    return asset_handle >= AUDIO_ASSET_HANDLE_SFX_BASE &&
           asset_handle < AUDIO_ASSET_HANDLE_SFX_BASE + AUDIO_MIXER_MAX_SFX_ASSETS;
}

static int mixer_is_valid_stream_asset_index(const audio_mixer_t *m, int idx)
{
    if (!m || idx < 0 || idx >= AUDIO_MIXER_MAX_STREAM_ASSETS)
        return 0;
    return m->stream_assets[idx].used && !m->stream_assets[idx].closing;
}

static int mixer_is_valid_sfx_asset_index(const audio_mixer_t *m, int idx)
{
    if (!m || idx < 0 || idx >= AUDIO_MIXER_MAX_SFX_ASSETS)
        return 0;
    return m->sfx_assets[idx].used && !m->sfx_assets[idx].closing;
}

static int mixer_is_valid_voice(const audio_mixer_t *m, int voice_handle)
{
    if (!m || voice_handle < 0 || voice_handle >= AUDIO_MIXER_MAX_VOICES)
        return 0;
    return m->voices[voice_handle].used;
}

static void audio_mixer_release_stream_voice_resources(audio_voice_t *v)
{
    audio_stream_voice_state_t *st;

    if (!v || v->kind != AUDIO_VOICE_KIND_STREAM)
        return;

    st = &v->u.stream;

    if (st->rb_storage) {
        mem_free(st->rb_storage, MEMTAG_AUDIO);
        st->rb_storage = NULL;
    }

    memset(&v->u.stream, 0, sizeof(v->u.stream));
    v->u.stream.asset_handle = -1;
}

static int audio_mixer_find_free_voice(const audio_mixer_t *m)
{
    int i;
    for (i = 0; i < AUDIO_MIXER_MAX_VOICES; i++) {
        if (!m->voices[i].used)
            return i;
    }
    return -1;
}

static void audio_mixer_notify_started(audio_mixer_t *m, int voice_handle, audio_voice_t *v)
{
    if (!v->started_notified && v->on_started) {
        v->started_notified = 1;
        v->on_started(m, voice_handle, v, v->callback_userdata);
    }
}

static void audio_mixer_notify_stopped(audio_mixer_t *m, int voice_handle, audio_voice_t *v)
{
    if (!v->stopped_notified && v->on_stopped) {
        v->stopped_notified = 1;
        v->on_stopped(m, voice_handle, v, v->callback_userdata);
    }
}

static void audio_mixer_finish_voice(audio_mixer_t *m, int voice_handle)
{
    audio_voice_t *v;
    int stream_idx = -1;

    if (!mixer_is_valid_voice(m, voice_handle))
        return;

    v = &m->voices[voice_handle];

    if (v->kind == AUDIO_VOICE_KIND_STREAM)
        stream_idx = v->u.stream.asset_handle;

    v->playing = 0;
    v->paused = 0;

    if (stream_idx >= 0 &&
        mixer_is_valid_stream_asset_index(m, stream_idx) &&
        m->stream_assets[stream_idx].bound_voice == voice_handle) {
        m->stream_assets[stream_idx].bound_voice = -1;
    }

    audio_mixer_notify_stopped(m, voice_handle, v);

    if (v->kind == AUDIO_VOICE_KIND_STREAM)
        audio_mixer_release_stream_voice_resources(v);

    audio_voice_clear(v);
}

static int audio_mixer_init_stream_voice(audio_mixer_t *m, int voice_handle, int stream_idx)
{
    audio_voice_t *v;
    audio_stream_voice_state_t *st;

    if (!mixer_is_valid_voice(m, voice_handle))
        return -1;
    if (!mixer_is_valid_stream_asset_index(m, stream_idx))
        return -2;

    v = &m->voices[voice_handle];
    if (v->kind != AUDIO_VOICE_KIND_STREAM)
        return -3;

    st = &v->u.stream;
    st->asset_handle = stream_idx;
    st->rb_capacity_bytes = AUDIO_STREAM_RING_BUFFER_BYTES;
    st->rb_storage = (u8 *)mem_alloc(st->rb_capacity_bytes, 64, MEMTAG_AUDIO);
    if (!st->rb_storage)
        return -4;

    if (ring_buffer_init(&st->rb, st->rb_storage, st->rb_capacity_bytes) < 0) {
        mem_free(st->rb_storage, MEMTAG_AUDIO);
        st->rb_storage = NULL;
        st->asset_handle = -1;
        return -5;
    }

    st->rb_low_watermark_bytes = st->rb_capacity_bytes / 4;
    st->rb_high_watermark_bytes = (st->rb_capacity_bytes * 3) / 4;

    audio_voice_stream_reset_runtime(v);
    return 0;
}

static void audio_stream_voice_refill(audio_mixer_t *m, audio_voice_t *v)
{
    s16 temp[AUDIO_STREAM_REFILL_TEMP_FRAMES * 2];
    audio_stream_asset_t *asset;
    audio_stream_source_t *src;
    audio_stream_voice_state_t *st;

    if (!m || !v || v->kind != AUDIO_VOICE_KIND_STREAM)
        return;

    st = &v->u.stream;
    if (!mixer_is_valid_stream_asset_index(m, st->asset_handle))
        return;

    asset = &m->stream_assets[st->asset_handle];
    src = &asset->source;

    if (!audio_stream_source_is_ready(src))
        return;

    while (ring_buffer_size(&st->rb) < st->rb_high_watermark_bytes) {
        u32 free_bytes;
        u32 free_frames;
        u32 frames_to_read;
        u32 got_frames;
        u32 bytes_to_write;
        u32 written_bytes;

        free_bytes = ring_buffer_free_space(&st->rb);
        if (free_bytes < AUDIO_FRAME_BYTES)
            break;

        free_frames = free_bytes / AUDIO_FRAME_BYTES;
        frames_to_read = free_frames;
        if (frames_to_read > AUDIO_STREAM_REFILL_TEMP_FRAMES)
            frames_to_read = AUDIO_STREAM_REFILL_TEMP_FRAMES;

        if (st->decode_frame >= src->total_frames) {
            if (v->loop) {
                st->decode_frame = 0;
            } else {
                st->eof_reached = 1;
                break;
            }
        }

        audio_stream_source_update(src, st->decode_frame);

        got_frames = audio_stream_source_read_frames(src,
                                                     st->decode_frame,
                                                     temp,
                                                     frames_to_read);
        if (got_frames == 0)
            break;

        bytes_to_write = got_frames * AUDIO_FRAME_BYTES;
        written_bytes = ring_buffer_write(&st->rb, temp, bytes_to_write);
        if (written_bytes == 0)
            break;

        st->decode_frame += written_bytes / AUDIO_FRAME_BYTES;

        if (written_bytes < bytes_to_write)
            break;
    }
}

static void audio_mixer_mix_stream_voice(audio_mixer_t *m,
                                         int voice_handle,
                                         audio_voice_t *v,
                                         s32 *accum_l,
                                         s32 *accum_r,
                                         int frames)
{
    audio_stream_asset_t *asset;
    audio_stream_source_t *src;
    audio_stream_voice_state_t *st;
    float step;
    int gain_l, gain_r;
    int i;

    if (!m || !v || v->kind != AUDIO_VOICE_KIND_STREAM)
        return;
    if (!v->used || !v->playing || v->paused)
        return;

    st = &v->u.stream;
    if (!mixer_is_valid_stream_asset_index(m, st->asset_handle))
        return;

    asset = &m->stream_assets[st->asset_handle];
    src = &asset->source;

    if (src->total_frames == 0)
        return;

    step = v->speed * ((float)src->src_rate / (float)AUDIO_OUTPUT_RATE);
    gain_l = (v->volume * v->volume_l * 256) / (100 * 100);
    gain_r = (v->volume * v->volume_r * 256) / (100 * 100);

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

        rb_frames = ring_buffer_size(&st->rb) / AUDIO_FRAME_BYTES;

        if (st->startup_pending && ring_buffer_size(&st->rb) >= AUDIO_START_MIN_READY_BYTES)
            st->startup_pending = 0;

        if (v->play_cursor_frames < st->rb_base_frame) {
            v->play_cursor_frames = st->rb_base_frame;
            v->play_frac = 0.0f;
        }

        local_index = v->play_cursor_frames - st->rb_base_frame;

        if (local_index >= rb_frames) {
            if (!st->startup_pending)
                st->underrun_count++;

            if (st->eof_reached && rb_frames == 0 && !v->loop) {
                audio_mixer_finish_voice(m, voice_handle);
                return;
            }

            continue;
        }

        next_index = local_index + 1;
        if (next_index >= rb_frames) {
            if (st->eof_reached && !v->loop) {
                next_index = local_index;
            } else {
                if (!st->startup_pending)
                    st->underrun_count++;
                continue;
            }
        }

        byte_offset0 = local_index * AUDIO_FRAME_BYTES;
        byte_offset1 = next_index * AUDIO_FRAME_BYTES;

        got0 = ring_buffer_peek_at(&st->rb, byte_offset0, frame0, AUDIO_FRAME_BYTES);
        got1 = ring_buffer_peek_at(&st->rb, byte_offset1, frame1, AUDIO_FRAME_BYTES);

        if (got0 < AUDIO_FRAME_BYTES || got1 < AUDIO_FRAME_BYTES) {
            if (!st->startup_pending)
                st->underrun_count++;
            continue;
        }

        t = v->play_frac;
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;

        l = (s32)((1.0f - t) * (float)frame0[0] + t * (float)frame1[0]);
        r = (s32)((1.0f - t) * (float)frame0[1] + t * (float)frame1[1]);

        {
            float env = 1.0f;
            u32 frames_left_to_end = 0;

            if (v->loop) {
                if (v->play_cursor_frames < src->total_frames)
                    frames_left_to_end = src->total_frames - v->play_cursor_frames;

                if (frames_left_to_end <= AUDIO_LOOP_XFADE_FRAMES) {
                    env = (float)frames_left_to_end / (float)AUDIO_LOOP_XFADE_FRAMES;
                    if (env < 0.0f) env = 0.0f;
                    if (env > 1.0f) env = 1.0f;
                }

                if (st->loop_fade_in_remaining > 0 && st->loop_fade_in_total > 0) {
                    float in_env = 1.0f - ((float)st->loop_fade_in_remaining /
                                           (float)st->loop_fade_in_total);
                    if (in_env < 0.0f) in_env = 0.0f;
                    if (in_env > 1.0f) in_env = 1.0f;
                    if (in_env < env)
                        env = in_env;
                }
            }

            l = (s32)((float)l * env);
            r = (s32)((float)r * env);
        }

        accum_l[i] += (l * gain_l) >> 8;
        accum_r[i] += (r * gain_r) >> 8;

        v->play_frac += step;
        while (v->play_frac >= 1.0f) {
            v->play_cursor_frames++;
            v->play_frac -= 1.0f;
        }

        while (v->play_frac < 0.0f) {
            if (v->play_cursor_frames > 0)
                v->play_cursor_frames--;
            v->play_frac += 1.0f;
        }

        if (st->loop_fade_in_remaining > 0)
            st->loop_fade_in_remaining--;

        if (v->play_cursor_frames >= src->total_frames) {
            if (v->loop) {
                v->play_cursor_frames = 0;
                v->play_frac = 0.0f;
                st->rb_base_frame = 0;

                st->loop_fade_in_total = AUDIO_LOOP_XFADE_FRAMES;
                st->loop_fade_in_remaining = AUDIO_LOOP_XFADE_FRAMES;

                ring_buffer_reset(&st->rb);
                st->decode_frame = 0;
                st->eof_reached = 0;
                st->startup_pending = 1;

                audio_stream_source_prewarm(src, 0, 2);
                audio_stream_voice_refill(m, v);
                audio_stream_voice_refill(m, v);
            } else if (st->eof_reached) {
                audio_mixer_finish_voice(m, voice_handle);
                return;
            }
        }
    }

    if (v->play_cursor_frames > st->rb_base_frame) {
        u32 consumed_frames = v->play_cursor_frames - st->rb_base_frame;

        if (consumed_frames > TAIL_FRAMES_TO_KEEP) {
            u32 skip_frames = consumed_frames - TAIL_FRAMES_TO_KEEP;
            u32 skip_bytes = skip_frames * AUDIO_FRAME_BYTES;
            u32 skipped = ring_buffer_skip(&st->rb, skip_bytes);
            st->rb_base_frame += skipped / AUDIO_FRAME_BYTES;
        }
    }
}

static void audio_mixer_mix_sfx_voice(audio_mixer_t *m,
                                      int voice_handle,
                                      audio_voice_t *v,
                                      s32 *accum_l,
                                      s32 *accum_r,
                                      int frames)
{
    audio_sfx_asset_t *a;
    float step;
    int gain_l, gain_r;
    int i;

    if (!m || !v || v->kind != AUDIO_VOICE_KIND_SFX)
        return;
    if (!v->used || !v->playing || v->paused)
        return;

    if (!mixer_is_valid_sfx_asset_index(m, v->u.sfx.asset_handle))
        return;

    a = &m->sfx_assets[v->u.sfx.asset_handle];
    if (!a->pcm || a->total_frames == 0)
        return;

    step = v->speed * ((float)a->src_rate / (float)AUDIO_OUTPUT_RATE);
    gain_l = (v->volume * v->volume_l * 256) / (100 * 100);
    gain_r = (v->volume * v->volume_r * 256) / (100 * 100);

    for (i = 0; i < frames; i++) {
        u32 frame0_index, frame1_index;
        s16 l0, r0, l1, r1;
        float t;
        s32 l, r;

        if (v->play_cursor_frames >= a->total_frames) {
            if (v->loop) {
                v->play_cursor_frames = 0;
                v->play_frac = 0.0f;
            } else {
                audio_mixer_finish_voice(m, voice_handle);
                return;
            }
        }

        frame0_index = v->play_cursor_frames;
        frame1_index = frame0_index + 1;

        if (frame1_index >= a->total_frames) {
            if (v->loop)
                frame1_index = 0;
            else
                frame1_index = frame0_index;
        }

        l0 = a->pcm[frame0_index * 2 + 0];
        r0 = a->pcm[frame0_index * 2 + 1];
        l1 = a->pcm[frame1_index * 2 + 0];
        r1 = a->pcm[frame1_index * 2 + 1];

        t = v->play_frac;
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;

        l = (s32)((1.0f - t) * (float)l0 + t * (float)l1);
        r = (s32)((1.0f - t) * (float)r0 + t * (float)r1);

        accum_l[i] += (l * gain_l) >> 8;
        accum_r[i] += (r * gain_r) >> 8;

        v->play_frac += step;
        while (v->play_frac >= 1.0f) {
            v->play_cursor_frames++;
            v->play_frac -= 1.0f;
        }

        while (v->play_frac < 0.0f) {
            if (v->play_cursor_frames > 0)
                v->play_cursor_frames--;
            v->play_frac += 1.0f;
        }
    }
}

static void audio_mixer_render(audio_mixer_t *m)
{
    int i, j;

    memset(m->accum_l, 0, m->mixbuf_frames * sizeof(s32));
    memset(m->accum_r, 0, m->mixbuf_frames * sizeof(s32));

    for (j = 0; j < AUDIO_MIXER_MAX_VOICES; j++) {
        audio_voice_t *v = &m->voices[j];

        if (!v->used || !v->playing)
            continue;

        if (v->kind == AUDIO_VOICE_KIND_STREAM) {
            audio_mixer_mix_stream_voice(m, j, v, m->accum_l, m->accum_r, m->mixbuf_frames);
        } else if (v->kind == AUDIO_VOICE_KIND_SFX) {
            audio_mixer_mix_sfx_voice(m, j, v, m->accum_l, m->accum_r, m->mixbuf_frames);
        }
    }

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

static int audio_mixer_has_active_voices(const audio_mixer_t *m)
{
    int i;
    for (i = 0; i < AUDIO_MIXER_MAX_VOICES; i++) {
        if (m->voices[i].used && m->voices[i].playing)
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
        int i;

        if (!audio_mixer_has_active_voices(m)) {
            memset(m->mixbuf, 0, bytes);
            audsrv_wait_audio(bytes);
            if (m->thread_exit)
                break;
            audsrv_play_audio((const char *)m->mixbuf, bytes);
            m->output_active = 1;
            continue;
        }

        for (i = 0; i < AUDIO_MIXER_MAX_VOICES; i++) {
            audio_voice_t *v = &m->voices[i];

            if (!v->used || !v->playing || v->paused)
                continue;

            if (v->kind == AUDIO_VOICE_KIND_STREAM) {
                audio_stream_voice_state_t *st = &v->u.stream;

                audio_stream_voice_refill(m, v);

                if (st->underrun_count != st->underrun_count_logged) {
                    st->underrun_count_logged = st->underrun_count;
                    LOGLN("[audio:mixer] voice=%d underruns=%u eof=%d rb_size=%u rb_free=%u playing=%d paused=%d",
                          i,
                          st->underrun_count,
                          st->eof_reached,
                          ring_buffer_size(&st->rb),
                          ring_buffer_free_space(&st->rb),
                          v->playing,
                          v->paused);
                }
            }
        }

        audio_mixer_render(m);

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

    m->mixbuf = (s16 *)mem_alloc(mixbuf_frames * 2 * sizeof(s16), 64, MEMTAG_AUDIO);
    if (!m->mixbuf)
        goto fail;

    m->accum_l = (s32 *)mem_alloc(mixbuf_frames * sizeof(s32), 64, MEMTAG_AUDIO);
    if (!m->accum_l)
        goto fail;

    m->accum_r = (s32 *)mem_alloc(mixbuf_frames * sizeof(s32), 64, MEMTAG_AUDIO);
    if (!m->accum_r)
        goto fail;

    m->thread_stack = mem_alloc(AUDIO_MIXER_STACK_SIZE, 16, MEMTAG_THREAD_STACK);
    if (!m->thread_stack)
        goto fail;

    memset(&th, 0, sizeof(th));
    th.func = audio_mixer_thread;
    th.stack = m->thread_stack;
    th.stack_size = AUDIO_MIXER_STACK_SIZE;
    th.gp_reg = &_gp;
    th.initial_priority = AUDIO_MIXER_THREAD_PRIO;

    m->thread_id = CreateThread(&th);
    if (m->thread_id < 0)
        goto fail;

    StartThread(m->thread_id, m);
    return 0;

fail:
    if (m->thread_id >= 0) {
        DeleteThread(m->thread_id);
        m->thread_id = -1;
    }
    if (m->thread_stack) {
        mem_free(m->thread_stack, MEMTAG_THREAD_STACK);
        m->thread_stack = NULL;
    }
    if (m->accum_r) {
        mem_free(m->accum_r, MEMTAG_AUDIO);
        m->accum_r = NULL;
    }
    if (m->accum_l) {
        mem_free(m->accum_l, MEMTAG_AUDIO);
        m->accum_l = NULL;
    }
    if (m->mixbuf) {
        mem_free(m->mixbuf, MEMTAG_AUDIO);
        m->mixbuf = NULL;
    }
    memset(m->stream_assets, 0, sizeof(m->stream_assets));
    memset(m->sfx_assets, 0, sizeof(m->sfx_assets));
    memset(m->voices, 0, sizeof(m->voices));
    return -2;
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

    for (i = 0; i < AUDIO_MIXER_MAX_VOICES; i++)
        audio_mixer_free_voice(m, i);

    for (i = 0; i < AUDIO_MIXER_MAX_STREAM_ASSETS; i++) {
        audio_stream_asset_t *a = &m->stream_assets[i];
        if (a->used)
            audio_stream_source_destroy(&a->source);
    }

    for (i = 0; i < AUDIO_MIXER_MAX_SFX_ASSETS; i++) {
        audio_sfx_asset_t *a = &m->sfx_assets[i];
        if (a->used && a->pcm)
            mem_free(a->pcm, MEMTAG_AUDIO);
    }

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

void audio_mixer_update(audio_mixer_t *m)
{
    (void)m;
}

int audio_mixer_add_stream_asset(audio_mixer_t *m,
                                 const char *wav_path,
                                 int io_buf_bytes)
{
    int i, rc;
    audio_stream_asset_t *a;

    if (!m || !wav_path)
        return -1;

    for (i = 0; i < AUDIO_MIXER_MAX_STREAM_ASSETS; i++) {
        if (!m->stream_assets[i].used)
            break;
    }
    if (i >= AUDIO_MIXER_MAX_STREAM_ASSETS)
        return -2;

    a = &m->stream_assets[i];
    memset(a, 0, sizeof(*a));
    a->bound_voice = -1;
    a->default_volume = 100;
    a->default_speed = 1.0f;

    rc = audio_stream_source_init(&a->source, wav_path, (u32)((io_buf_bytes > 0) ? io_buf_bytes : 0));
    if (rc < 0)
        return rc;

    a->used = 1;
    return AUDIO_ASSET_HANDLE_STREAM_BASE + i;
}

int audio_mixer_add_sfx_asset(audio_mixer_t *m, const char *wav_path)
{
    int i, rc;
    audio_sfx_asset_t *a;
    audio_sfx_data_t data;

    if (!m || !wav_path)
        return -1;

    for (i = 0; i < AUDIO_MIXER_MAX_SFX_ASSETS; i++) {
        if (!m->sfx_assets[i].used)
            break;
    }
    if (i >= AUDIO_MIXER_MAX_SFX_ASSETS)
        return -2;

    memset(&data, 0, sizeof(data));
    rc = audio_sfx_bank_load_wav(wav_path, &data);
    if (rc < 0)
        return rc;

    a = &m->sfx_assets[i];
    memset(a, 0, sizeof(*a));

    a->pcm = data.pcm;
    a->total_frames = data.total_frames;
    a->src_rate = data.src_rate;
    a->channels = data.channels;
    a->bits = data.bits;
    a->default_volume = 100;
    a->default_speed = 1.0f;
    a->used = 1;

    return AUDIO_ASSET_HANDLE_SFX_BASE + i;
}

static void audio_mixer_remove_stream_asset(audio_mixer_t *m, int stream_idx)
{
    audio_stream_asset_t *a;
    int bound_voice = -1;

    if (!mixer_is_valid_stream_asset_index(m, stream_idx))
        return;

    a = &m->stream_assets[stream_idx];
    a->closing = 1;

    bound_voice = a->bound_voice;
    a->bound_voice = -1;

    if (bound_voice >= 0 && mixer_is_valid_voice(m, bound_voice)) {
        audio_voice_t *v = &m->voices[bound_voice];

        v->playing = 0;
        v->paused = 0;
        audio_mixer_notify_stopped(m, bound_voice, v);
        audio_mixer_free_voice(m, bound_voice);
    }

    audio_stream_source_destroy(&a->source);
    memset(a, 0, sizeof(*a));
    a->bound_voice = -1;
}

static int audio_mixer_preload_stream_asset(audio_mixer_t *m, int stream_idx)
{
    audio_stream_asset_t *a;
    int rc;

    if (!mixer_is_valid_stream_asset_index(m, stream_idx))
        return -1;

    a = &m->stream_assets[stream_idx];

    rc = audio_stream_source_prewarm(&a->source, 0, 2);
    if (rc < 0)
        return rc;

    return 0;
}

static int audio_mixer_stream_asset_is_ready(const audio_mixer_t *m, int stream_idx)
{
    const audio_stream_asset_t *a;

    if (!mixer_is_valid_stream_asset_index(m, stream_idx))
        return 0;

    a = &m->stream_assets[stream_idx];
    return audio_stream_source_is_prefilled(&a->source, 0, 1);
}

static int audio_mixer_play_stream_asset(audio_mixer_t *m, int stream_idx, int loop)
{
    audio_stream_asset_t *a;
    audio_voice_t *v;
    int voice_handle;
    int rc;

    if (!mixer_is_valid_stream_asset_index(m, stream_idx))
        return -1;

    a = &m->stream_assets[stream_idx];

    if (!audio_stream_source_is_ready(&a->source))
        return -2;

    if (a->bound_voice >= 0 && mixer_is_valid_voice(m, a->bound_voice)) {
        audio_mixer_stop_voice(m, a->bound_voice);
        audio_mixer_free_voice(m, a->bound_voice);
        a->bound_voice = -1;
    }

    voice_handle = audio_mixer_alloc_voice(m, AUDIO_VOICE_KIND_STREAM);
    if (voice_handle < 0)
        return voice_handle;

    rc = audio_mixer_init_stream_voice(m, voice_handle, stream_idx);
    if (rc < 0) {
        audio_mixer_free_voice(m, voice_handle);
        return rc;
    }

    v = &m->voices[voice_handle];
    v->loop = loop ? 1 : 0;
    v->paused = 0;
    v->volume = a->default_volume;
    v->volume_l = 100;
    v->volume_r = 100;
    v->pan = 0.0f;
    v->speed = a->default_speed;

    audio_voice_stream_reset_runtime(v);

    rc = audio_stream_source_prewarm(&a->source, 0, 2);
    if (rc < 0) {
        audio_mixer_free_voice(m, voice_handle);
        return rc;
    }

    audio_stream_voice_refill(m, v);
    audio_stream_voice_refill(m, v);

    v->playing = 1;
    a->bound_voice = voice_handle;

    audio_mixer_notify_started(m, voice_handle, v);
    return voice_handle;
}

static void audio_mixer_remove_sfx_asset(audio_mixer_t *m, int sfx_idx)
{
    int i;
    audio_sfx_asset_t *a;

    if (!mixer_is_valid_sfx_asset_index(m, sfx_idx))
        return;

    a = &m->sfx_assets[sfx_idx];
    a->closing = 1;

    for (i = 0; i < AUDIO_MIXER_MAX_VOICES; i++) {
        audio_voice_t *v = &m->voices[i];

        if (!v->used || v->kind != AUDIO_VOICE_KIND_SFX)
            continue;

        if (v->u.sfx.asset_handle == sfx_idx) {
            audio_mixer_stop_voice(m, i);
            audio_mixer_free_voice(m, i);
        }
    }

    if (a->pcm)
        mem_free(a->pcm, MEMTAG_AUDIO);

    memset(a, 0, sizeof(*a));
}

static int audio_mixer_sfx_asset_is_ready(const audio_mixer_t *m, int sfx_idx)
{
    if (!mixer_is_valid_sfx_asset_index(m, sfx_idx))
        return 0;
    return m->sfx_assets[sfx_idx].pcm != NULL;
}

static int audio_mixer_play_sfx(audio_mixer_t *m,
                                int sfx_idx,
                                int volume_percent,
                                float speed,
                                int loop)
{
    audio_sfx_asset_t *a;
    audio_voice_t *v;
    int voice_handle;

    if (!mixer_is_valid_sfx_asset_index(m, sfx_idx))
        return -1;

    a = &m->sfx_assets[sfx_idx];
    if (!a->pcm || a->total_frames == 0)
        return -2;

    if (volume_percent < 0) volume_percent = 0;
    if (volume_percent > 100) volume_percent = 100;

    if (speed <= 0.0f)
        speed = a->default_speed;
    if (speed < (1.0f / 3.0f)) speed = (1.0f / 3.0f);
    if (speed > 3.0f) speed = 3.0f;

    voice_handle = audio_mixer_alloc_voice(m, AUDIO_VOICE_KIND_SFX);
    if (voice_handle < 0)
        return voice_handle;

    v = &m->voices[voice_handle];
    v->u.sfx.asset_handle = sfx_idx;
    v->u.sfx.priority = 0;
    v->loop = loop ? 1 : 0;
    v->paused = 0;
    v->volume = volume_percent;
    v->volume_l = 100;
    v->volume_r = 100;
    v->pan = 0.0f;
    v->speed = speed;

    audio_voice_reset_playback(v);
    v->playing = 1;

    audio_mixer_notify_started(m, voice_handle, v);
    return voice_handle;
}

/* ------------------------------------------------------------------------- */
/* Unified asset API                                                         */
/* ------------------------------------------------------------------------- */

void audio_mixer_remove_asset(audio_mixer_t *m, int asset_handle)
{
    if (!m)
        return;

    if (mixer_is_stream_handle(asset_handle)) {
        audio_mixer_remove_stream_asset(m, mixer_stream_index_from_handle(asset_handle));
        return;
    }

    if (mixer_is_sfx_handle(asset_handle)) {
        audio_mixer_remove_sfx_asset(m, mixer_sfx_index_from_handle(asset_handle));
        return;
    }
}

int audio_mixer_preload_asset(audio_mixer_t *m, int asset_handle)
{
    if (!m)
        return -1;

    if (mixer_is_stream_handle(asset_handle))
        return audio_mixer_preload_stream_asset(m, mixer_stream_index_from_handle(asset_handle));

    if (mixer_is_sfx_handle(asset_handle))
        return 0;

    return -1;
}

int audio_mixer_asset_is_ready(const audio_mixer_t *m, int asset_handle)
{
    if (!m)
        return 0;

    if (mixer_is_stream_handle(asset_handle))
        return audio_mixer_stream_asset_is_ready(m, mixer_stream_index_from_handle(asset_handle));

    if (mixer_is_sfx_handle(asset_handle))
        return audio_mixer_sfx_asset_is_ready(m, mixer_sfx_index_from_handle(asset_handle));

    return 0;
}

audio_asset_kind_t audio_mixer_asset_get_kind(const audio_mixer_t *m, int asset_handle)
{
    (void)m;

    if (mixer_is_stream_handle(asset_handle))
        return AUDIO_ASSET_KIND_STREAM;

    if (mixer_is_sfx_handle(asset_handle))
        return AUDIO_ASSET_KIND_SFX;

    return AUDIO_ASSET_KIND_STREAM;
}

int audio_mixer_play_asset(audio_mixer_t *m,
                           int asset_handle,
                           int volume_percent,
                           float speed,
                           int loop)
{
    if (!m)
        return -1;

    if (volume_percent < 0) volume_percent = 0;
    if (volume_percent > 100) volume_percent = 100;

    if (speed <= 0.0f)
        speed = 1.0f;
    if (speed < (1.0f / 3.0f)) speed = (1.0f / 3.0f);
    if (speed > 3.0f) speed = 3.0f;

    if (mixer_is_stream_handle(asset_handle)) {
        int stream_idx = mixer_stream_index_from_handle(asset_handle);
        int voice = audio_mixer_play_stream_asset(m, stream_idx, loop);
        if (voice < 0)
            return voice;

        audio_mixer_set_voice_volume(m, voice, volume_percent);
        audio_mixer_set_voice_speed(m, voice, speed);
        return voice;
    }

    if (mixer_is_sfx_handle(asset_handle)) {
        int sfx_idx = mixer_sfx_index_from_handle(asset_handle);
        return audio_mixer_play_sfx(m, sfx_idx, volume_percent, speed, loop);
    }

    return -1;
}

/* ------------------------------------------------------------------------- */
/* Voices                                                                    */
/* ------------------------------------------------------------------------- */

int audio_mixer_alloc_voice(audio_mixer_t *m, audio_voice_kind_t kind)
{
    int i;
    audio_voice_t *v;

    if (!m || kind == AUDIO_VOICE_KIND_NONE)
        return -1;

    i = audio_mixer_find_free_voice(m);
    if (i < 0)
        return -2;

    v = &m->voices[i];
    audio_voice_clear(v);

    v->used = 1;
    v->kind = kind;
    v->volume = 100;
    v->volume_l = 100;
    v->volume_r = 100;
    v->pan = 0.0f;
    v->speed = 1.0f;

    return i;
}

void audio_mixer_free_voice(audio_mixer_t *m, int voice_handle)
{
    audio_voice_t *v;

    if (!mixer_is_valid_voice(m, voice_handle))
        return;

    v = &m->voices[voice_handle];

    if (v->playing)
        audio_mixer_stop_voice(m, voice_handle);

    if (v->kind == AUDIO_VOICE_KIND_STREAM)
        audio_mixer_release_stream_voice_resources(v);

    audio_voice_clear(v);
}

void audio_mixer_stop_voice(audio_mixer_t *m, int voice_handle)
{
    audio_voice_t *v;
    int stream_idx = -1;

    if (!mixer_is_valid_voice(m, voice_handle))
        return;

    v = &m->voices[voice_handle];

    if (v->kind == AUDIO_VOICE_KIND_STREAM)
        stream_idx = v->u.stream.asset_handle;

    v->playing = 0;
    v->paused = 0;

    if (stream_idx >= 0 &&
        mixer_is_valid_stream_asset_index(m, stream_idx) &&
        m->stream_assets[stream_idx].bound_voice == voice_handle) {
        m->stream_assets[stream_idx].bound_voice = -1;
    }

    if (v->kind == AUDIO_VOICE_KIND_STREAM)
        audio_voice_stream_reset_runtime(v);
    else
        audio_voice_reset_playback(v);

    audio_mixer_notify_stopped(m, voice_handle, v);
}

void audio_mixer_pause_voice(audio_mixer_t *m, int voice_handle)
{
    if (!mixer_is_valid_voice(m, voice_handle))
        return;
    if (m->voices[voice_handle].playing)
        m->voices[voice_handle].paused = 1;
}

void audio_mixer_resume_voice(audio_mixer_t *m, int voice_handle)
{
    if (!mixer_is_valid_voice(m, voice_handle))
        return;
    if (m->voices[voice_handle].playing)
        m->voices[voice_handle].paused = 0;
}

void audio_mixer_set_voice_volume(audio_mixer_t *m, int voice_handle, int percent)
{
    audio_voice_t *v;

    if (!mixer_is_valid_voice(m, voice_handle))
        return;

    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;

    v = &m->voices[voice_handle];
    v->volume = percent;
}

void audio_mixer_set_voice_channel_volume(audio_mixer_t *m,
                                          int voice_handle,
                                          int left_percent,
                                          int right_percent)
{
    audio_voice_t *v;

    if (!mixer_is_valid_voice(m, voice_handle))
        return;

    if (left_percent < 0) left_percent = 0;
    if (left_percent > 100) left_percent = 100;
    if (right_percent < 0) right_percent = 0;
    if (right_percent > 100) right_percent = 100;

    v = &m->voices[voice_handle];
    v->volume_l = left_percent;
    v->volume_r = right_percent;
}

void audio_mixer_set_voice_pan(audio_mixer_t *m, int voice_handle, float pan)
{
    audio_voice_t *v;
    float left, right;

    if (!mixer_is_valid_voice(m, voice_handle))
        return;

    if (pan < -1.0f) pan = -1.0f;
    if (pan >  1.0f) pan =  1.0f;

    v = &m->voices[voice_handle];
    v->pan = pan;

    left  = (pan <= 0.0f) ? 1.0f : (1.0f - pan);
    right = (pan >= 0.0f) ? 1.0f : (1.0f + pan);

    v->volume_l = (int)(left  * 100.0f);
    v->volume_r = (int)(right * 100.0f);
}

void audio_mixer_set_voice_speed(audio_mixer_t *m, int voice_handle, float speed)
{
    if (!mixer_is_valid_voice(m, voice_handle))
        return;

    if (speed < (1.0f / 3.0f)) speed = (1.0f / 3.0f);
    if (speed > 3.0f) speed = 3.0f;
    m->voices[voice_handle].speed = speed;
}

int audio_mixer_voice_is_playing(const audio_mixer_t *m, int voice_handle)
{
    if (!mixer_is_valid_voice(m, voice_handle))
        return 0;
    return m->voices[voice_handle].playing;
}

int audio_mixer_voice_is_paused(const audio_mixer_t *m, int voice_handle)
{
    if (!mixer_is_valid_voice(m, voice_handle))
        return 0;
    return m->voices[voice_handle].paused;
}

void audio_mixer_set_voice_callbacks(audio_mixer_t *m,
                                     int voice_handle,
                                     audio_voice_callback_t on_started,
                                     audio_voice_callback_t on_stopped,
                                     void *userdata)
{
    audio_voice_t *v;

    if (!mixer_is_valid_voice(m, voice_handle))
        return;

    v = &m->voices[voice_handle];
    v->on_started = on_started;
    v->on_stopped = on_stopped;
    v->callback_userdata = userdata;
}