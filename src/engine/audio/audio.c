#include "audio.h"
#include "audio_driver.h"
#include "audio_mixer.h"
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

    if (!g_audio.available)
        return;

    audio_mixer_update(&g_audio.mixer);
}

int audio_is_available(void)
{
    return g_audio.available;
}

/* ------------------------------------------------------------------------- */
/* stream facade                                                              */
/* ------------------------------------------------------------------------- */

int audio_stream_open(const char *wav_path, int io_buf_bytes)
{
    if (!g_audio.available)
        return -1;

    return audio_mixer_add_stream_asset(&g_audio.mixer, wav_path, io_buf_bytes);
}

void audio_stream_close(int handle)
{
    if (!g_audio.available)
        return;

    audio_mixer_remove_stream_asset(&g_audio.mixer, handle);
}

int audio_stream_preload(int handle)
{
    if (!g_audio.available)
        return -1;

    return audio_mixer_preload_stream_asset(&g_audio.mixer, handle);
}

int audio_stream_is_ready(int handle)
{
    if (!g_audio.available)
        return 0;

    return audio_mixer_stream_asset_is_ready(&g_audio.mixer, handle);
}

int audio_stream_play(int handle, int loop)
{
    if (!g_audio.available)
        return -1;

    return audio_mixer_play_stream_asset(&g_audio.mixer, handle, loop);
}

void audio_stream_stop(int handle)
{
    if (!g_audio.available)
        return;

    audio_mixer_stop_stream_asset(&g_audio.mixer, handle);
}

void audio_stream_pause(int handle)
{
    if (!g_audio.available)
        return;

    audio_mixer_pause_stream_asset(&g_audio.mixer, handle);
}

void audio_stream_resume(int handle)
{
    if (!g_audio.available)
        return;

    audio_mixer_resume_stream_asset(&g_audio.mixer, handle);
}

void audio_stream_set_volume(int handle, int percent)
{
    if (!g_audio.available)
        return;

    audio_mixer_set_stream_asset_volume(&g_audio.mixer, handle, percent);
}

void audio_stream_set_speed(int handle, float speed)
{
    if (!g_audio.available)
        return;

    audio_mixer_set_stream_asset_speed(&g_audio.mixer, handle, speed);
}

int audio_stream_is_playing(int handle)
{
    if (!g_audio.available)
        return 0;

    return audio_mixer_stream_asset_is_playing(&g_audio.mixer, handle);
}

int audio_stream_is_paused(int handle)
{
    if (!g_audio.available)
        return 0;

    return audio_mixer_stream_asset_is_paused(&g_audio.mixer, handle);
}

/* ------------------------------------------------------------------------- */
/* music facade                                                               */
/* ------------------------------------------------------------------------- */

int audio_music_open(const char *wav_path, int io_buf_bytes)
{
    return audio_stream_open(wav_path, io_buf_bytes);
}

void audio_music_close(int handle)
{
    audio_stream_close(handle);
}

int audio_music_preload(int handle)
{
    return audio_stream_preload(handle);
}

int audio_music_is_ready(int handle)
{
    return audio_stream_is_ready(handle);
}

int audio_music_play(int handle, int loop)
{
    return audio_stream_play(handle, loop);
}

void audio_music_stop(int handle)
{
    audio_stream_stop(handle);
}

void audio_music_pause(int handle)
{
    audio_stream_pause(handle);
}

void audio_music_resume(int handle)
{
    audio_stream_resume(handle);
}

void audio_music_set_volume(int handle, int percent)
{
    audio_stream_set_volume(handle, percent);
}

void audio_music_set_speed(int handle, float speed)
{
    audio_stream_set_speed(handle, speed);
}

int audio_music_is_playing(int handle)
{
    return audio_stream_is_playing(handle);
}

int audio_music_is_paused(int handle)
{
    return audio_stream_is_paused(handle);
}

/* ------------------------------------------------------------------------- */
/* sfx asset API                                                              */
/* ------------------------------------------------------------------------- */

int audio_sfx_load(const char *wav_path)
{
    if (!g_audio.available)
        return -1;

    return audio_mixer_add_sfx_asset(&g_audio.mixer, wav_path);
}

void audio_sfx_unload(int handle)
{
    if (!g_audio.available)
        return;

    audio_mixer_remove_sfx_asset(&g_audio.mixer, handle);
}

int audio_sfx_is_ready(int handle)
{
    if (!g_audio.available)
        return 0;

    return audio_mixer_sfx_asset_is_ready(&g_audio.mixer, handle);
}

int audio_sfx_play(int handle)
{
    if (!g_audio.available)
        return -1;

    return audio_mixer_play_sfx(&g_audio.mixer, handle, 100, 1.0f, 0);
}

int audio_sfx_play_ex(int handle, int volume_percent, float speed, int loop)
{
    if (!g_audio.available)
        return -1;

    return audio_mixer_play_sfx(&g_audio.mixer, handle, volume_percent, speed, loop);
}

/* ------------------------------------------------------------------------- */
/* voice API                                                                  */
/* ------------------------------------------------------------------------- */

void audio_voice_stop(int voice_handle)
{
    if (!g_audio.available)
        return;

    audio_mixer_stop_voice(&g_audio.mixer, voice_handle);
}

void audio_voice_pause(int voice_handle)
{
    if (!g_audio.available)
        return;

    audio_mixer_pause_voice(&g_audio.mixer, voice_handle);
}

void audio_voice_resume(int voice_handle)
{
    if (!g_audio.available)
        return;

    audio_mixer_resume_voice(&g_audio.mixer, voice_handle);
}

void audio_voice_set_volume(int voice_handle, int percent)
{
    if (!g_audio.available)
        return;

    audio_mixer_set_voice_volume(&g_audio.mixer, voice_handle, percent);
}

void audio_voice_set_speed(int voice_handle, float speed)
{
    if (!g_audio.available)
        return;

    audio_mixer_set_voice_speed(&g_audio.mixer, voice_handle, speed);
}

int audio_voice_is_playing(int voice_handle)
{
    if (!g_audio.available)
        return 0;

    return audio_mixer_voice_is_playing(&g_audio.mixer, voice_handle);
}

int audio_voice_is_paused(int voice_handle)
{
    if (!g_audio.available)
        return 0;

    return audio_mixer_voice_is_paused(&g_audio.mixer, voice_handle);
}