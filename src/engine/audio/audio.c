#include "audio.h"
#include "audio_driver.h"
#include "audio_mixer_stream.h"
#include "engine/logging/log.h"

#include <string.h>

#ifndef AUDIO_DEFAULT_MIXBUF_FRAMES
#define AUDIO_DEFAULT_MIXBUF_FRAMES 1024
#endif

typedef struct audio_system {
    int available;
    audio_mixer_t mixer;
} audio_system_t;

static audio_system_t g_audio;

int audio_init(void)
{
    int rc;

    memset(&g_audio, 0, sizeof(g_audio));

    rc = audio_driver_init();
    if (rc < 0) {
        g_audio.available = 0;
        LOGLN("[audio] disabled: driver init failed rc=%d", rc);
        return 0;
    }

    rc = audio_mixer_init(&g_audio.mixer, AUDIO_DEFAULT_MIXBUF_FRAMES);
    if (rc < 0) {
        LOGLN("[audio] mixer init failed rc=%d", rc);
        audio_driver_shutdown();
        g_audio.available = 0;
        return 0;
    }

    g_audio.available = 1;
    LOGLN("[audio] system ready");
    return 0;
}

void audio_shutdown(void)
{
    if (!g_audio.available)
        return;

    audio_mixer_destroy(&g_audio.mixer);
    audio_driver_shutdown();

    memset(&g_audio, 0, sizeof(g_audio));
    LOGLN("[audio] system shutdown");
}

void audio_update(float dt)
{
    (void)dt;
}

int audio_is_available(void)
{
    return g_audio.available;
}

int audio_stream_open(const char *wav_path, int io_buf_bytes)
{
    if (!g_audio.available)
        return -1;

    return audio_mixer_add_stream(&g_audio.mixer, wav_path, io_buf_bytes);
}

void audio_stream_close(int handle)
{
    if (!g_audio.available)
        return;

    audio_mixer_remove_stream(&g_audio.mixer, handle);
}

int audio_stream_preload(int handle)
{
    if (!g_audio.available)
        return -1;

    return audio_mixer_preload(&g_audio.mixer, handle);
}

int audio_stream_is_ready(int handle)
{
    if (!g_audio.available)
        return 0;

    return audio_mixer_stream_is_ready(&g_audio.mixer, handle);
}

int audio_stream_play(int handle, int loop)
{
    if (!g_audio.available)
        return -1;

    return audio_mixer_play(&g_audio.mixer, handle, loop);
}

void audio_stream_stop(int handle)
{
    if (!g_audio.available)
        return;

    audio_mixer_stop(&g_audio.mixer, handle);
}

void audio_stream_pause(int handle)
{
    if (!g_audio.available)
        return;

    audio_mixer_pause(&g_audio.mixer, handle);
}

void audio_stream_resume(int handle)
{
    if (!g_audio.available)
        return;

    audio_mixer_resume(&g_audio.mixer, handle);
}

void audio_stream_set_volume(int handle, int percent)
{
    if (!g_audio.available)
        return;

    audio_mixer_set_volume(&g_audio.mixer, handle, percent);
}

void audio_stream_set_speed(int handle, float speed)
{
    if (!g_audio.available)
        return;

    audio_mixer_set_speed(&g_audio.mixer, handle, speed);
}

int audio_stream_is_playing(int handle)
{
    if (!g_audio.available)
        return 0;

    return audio_mixer_is_playing(&g_audio.mixer, handle);
}

int audio_stream_is_paused(int handle)
{
    if (!g_audio.available)
        return 0;

    return audio_mixer_is_paused(&g_audio.mixer, handle);
}