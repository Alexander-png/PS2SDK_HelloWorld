#ifndef AUDIO_H
#define AUDIO_H

#ifdef __cplusplus
extern "C" {
#endif

int  audio_init(void);
void audio_shutdown(void);
void audio_update(float dt);
int  audio_is_available(void);

/* ------------------------------------------------------------------------- */
/* Music / streaming facade (backward-compatible API)                        */
/* ------------------------------------------------------------------------- */

int  audio_stream_open(const char *wav_path, int io_buf_bytes);
void audio_stream_close(int handle);

int  audio_stream_preload(int handle);
int  audio_stream_is_ready(int handle);

int  audio_stream_play(int handle, int loop);
void audio_stream_stop(int handle);
void audio_stream_pause(int handle);
void audio_stream_resume(int handle);

void audio_stream_set_volume(int handle, int percent);
void audio_stream_set_speed(int handle, float speed);

int  audio_stream_is_playing(int handle);
int  audio_stream_is_paused(int handle);

/* ------------------------------------------------------------------------- */
/* Explicit music API (optional new facade over same stream asset model)      */
/* ------------------------------------------------------------------------- */

int  audio_music_open(const char *wav_path, int io_buf_bytes);
void audio_music_close(int handle);

int  audio_music_preload(int handle);
int  audio_music_is_ready(int handle);

int  audio_music_play(int handle, int loop);
void audio_music_stop(int handle);
void audio_music_pause(int handle);
void audio_music_resume(int handle);

void audio_music_set_volume(int handle, int percent);
void audio_music_set_speed(int handle, float speed);

int  audio_music_is_playing(int handle);
int  audio_music_is_paused(int handle);

/* ------------------------------------------------------------------------- */
/* SFX asset API                                                              */
/* ------------------------------------------------------------------------- */

int  audio_sfx_load(const char *wav_path);
void audio_sfx_unload(int handle);

int  audio_sfx_is_ready(int handle);

/* returns voice handle */
int  audio_sfx_play(int handle);
int  audio_sfx_play_ex(int handle, int volume_percent, float speed, int loop);

/* ------------------------------------------------------------------------- */
/* Voice-level runtime control                                                */
/* ------------------------------------------------------------------------- */

void audio_voice_stop(int voice_handle);
void audio_voice_pause(int voice_handle);
void audio_voice_resume(int voice_handle);

void audio_voice_set_volume(int voice_handle, int percent);
void audio_voice_set_speed(int voice_handle, float speed);

int  audio_voice_is_playing(int voice_handle);
int  audio_voice_is_paused(int voice_handle);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_H */