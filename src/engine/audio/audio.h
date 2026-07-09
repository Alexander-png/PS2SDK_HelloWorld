#ifndef AUDIO_H
#define AUDIO_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum audio_asset_kind {
    AUDIO_ASSET_KIND_STREAM = 0,
    AUDIO_ASSET_KIND_SFX
} audio_asset_kind_t;

int  audio_init(void);
void audio_shutdown(void);
void audio_update(float dt);
int  audio_is_available(void);

/* ------------------------------------------------------------------------- */
/* Asset API                                                                  */
/* ------------------------------------------------------------------------- */

int  audio_asset_load_stream(const char *wav_path, int io_buf_bytes);
int  audio_asset_load_sfx(const char *wav_path);
void audio_asset_unload(int asset_handle);

int  audio_asset_preload(int asset_handle);
int  audio_asset_is_ready(int asset_handle);
audio_asset_kind_t audio_asset_get_kind(int asset_handle);

/* ------------------------------------------------------------------------- */
/* Playback / voice API                                                       */
/* ------------------------------------------------------------------------- */

/* returns voice handle */
int  audio_play(int asset_handle, int volume_percent, float speed, int loop);

void audio_voice_stop(int voice_handle);
void audio_voice_pause(int voice_handle);
void audio_voice_resume(int voice_handle);

void audio_voice_set_volume(int voice_handle, int percent);
void audio_voice_set_channel_volume(int voice_handle, int left_percent, int right_percent);
void audio_voice_set_pan(int voice_handle, float pan);
void audio_voice_set_speed(int voice_handle, float speed);

int  audio_voice_is_playing(int voice_handle);
int  audio_voice_is_paused(int voice_handle);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_H */