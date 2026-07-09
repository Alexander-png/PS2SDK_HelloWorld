#include "game/states/test/debug_menu_state.h"

#include "audio_test_state.h"
#include "engine/audio/audio.h"
#include "engine/debug/screen_console.h"
#include "engine/input/input.h"
#include "engine/logging/log.h"

#ifndef AUDIO_TEST_WAV_PATH
#define AUDIO_TEST_WAV_PATH "test.wav"
#endif

#ifndef AUDIO_TEST_IO_BUFFER_BYTES
#define AUDIO_TEST_IO_BUFFER_BYTES (128 * 1024)
#endif

#ifndef AUDIO_TEST_LOG_INTERVAL_SEC
#define AUDIO_TEST_LOG_INTERVAL_SEC 1.0f
#endif

typedef struct audio_test_state {
    int asset;
    int asset_ok;

    int voice;
    int paused;

    int volume;
    float speed;
    float log_timer;
    float uptime_sec;
    int screen_dirty;
} audio_test_state_t;

static audio_test_state_t s_audio_test;

static void audio_test_redraw_screen(void)
{
    int audio_available = audio_is_available();
    int playing = 0;
    int paused = 0;

    if (s_audio_test.voice >= 0) {
        playing = audio_voice_is_playing(s_audio_test.voice);
        paused = audio_voice_is_paused(s_audio_test.voice);
    }

    screen_console_begin(
        "audio_test",
        "CROSS=pause/resume  TRIANGLE=stop/play\n"
        "UP/DOWN=volume      LEFT/RIGHT=speed\n"
        "START=quit"
    );

    screen_console_printf("wav: %s\n\n", AUDIO_TEST_WAV_PATH);
    screen_console_printf("audio_available: %d\n", audio_available);
    screen_console_printf("asset_handle:    %d\n", s_audio_test.asset);
    screen_console_printf("asset_ok:        %d\n", s_audio_test.asset_ok);
    screen_console_printf("voice_handle:    %d\n", s_audio_test.voice);
    screen_console_printf("playing:         %d\n", playing);
    screen_console_printf("paused:          %d\n", paused);
    screen_console_printf("volume:          %d\n", s_audio_test.volume);
    screen_console_printf("speed:           %d/100\n", (int)(s_audio_test.speed * 100.0f));
    screen_console_printf("uptime_ms:       %d\n", (int)(s_audio_test.uptime_sec * 1000.0f));
}

static int audio_test_start_voice(void)
{
    int voice;

    if (!s_audio_test.asset_ok || s_audio_test.asset < 0)
        return -1;

    voice = audio_play(s_audio_test.asset,
                       s_audio_test.volume,
                       s_audio_test.speed,
                       1);
    if (voice < 0) {
        LOGLN("[state:audio_test] audio_play failed rc=%d", voice);
        return -1;
    }

    s_audio_test.voice = voice;
    s_audio_test.paused = 0;

    LOGLN("[state:audio_test] playing asset=%d voice=%d loop=1",
          s_audio_test.asset,
          s_audio_test.voice);
    return 0;
}

static int audio_test_enter(game_app_t *app, void *userdata)
{
    (void)app;
    (void)userdata;

    s_audio_test.asset = -1;
    s_audio_test.asset_ok = 0;
    s_audio_test.voice = -1;
    s_audio_test.paused = 0;
    s_audio_test.volume = 100;
    s_audio_test.speed = 0.85f;
    s_audio_test.log_timer = 0.0f;
    s_audio_test.uptime_sec = 0.0f;
    s_audio_test.screen_dirty = 1;

    screen_console_enter();

    LOGLN("[state:audio_test] enter");
    LOGLN("[state:audio_test] wav path: %s", AUDIO_TEST_WAV_PATH);

    if (!audio_is_available()) {
        LOGLN("[state:audio_test] audio unavailable");
        audio_test_redraw_screen();
        s_audio_test.screen_dirty = 0;
        return 0;
    }

    s_audio_test.asset = audio_asset_load_stream(
        AUDIO_TEST_WAV_PATH,
        AUDIO_TEST_IO_BUFFER_BYTES
    );

    if (s_audio_test.asset < 0) {
        LOGLN("[state:audio_test] audio_asset_load_stream failed: %d",
              s_audio_test.asset);
        audio_test_redraw_screen();
        s_audio_test.screen_dirty = 0;
        return 0;
    }

    s_audio_test.asset_ok = 1;

    LOGLN("[state:audio_test] calling audio_asset_preload...");
    if (audio_asset_preload(s_audio_test.asset) < 0) {
        LOGLN("[state:audio_test] audio_asset_preload failed: %d",
              s_audio_test.asset);
        audio_asset_unload(s_audio_test.asset);
        s_audio_test.asset = -1;
        s_audio_test.asset_ok = 0;
        audio_test_redraw_screen();
        s_audio_test.screen_dirty = 0;
        return 0;
    }

    if (audio_test_start_voice() < 0) {
        audio_asset_unload(s_audio_test.asset);
        s_audio_test.asset = -1;
        s_audio_test.asset_ok = 0;
        audio_test_redraw_screen();
        s_audio_test.screen_dirty = 0;
        return 0;
    }

    audio_test_redraw_screen();
    s_audio_test.screen_dirty = 0;
    return 0;
}

static void audio_test_exit(game_app_t *app)
{
    (void)app;

    LOGLN("[state:audio_test] exit");

    if (s_audio_test.voice >= 0){
        audio_voice_stop(s_audio_test.voice);
        s_audio_test.voice = -1;
    }

    if (s_audio_test.asset_ok && s_audio_test.asset >= 0)
        audio_asset_unload(s_audio_test.asset);

    s_audio_test.asset = -1;
    s_audio_test.asset_ok = 0;
    s_audio_test.voice = -1;
    s_audio_test.paused = 0;
    s_audio_test.log_timer = 0.0f;
    s_audio_test.uptime_sec = 0.0f;
    s_audio_test.screen_dirty = 0;

    screen_console_exit();
}

static void audio_test_fixed_update(game_app_t *app, float dt)
{
    (void)app;
    (void)dt;
}

static void audio_test_update(game_app_t *app, float dt)
{
    int do_periodic_log = 0;

    (void)app;

    s_audio_test.uptime_sec += dt;

    if (input_button_pressed(INPUT_BUTTON_START)) {
        LOGLN("[state:audio_test] START pressed, return to menu");
        input_consume();
        game_app_request_state_change(debug_menu_state_desc(), NULL);
        return;
    }

    if (s_audio_test.voice >= 0) {
        if (input_button_pressed(INPUT_BUTTON_CROSS)) {
            if (s_audio_test.paused) {
                audio_voice_resume(s_audio_test.voice);
                s_audio_test.paused = 0;
                LOGLN("[state:audio_test] resume");
            } else {
                audio_voice_pause(s_audio_test.voice);
                s_audio_test.paused = 1;
                LOGLN("[state:audio_test] pause");
            }
            s_audio_test.screen_dirty = 1;
        }

        if (input_button_pressed(INPUT_BUTTON_TRIANGLE)) {
            if (audio_voice_is_playing(s_audio_test.voice)) {
                audio_voice_stop(s_audio_test.voice);
                s_audio_test.paused = 0;
                LOGLN("[state:audio_test] stop");
            } else {
                if (audio_test_start_voice() == 0)
                    LOGLN("[state:audio_test] play");
            }
            s_audio_test.screen_dirty = 1;
        }

        if (input_button_pressed(INPUT_BUTTON_UP)) {
            s_audio_test.volume += 10;
            if (s_audio_test.volume > 100)
                s_audio_test.volume = 100;
            audio_voice_set_volume(s_audio_test.voice, s_audio_test.volume);
            LOGLN("[state:audio_test] volume=%d", s_audio_test.volume);
            s_audio_test.screen_dirty = 1;
        }

        if (input_button_pressed(INPUT_BUTTON_DOWN)) {
            s_audio_test.volume -= 10;
            if (s_audio_test.volume < 0)
                s_audio_test.volume = 0;
            audio_voice_set_volume(s_audio_test.voice, s_audio_test.volume);
            LOGLN("[state:audio_test] volume=%d", s_audio_test.volume);
            s_audio_test.screen_dirty = 1;
        }

        if (input_button_pressed(INPUT_BUTTON_RIGHT)) {
            s_audio_test.speed += 0.25f;
            if (s_audio_test.speed > 3.0f)
                s_audio_test.speed = 3.0f;
            audio_voice_set_speed(s_audio_test.voice, s_audio_test.speed);
            LOGLN("[state:audio_test] speed=%d/100", (int)(s_audio_test.speed * 100.0f));
            s_audio_test.screen_dirty = 1;
        }

        if (input_button_pressed(INPUT_BUTTON_LEFT)) {
            s_audio_test.speed -= 0.25f;
            if (s_audio_test.speed < (1.0f / 3.0f))
                s_audio_test.speed = (1.0f / 3.0f);
            audio_voice_set_speed(s_audio_test.voice, s_audio_test.speed);
            LOGLN("[state:audio_test] speed=%d/100", (int)(s_audio_test.speed * 100.0f));
            s_audio_test.screen_dirty = 1;
        }
    }

    s_audio_test.log_timer += dt;
    if (s_audio_test.log_timer >= AUDIO_TEST_LOG_INTERVAL_SEC) {
        while (s_audio_test.log_timer >= AUDIO_TEST_LOG_INTERVAL_SEC)
            s_audio_test.log_timer -= AUDIO_TEST_LOG_INTERVAL_SEC;
        do_periodic_log = 1;
        s_audio_test.screen_dirty = 1;
    }

    if (do_periodic_log) {
        if (!audio_is_available()) {
            LOGLN("[state:audio_test] audio unavailable");
        } else if (!s_audio_test.asset_ok || s_audio_test.asset < 0) {
            LOGLN("[state:audio_test] no asset");
        } else {
            LOGLN("[state:audio_test] playing=%d paused=%d volume=%d speed=%d/100",
                  (s_audio_test.voice >= 0) ? audio_voice_is_playing(s_audio_test.voice) : 0,
                  (s_audio_test.voice >= 0) ? audio_voice_is_paused(s_audio_test.voice) : 0,
                  s_audio_test.volume,
                  (int)(s_audio_test.speed * 100.0f));
        }
    }

    if (s_audio_test.screen_dirty) {
        audio_test_redraw_screen();
        s_audio_test.screen_dirty = 0;
    }
}

static void audio_test_draw(game_app_t *app, float alpha)
{
    (void)app;
    (void)alpha;
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