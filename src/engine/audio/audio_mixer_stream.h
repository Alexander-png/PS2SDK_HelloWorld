#ifndef AUDIO_MIXER_STREAM_H
#define AUDIO_MIXER_STREAM_H

#include <tamtypes.h>
#include "engine/audio/audio_stream_source.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AUDIO_MIXER_MAX_STREAMS 8

typedef struct audio_mixer audio_mixer_t;
typedef struct audio_mix_stream audio_mix_stream_t;

typedef void (*audio_stream_callback_t)(
    audio_mixer_t *m,
    int handle,
    audio_mix_stream_t *stream,
    void *userdata
);

typedef struct audio_mix_stream {
    int used;
    audio_stream_source_t source;

    volatile int playing;
    volatile int paused;
    volatile int loop;
    volatile int volume;
    volatile float speed;

    double src_pos;

    audio_stream_callback_t on_started;
    audio_stream_callback_t on_stopped;
    void *callback_userdata;
    int started_notified;
    int stopped_notified;
} audio_mix_stream_t;

typedef struct audio_mixer {
    audio_mix_stream_t streams[AUDIO_MIXER_MAX_STREAMS];

    s16 *mixbuf;
    s32 *accum_l;
    s32 *accum_r;
    int mixbuf_frames;

    int thread_id;
    void *thread_stack;
    volatile int thread_exit;
    volatile int thread_running;

    int output_active;
} audio_mixer_t;

int  audio_mixer_init(audio_mixer_t *m, int mixbuf_frames);
void audio_mixer_destroy(audio_mixer_t *m);

int  audio_mixer_add_stream(audio_mixer_t *m, const char *wav_path, int io_buf_bytes);
void audio_mixer_remove_stream(audio_mixer_t *m, int handle);

int  audio_mixer_preload(audio_mixer_t *m, int handle);
int  audio_mixer_stream_is_ready(const audio_mixer_t *m, int handle);

int  audio_mixer_play(audio_mixer_t *m, int handle, int loop);
void audio_mixer_pause(audio_mixer_t *m, int handle);
void audio_mixer_resume(audio_mixer_t *m, int handle);
void audio_mixer_stop(audio_mixer_t *m, int handle);

void audio_mixer_set_volume(audio_mixer_t *m, int handle, int percent);
void audio_mixer_set_speed(audio_mixer_t *m, int handle, float speed);

void audio_mixer_set_callbacks(audio_mixer_t *m, int handle,
                               audio_stream_callback_t on_started,
                               audio_stream_callback_t on_stopped,
                               void *userdata);

int  audio_mixer_is_playing(const audio_mixer_t *m, int handle);
int  audio_mixer_is_paused(const audio_mixer_t *m, int handle);

#ifdef __cplusplus
}
#endif

#endif