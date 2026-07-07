#ifndef AUDIO_MIXER_H
#define AUDIO_MIXER_H

#include <tamtypes.h>
#include "engine/audio/audio_stream_source.h"
#include "engine/audio/audio_voice.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef AUDIO_MIXER_MAX_STREAM_ASSETS
#define AUDIO_MIXER_MAX_STREAM_ASSETS 8
#endif

#ifndef AUDIO_MIXER_MAX_SFX_ASSETS
#define AUDIO_MIXER_MAX_SFX_ASSETS 32
#endif

#ifndef AUDIO_MIXER_MAX_VOICES
#define AUDIO_MIXER_MAX_VOICES 16
#endif

typedef struct audio_stream_asset {
    int used;
    int closing;
    int bound_voice; /* compatibility: one music stream <-> one active voice */

    int default_volume;
    float default_speed;

    audio_stream_source_t source;
} audio_stream_asset_t;

typedef struct audio_sfx_asset {
    int used;
    int closing;

    char path[128];

    s16 *pcm;
    u32 total_frames;

    int src_rate;
    int channels;
    int bits;
} audio_sfx_asset_t;

typedef struct audio_mixer {
    audio_stream_asset_t stream_assets[AUDIO_MIXER_MAX_STREAM_ASSETS];
    audio_sfx_asset_t    sfx_assets[AUDIO_MIXER_MAX_SFX_ASSETS];
    audio_voice_t        voices[AUDIO_MIXER_MAX_VOICES];

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

/* core mixer lifecycle */
int  audio_mixer_init(audio_mixer_t *m, int mixbuf_frames);
void audio_mixer_destroy(audio_mixer_t *m);

/* update / maintenance */
void audio_mixer_update(audio_mixer_t *m);

/* ------------------------------------------------------------------------- */
/* music / stream assets                                                     */
/* ------------------------------------------------------------------------- */

int  audio_mixer_add_stream_asset(audio_mixer_t *m,
                                  const char *wav_path,
                                  int io_buf_bytes);

void audio_mixer_remove_stream_asset(audio_mixer_t *m, int asset_handle);

int  audio_mixer_preload_stream_asset(audio_mixer_t *m, int asset_handle);
int  audio_mixer_stream_asset_is_ready(const audio_mixer_t *m, int asset_handle);

int  audio_mixer_play_stream_asset(audio_mixer_t *m, int asset_handle, int loop);
void audio_mixer_stop_stream_asset(audio_mixer_t *m, int asset_handle);
void audio_mixer_pause_stream_asset(audio_mixer_t *m, int asset_handle);
void audio_mixer_resume_stream_asset(audio_mixer_t *m, int asset_handle);

void audio_mixer_set_stream_asset_volume(audio_mixer_t *m, int asset_handle, int percent);
void audio_mixer_set_stream_asset_speed(audio_mixer_t *m, int asset_handle, float speed);

int  audio_mixer_stream_asset_is_playing(const audio_mixer_t *m, int asset_handle);
int  audio_mixer_stream_asset_is_paused(const audio_mixer_t *m, int asset_handle);

/* ------------------------------------------------------------------------- */
/* sfx assets                                                                */
/* ------------------------------------------------------------------------- */

int  audio_mixer_add_sfx_asset(audio_mixer_t *m, const char *wav_path);
void audio_mixer_remove_sfx_asset(audio_mixer_t *m, int asset_handle);

int  audio_mixer_sfx_asset_is_ready(const audio_mixer_t *m, int asset_handle);

int  audio_mixer_play_sfx(audio_mixer_t *m,
                          int asset_handle,
                          int volume_percent,
                          float speed,
                          int loop);

/* ------------------------------------------------------------------------- */
/* voices                                                                    */
/* ------------------------------------------------------------------------- */

int  audio_mixer_alloc_voice(audio_mixer_t *m, audio_voice_kind_t kind);
void audio_mixer_free_voice(audio_mixer_t *m, int voice_handle);

void audio_mixer_stop_voice(audio_mixer_t *m, int voice_handle);
void audio_mixer_pause_voice(audio_mixer_t *m, int voice_handle);
void audio_mixer_resume_voice(audio_mixer_t *m, int voice_handle);

void audio_mixer_set_voice_volume(audio_mixer_t *m, int voice_handle, int percent);
void audio_mixer_set_voice_speed(audio_mixer_t *m, int voice_handle, float speed);

int  audio_mixer_voice_is_playing(const audio_mixer_t *m, int voice_handle);
int  audio_mixer_voice_is_paused(const audio_mixer_t *m, int voice_handle);

/* callbacks */
void audio_mixer_set_voice_callbacks(audio_mixer_t *m,
                                     int voice_handle,
                                     audio_voice_callback_t on_started,
                                     audio_voice_callback_t on_stopped,
                                     void *userdata);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_MIXER_H */