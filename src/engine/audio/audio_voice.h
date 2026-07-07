#ifndef AUDIO_VOICE_H
#define AUDIO_VOICE_H

#include <tamtypes.h>
#include "engine/memory/ring_buffer.h"

#ifdef __cplusplus
extern "C" {
#endif

struct audio_mixer;
typedef struct audio_mixer audio_mixer_t;

typedef struct audio_voice audio_voice_t;

typedef void (*audio_voice_callback_t)(
    audio_mixer_t *m,
    int voice_handle,
    audio_voice_t *voice,
    void *userdata
);

typedef enum audio_voice_kind {
    AUDIO_VOICE_KIND_NONE = 0,
    AUDIO_VOICE_KIND_STREAM,
    AUDIO_VOICE_KIND_SFX
} audio_voice_kind_t;

typedef struct audio_stream_voice_state {
    int asset_handle;

    u32 decode_frame;
    int eof_reached;
    u32 underrun_count;
    u32 underrun_count_logged;

    int startup_pending;

    int loop_fade_in_remaining;
    int loop_fade_in_total;

    ring_buffer_t rb;
    u8 *rb_storage;
    u32 rb_capacity_bytes;
    u32 rb_low_watermark_bytes;
    u32 rb_high_watermark_bytes;

    u32 rb_base_frame;
} audio_stream_voice_state_t;

typedef struct audio_sfx_voice_state {
    int asset_handle;
    int priority;
} audio_sfx_voice_state_t;

typedef union audio_voice_state {
    audio_stream_voice_state_t stream;
    audio_sfx_voice_state_t sfx;
} audio_voice_state_t;

typedef struct audio_voice {
    int used;
    audio_voice_kind_t kind;

    volatile int playing;
    volatile int paused;
    volatile int loop;
    volatile int volume;
    volatile float speed;

    u32 play_cursor_frames;
    float play_frac;

    audio_voice_callback_t on_started;
    audio_voice_callback_t on_stopped;
    void *callback_userdata;

    int started_notified;
    int stopped_notified;

    audio_voice_state_t u;
} audio_voice_t;

/* common helpers */
void audio_voice_reset_playback(audio_voice_t *v);
void audio_voice_clear(audio_voice_t *v);

/* stream-specific runtime */
void audio_voice_stream_reset_runtime(audio_voice_t *v);

/* generic lifecycle */
int  audio_voice_state_is_playing(const audio_voice_t *v);
int  audio_voice_state_is_paused(const audio_voice_t *v);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_VOICE_H */