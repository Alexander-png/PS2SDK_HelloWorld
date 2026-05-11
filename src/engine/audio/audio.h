#ifndef AUDIO_H
#define AUDIO_H

#ifdef __cplusplus
extern "C" {
#endif

int  audio_init(void);
void audio_shutdown(void);
void audio_update(float dt);
int  audio_is_available(void);

int  audio_stream_open(const char *wav_path, int io_buf_bytes);
void audio_stream_close(int handle);

int  audio_stream_play(int handle, int loop);
void audio_stream_stop(int handle);
void audio_stream_pause(int handle);
void audio_stream_resume(int handle);

void audio_stream_set_volume(int handle, int percent);
void audio_stream_set_speed(int handle, float speed);

int  audio_stream_is_playing(int handle);
int  audio_stream_is_paused(int handle);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_H */
