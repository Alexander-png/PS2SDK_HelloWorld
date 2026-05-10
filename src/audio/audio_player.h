#ifndef AUDIO_PLAYER_H
#define AUDIO_PLAYER_H

#include <tamtypes.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct audio_player {
    u8 *file_data;
    u32 file_size;
    u8 *pcm_data;
    u32 pcm_bytes;

    int src_rate;
    int channels;
    int bits;

    volatile int playing;
    volatile int paused;
    volatile int loop;
    volatile int volume;
    volatile float speed;

    double src_pos;

    int thread_id;
    volatile int thread_exit;
    volatile int thread_running;

    void *thread_stack;

    s16 *mixbuf;
    int mixbuf_frames;
} audio_player_t;

int  audio_player_init(audio_player_t *p, const char *wav_path, int mixbuf_frames);
void audio_player_destroy(audio_player_t *p);

int  audio_player_play(audio_player_t *p, int loop);
void audio_player_pause(audio_player_t *p);
void audio_player_resume(audio_player_t *p);
void audio_player_stop(audio_player_t *p);

void audio_player_set_volume(audio_player_t *p, int percent);
void audio_player_set_speed(audio_player_t *p, float speed);

int  audio_player_is_playing(const audio_player_t *p);
int  audio_player_is_paused(const audio_player_t *p);

/*
Offline conversion:
ffmpeg -i input.ogg -vn -ac 2 -ar 48000 -c:a pcm_s16le sound.wav

Required runtime init before audio_player_init():
- SifInitRpc(0)
- SifLoadFileInit()
- Load rom0:LIBSD and audsrv.irx
- audsrv_init()
- audsrv_set_format({48000,16,2})
*/

#ifdef __cplusplus
}
#endif

#endif