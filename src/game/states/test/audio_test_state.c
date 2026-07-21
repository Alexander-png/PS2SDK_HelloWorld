#include "engine/audio/audio.h"
#include "engine/input/input.h"
#include "engine/logging/log.h"
#include "engine/memory/memory_arena.h"

#include "game/states/test/debug_menu_state.h"
#include "game/states/test/audio_test_state.h"
#include "game/debug/debug_overlay.h"

#ifndef AUDIO_TEST_WAV_PATH
#define AUDIO_TEST_WAV_PATH "test.wav"
#endif

#ifndef AUDIO_TEST_IO_BUFFER_BYTES
#define AUDIO_TEST_IO_BUFFER_BYTES (128 * 1024)
#endif

#ifndef AUDIO_TEST_LOG_INTERVAL_SEC
#define AUDIO_TEST_LOG_INTERVAL_SEC 1.0f
#endif

typedef struct audio_test_state_data {
    int asset;
    int asset_ok;

    int voice;
    int paused;

    int volume;
    float speed;
    float log_timer;
    float uptime_sec;
    int overlay_dirty;

    debug_overlay_t overlay;
} audio_test_state_data_t;

static audio_test_state_data_t *audio_test_data(game_app_t *app)
{
    return GAME_APP_STATE_DATA_AS(app, audio_test_state_data_t);
}

static void audio_test_reset(audio_test_state_data_t *data)
{
    if (!data)
        return;

    data->asset = -1;
    data->asset_ok = 0;
    data->voice = -1;
    data->paused = 0;
    data->volume = 100;
    data->speed = 0.85f;
    data->log_timer = 0.0f;
    data->uptime_sec = 0.0f;
    data->overlay_dirty = 1;
}

static int audio_test_is_playing(const audio_test_state_data_t *data)
{
    if (!data || data->voice < 0)
        return 0;

    return audio_voice_is_playing(data->voice);
}

static int audio_test_is_paused(const audio_test_state_data_t *data)
{
    if (!data || data->voice < 0)
        return 0;

    return audio_voice_is_paused(data->voice);
}

static void audio_test_rebuild_overlay(audio_test_state_data_t *data)
{
    int audio_available;
    int playing;
    int paused;

    if (!data)
        return;

    audio_available = audio_is_available();
    playing = audio_test_is_playing(data);
    paused = audio_test_is_paused(data);

    debug_overlay_printf(
        &data->overlay,
        "Audio Test\n"
        "CROSS=pause/resume  TRIANGLE=stop/play\n"
        "UP/DOWN=volume      LEFT/RIGHT=speed\n"
        "START=return to menu\n"
        "\n"
        "wav:             %s\n"
        "audio_available: %d\n"
        "asset_handle:    %d\n"
        "asset_ok:        %d\n"
        "voice_handle:    %d\n"
        "playing:         %d\n"
        "paused:          %d\n"
        "volume:          %d\n"
        "speed:           %d/100\n"
        "uptime_ms:       %d\n",
        AUDIO_TEST_WAV_PATH,
        audio_available,
        data->asset,
        data->asset_ok,
        data->voice,
        playing,
        paused,
        data->volume,
        (int)(data->speed * 100.0f),
        (int)(data->uptime_sec * 1000.0f)
    );
}

static int audio_test_start_voice(audio_test_state_data_t *data)
{
    int voice;

    if (!data || !data->asset_ok || data->asset < 0)
        return -1;

    voice = audio_play(data->asset,
                       data->volume,
                       data->speed,
                       1);
    if (voice < 0) {
        LOGLNC(LOGCAT_AUDIO, "[state:audio_test] audio_play failed rc=%d", voice);
        return -1;
    }

    data->voice = voice;
    data->paused = 0;

    LOGLNC(LOGCAT_AUDIO, "[state:audio_test] playing asset=%d voice=%d loop=1",
          data->asset,
          data->voice);
    return 0;
}

static int audio_test_enter(game_app_t *app, void *userdata)
{
    audio_test_state_data_t *data;
    debug_overlay_desc_t overlay_desc;

    (void)userdata;

    data = (audio_test_state_data_t *)mem_arena_calloc(
        game_app_state_arena(app),
        1,
        sizeof(*data),
        16
    );
    if (!data) {
        LOGLNC(LOGCAT_STATE, "[state:audio_test] enter failed: no state arena memory");
        return -1;
    }

    game_app_set_state_userdata(app, data);
    audio_test_reset(data);

    debug_overlay_desc_init(&overlay_desc);
    overlay_desc.x = 16;
    overlay_desc.y = 16;
    overlay_desc.w = 620;
    overlay_desc.h = 220;

    if (debug_overlay_init(app, &data->overlay, &overlay_desc) != 0) {
        LOGLNC(LOGCAT_STATE, "[state:audio_test] overlay init failed");
        return -1;
    }

    LOGLNC(LOGCAT_STATE, "[state:audio_test] enter");
    LOGLNC(LOGCAT_AUDIO, "[state:audio_test] wav path: %s", AUDIO_TEST_WAV_PATH);

    if (!audio_is_available()) {
        LOGLNC(LOGCAT_AUDIO, "[state:audio_test] audio unavailable");
        audio_test_rebuild_overlay(data);
        data->overlay_dirty = 0;
        return 0;
    }

    data->asset = audio_asset_load_stream(
        AUDIO_TEST_WAV_PATH,
        AUDIO_TEST_IO_BUFFER_BYTES
    );

    if (data->asset < 0) {
        LOGLNC(LOGCAT_AUDIO, "[state:audio_test] audio_asset_load_stream failed: %d",
              data->asset);
        audio_test_rebuild_overlay(data);
        data->overlay_dirty = 0;
        return 0;
    }

    data->asset_ok = 1;

    LOGLNC(LOGCAT_AUDIO, "[state:audio_test] calling audio_asset_preload...");
    if (audio_asset_preload(data->asset) < 0) {
        LOGLNC(LOGCAT_AUDIO, "[state:audio_test] audio_asset_preload failed: %d",
              data->asset);
        audio_asset_unload(data->asset);
        data->asset = -1;
        data->asset_ok = 0;
        audio_test_rebuild_overlay(data);
        data->overlay_dirty = 0;
        return 0;
    }

    if (audio_test_start_voice(data) < 0) {
        audio_asset_unload(data->asset);
        data->asset = -1;
        data->asset_ok = 0;
        audio_test_rebuild_overlay(data);
        data->overlay_dirty = 0;
        return 0;
    }

    audio_test_rebuild_overlay(data);
    data->overlay_dirty = 0;
    return 0;
}

static void audio_test_exit(game_app_t *app)
{
    audio_test_state_data_t *data = audio_test_data(app);

    LOGLNC(LOGCAT_STATE, "[state:audio_test] exit");

    if (!data)
        return;

    if (data->voice >= 0) {
        audio_voice_stop(data->voice);
        data->voice = -1;
    }

    if (data->asset_ok && data->asset >= 0)
        audio_asset_unload(data->asset);

    debug_overlay_shutdown(app, &data->overlay);

    audio_test_reset(data);
    game_app_set_state_userdata(app, NULL);
}

static void audio_test_fixed_update(game_app_t *app, float dt)
{
    (void)app;
    (void)dt;
}

static void audio_test_update(game_app_t *app, float dt)
{
    audio_test_state_data_t *data = audio_test_data(app);
    int do_periodic_log = 0;

    if (!data)
        return;

    debug_overlay_update(app, &data->overlay, dt);

    data->uptime_sec += dt;

    if (input_button_pressed(INPUT_BUTTON_START)) {
        LOGLNC(LOGCAT_STATE, "[state:audio_test] START pressed, return to menu");
        input_consume();
        game_app_request_state_change(debug_menu_state_desc(), NULL);
        return;
    }

    if (data->voice >= 0) {
        if (input_button_pressed(INPUT_BUTTON_CROSS)) {
            if (data->paused) {
                audio_voice_resume(data->voice);
                data->paused = 0;
                LOGLNC(LOGCAT_AUDIO, "[state:audio_test] resume");
            } else {
                audio_voice_pause(data->voice);
                data->paused = 1;
                LOGLNC(LOGCAT_AUDIO, "[state:audio_test] pause");
            }
            data->overlay_dirty = 1;
        }

        if (input_button_pressed(INPUT_BUTTON_TRIANGLE)) {
            if (audio_voice_is_playing(data->voice)) {
                audio_voice_stop(data->voice);
                data->paused = 0;
                LOGLNC(LOGCAT_AUDIO, "[state:audio_test] stop");
            } else {
                if (audio_test_start_voice(data) == 0)
                    LOGLNC(LOGCAT_AUDIO, "[state:audio_test] play");
            }
            data->overlay_dirty = 1;
        }

        if (input_button_pressed(INPUT_BUTTON_UP)) {
            data->volume += 10;
            if (data->volume > 100)
                data->volume = 100;
            audio_voice_set_volume(data->voice, data->volume);
            LOGLNC(LOGCAT_AUDIO, "[state:audio_test] volume=%d", data->volume);
            data->overlay_dirty = 1;
        }

        if (input_button_pressed(INPUT_BUTTON_DOWN)) {
            data->volume -= 10;
            if (data->volume < 0)
                data->volume = 0;
            audio_voice_set_volume(data->voice, data->volume);
            LOGLNC(LOGCAT_AUDIO, "[state:audio_test] volume=%d", data->volume);
            data->overlay_dirty = 1;
        }

        if (input_button_pressed(INPUT_BUTTON_RIGHT)) {
            data->speed += 0.25f;
            if (data->speed > 3.0f)
                data->speed = 3.0f;
            audio_voice_set_speed(data->voice, data->speed);
            LOGLNC(LOGCAT_AUDIO, "[state:audio_test] speed=%d/100", (int)(data->speed * 100.0f));
            data->overlay_dirty = 1;
        }

        if (input_button_pressed(INPUT_BUTTON_LEFT)) {
            data->speed -= 0.25f;
            if (data->speed < (1.0f / 3.0f))
                data->speed = (1.0f / 3.0f);
            audio_voice_set_speed(data->voice, data->speed);
            LOGLNC(LOGCAT_AUDIO, "[state:audio_test] speed=%d/100", (int)(data->speed * 100.0f));
            data->overlay_dirty = 1;
        }
    }

    data->log_timer += dt;
    if (data->log_timer >= AUDIO_TEST_LOG_INTERVAL_SEC) {
        while (data->log_timer >= AUDIO_TEST_LOG_INTERVAL_SEC)
            data->log_timer -= AUDIO_TEST_LOG_INTERVAL_SEC;
        do_periodic_log = 1;
        data->overlay_dirty = 1;
    }

    if (do_periodic_log) {
        if (!audio_is_available()) {
            LOGLNC(LOGCAT_AUDIO, "[state:audio_test] audio unavailable");
        } else if (!data->asset_ok || data->asset < 0) {
            LOGLNC(LOGCAT_AUDIO, "[state:audio_test] no asset");
        } else {
            LOGLNC(LOGCAT_AUDIO, "[state:audio_test] playing=%d paused=%d volume=%d speed=%d/100",
                  (data->voice >= 0) ? audio_voice_is_playing(data->voice) : 0,
                  (data->voice >= 0) ? audio_voice_is_paused(data->voice) : 0,
                  data->volume,
                  (int)(data->speed * 100.0f));
        }
    }

    if (data->overlay_dirty) {
        audio_test_rebuild_overlay(data);
        data->overlay_dirty = 0;
    }
}

static void audio_test_draw(game_app_t *app, float alpha)
{
    audio_test_state_data_t *data = audio_test_data(app);

    (void)app;
    (void)alpha;

    if (data)
        debug_overlay_draw(&data->overlay);
}

static const game_state_desc_t g_audio_test_state = {
    "audio_test",
    audio_test_enter,
    audio_test_exit,
    audio_test_fixed_update,
    audio_test_update,
    audio_test_draw
};

const game_state_desc_t *audio_test_state_desc(void)
{
    return &g_audio_test_state;
}