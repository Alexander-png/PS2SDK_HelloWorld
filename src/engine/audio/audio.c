#include "engine/audio/audio.h"
#include "engine/audio/audio_driver.h"
#include "engine/audio/audio_mixer.h"
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
        LOGLNC(LOGCAT_AUDIO, "[audio] disabled: driver init failed rc=%d", rc);
        return 0;
    }

    rc = audio_mixer_init(&g_audio.mixer, AUDIO_DEFAULT_MIXBUF_FRAMES);
    if (rc < 0) {
        LOGLNC(LOGCAT_AUDIO, "[audio] mixer init failed rc=%d", rc);
        audio_driver_shutdown();
        g_audio.available = 0;
        return 0;
    }

    g_audio.available = 1;
    LOGLNC(LOGCAT_AUDIO, "[audio] system ready");
    return 0;
}

void audio_shutdown(void)
{
    if (!g_audio.available)
        return;

    audio_mixer_destroy(&g_audio.mixer);
    audio_driver_shutdown();

    memset(&g_audio, 0, sizeof(g_audio));
    LOGLNC(LOGCAT_AUDIO, "[audio] system shutdown");
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
/* Asset API                                                                  */
/* ------------------------------------------------------------------------- */

int audio_asset_load_stream(const char *wav_path, int io_buf_bytes)
{
    if (!g_audio.available)
        return -1;

    return audio_mixer_add_stream_asset(&g_audio.mixer, wav_path, io_buf_bytes);
}

int audio_asset_load_sfx(const char *wav_path)
{
    if (!g_audio.available)
        return -1;

    return audio_mixer_add_sfx_asset(&g_audio.mixer, wav_path);
}

void audio_asset_unload(int asset_handle)
{
    if (!g_audio.available)
        return;

    audio_mixer_remove_asset(&g_audio.mixer, asset_handle);
}

int audio_asset_preload(int asset_handle)
{
    if (!g_audio.available)
        return -1;

    return audio_mixer_preload_asset(&g_audio.mixer, asset_handle);
}

int audio_asset_is_ready(int asset_handle)
{
    if (!g_audio.available)
        return 0;

    return audio_mixer_asset_is_ready(&g_audio.mixer, asset_handle);
}

audio_asset_kind_t audio_asset_get_kind(int asset_handle)
{
    if (!g_audio.available)
        return AUDIO_ASSET_KIND_STREAM;

    return audio_mixer_asset_get_kind(&g_audio.mixer, asset_handle);
}

/* ------------------------------------------------------------------------- */
/* Playback / voice API                                                       */
/* ------------------------------------------------------------------------- */

int audio_play(int asset_handle, float volume_percent, float speed, int loop)
{
    if (!g_audio.available)
        return -1;

    return audio_mixer_play_asset_ex(&g_audio.mixer,
                                     asset_handle,
                                     volume_percent,
                                     speed,
                                     loop,
                                     NULL,
                                     NULL,
                                     NULL);
}

int audio_play_ex(int asset_handle,
                  float volume_percent,
                  float speed,
                  int loop,
                  audio_voice_callback_t on_started,
                  audio_voice_callback_t on_stopped,
                  void *userdata)
{
    if (!g_audio.available)
        return -1;

    return audio_mixer_play_asset_ex(&g_audio.mixer,
                                     asset_handle,
                                     volume_percent,
                                     speed,
                                     loop,
                                     on_started,
                                     on_stopped,
                                     userdata);
}

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

void audio_voice_set_volume(int voice_handle, float percent)
{
    if (!g_audio.available)
        return;

    audio_mixer_set_voice_volume(&g_audio.mixer, voice_handle, percent);
}

void audio_voice_set_channel_volume(int voice_handle, float left_percent, float right_percent)
{
    if (!g_audio.available)
        return;

    audio_mixer_set_voice_channel_volume(&g_audio.mixer,
                                         voice_handle,
                                         left_percent,
                                         right_percent);
}

void audio_voice_set_pan(int voice_handle, float pan)
{
    if (!g_audio.available)
        return;

    audio_mixer_set_voice_pan(&g_audio.mixer, voice_handle, pan);
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

void audio_voice_set_callbacks(int voice_handle,
                               audio_voice_callback_t on_started,
                               audio_voice_callback_t on_stopped,
                               void *userdata)
{
    if (!g_audio.available)
        return;

    audio_mixer_set_voice_callbacks(&g_audio.mixer,
                                    voice_handle,
                                    on_started,
                                    on_stopped,
                                    userdata);
}