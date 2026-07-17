#include "engine/audio/audio_voice.h"

#include <string.h>

static void audio_voice_reset_playback_common(audio_voice_t *v)
{
    if (!v)
        return;

    v->play_cursor_frames = 0;
    v->play_frac = 0.0f;
}

static void audio_voice_reset_notify_flags_common(audio_voice_t *v)
{
    if (!v)
        return;

    v->started_notified = 0;
    v->stopped_notified = 0;
}

void audio_voice_reset_playback(audio_voice_t *v)
{
    if (!v)
        return;

    audio_voice_reset_playback_common(v);
}

void audio_voice_reset_notify_flags(audio_voice_t *v)
{
    if (!v)
        return;
    
    audio_voice_reset_notify_flags_common(v);
}

void audio_voice_clear(audio_voice_t *v)
{
    if (!v)
        return;

    memset(v, 0, sizeof(*v));

    v->kind = AUDIO_VOICE_KIND_NONE;

    v->volume = 100;
    v->volume_l = 100;
    v->volume_r = 100;
    v->pan = 0.0f;
    v->speed = 1.0f;

    v->u.stream.asset_index = -1;
}

void audio_voice_stream_reset_runtime(audio_voice_t *v)
{
    audio_stream_voice_state_t *st;

    if (!v || v->kind != AUDIO_VOICE_KIND_STREAM)
        return;

    st = &v->u.stream;

    st->decode_frame = 0;
    st->eof_reached = 0;
    st->underrun_count = 0;
    st->underrun_count_logged = 0;
    st->startup_pending = 1;
    st->loop_fade_in_remaining = 0;
    st->loop_fade_in_total = 0;
    st->rb_base_frame = 0;

    if (st->rb_storage && st->rb_capacity_bytes > 0)
        ring_buffer_reset(&st->rb);

    audio_voice_reset_playback_common(v);
}

int audio_voice_state_is_playing(const audio_voice_t *v)
{
    if (!v)
        return 0;
    return v->playing;
}

int audio_voice_state_is_paused(const audio_voice_t *v)
{
    if (!v)
        return 0;
    return v->paused;
}