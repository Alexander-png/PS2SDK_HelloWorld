#ifndef AUDIO_STREAM_H
#define AUDIO_STREAM_H

#include <tamtypes.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct audio_stream {
    int fd;
    u32 data_offset;
    u32 data_size;
    u32 total_frames;

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

    u8 *io_buf;
    int io_buf_size;
    u32 io_buf_start_frame;
    u32 io_buf_frames;
} audio_stream_t;

int  audio_stream_init(audio_stream_t *s, const char *wav_path,
                       int mixbuf_frames, int io_buf_bytes);
void audio_stream_destroy(audio_stream_t *s);

int  audio_stream_play(audio_stream_t *s, int loop);
void audio_stream_pause(audio_stream_t *s);
void audio_stream_resume(audio_stream_t *s);
void audio_stream_stop(audio_stream_t *s);

void audio_stream_set_volume(audio_stream_t *s, int percent);
void audio_stream_set_speed(audio_stream_t *s, float speed);

int  audio_stream_is_playing(const audio_stream_t *s);
int  audio_stream_is_paused(const audio_stream_t *s);

/*
Offline conversion:
ffmpeg -i input.ogg -vn -ac 2 -ar 48000 -c:a pcm_s16le sound.wav

Required runtime init before audio_stream_init():
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