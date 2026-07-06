#include "game/states/test/debug_menu_state.h"

#include "audio_test_state.h"
#include "engine/audio/audio.h"
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
    int stream;
    int open_ok;
    int paused;
    int volume;
    float speed;
    float log_timer;
} audio_test_state_t;

static audio_test_state_t s_audio_test;

static int audio_test_enter(game_app_t *app, void *userdata)
{
    (void)app;
    (void)userdata;

    s_audio_test.stream = -1;
    s_audio_test.open_ok = 0;
    s_audio_test.paused = 0;
    s_audio_test.volume = 100;
    s_audio_test.speed = 0.85f;
    s_audio_test.log_timer = 0.0f;

    LOGLN("[state:audio_test] enter");
    LOGLN("[state:audio_test] wav path: %s", AUDIO_TEST_WAV_PATH);

    if (!audio_is_available()) {
        LOGLN("[state:audio_test] audio unavailable");
        return 0;
    }

    s_audio_test.stream = audio_stream_open(
        AUDIO_TEST_WAV_PATH,
        AUDIO_TEST_IO_BUFFER_BYTES
    );

    if (s_audio_test.stream < 0) {
        LOGLN("[state:audio_test] audio_stream_open failed: %d",
              s_audio_test.stream);
        return 0;
    }

    s_audio_test.open_ok = 1;

    LOGLN("[state:audio_test] calling audio_stream_preload...");
    if (audio_stream_preload(s_audio_test.stream) < 0) {
        LOGLN("[state:audio_test] audio_stream_preload failed: %d",
              s_audio_test.stream);
        audio_stream_close(s_audio_test.stream);
        s_audio_test.stream = -1;
        s_audio_test.open_ok = 0;
        return 0;
    }

    audio_stream_set_volume(s_audio_test.stream, s_audio_test.volume);
    audio_stream_set_speed(s_audio_test.stream, s_audio_test.speed);

    if (audio_stream_play(s_audio_test.stream, 1) < 0) {
        LOGLN("[state:audio_test] audio_stream_play failed");
        audio_stream_close(s_audio_test.stream);
        s_audio_test.stream = -1;
        s_audio_test.open_ok = 0;
        return 0;
    }

    LOGLN("[state:audio_test] playing handle=%d loop=1", s_audio_test.stream);
    return 0;
}

static void audio_test_exit(game_app_t *app)
{
    (void)app;

    LOGLN("[state:audio_test] exit");

    if (s_audio_test.open_ok && s_audio_test.stream >= 0) {
        audio_stream_stop(s_audio_test.stream);
        audio_stream_close(s_audio_test.stream);
    }

    s_audio_test.stream = -1;
    s_audio_test.open_ok = 0;
    s_audio_test.paused = 0;
    s_audio_test.log_timer = 0.0f;
}

static void audio_test_fixed_update(game_app_t *app, float dt)
{
    (void)app;
    (void)dt;
}

static void audio_test_update(game_app_t *app, float dt)
{
    (void)app;

    if (input_button_pressed(INPUT_BUTTON_START)) {
        LOGLN("[state:audio_test] START pressed, return to menu");
        input_consume();
        game_app_request_state_change(debug_menu_state_desc(), NULL);
        return;
    }

    if (s_audio_test.open_ok && s_audio_test.stream >= 0) {
        if (input_button_pressed(INPUT_BUTTON_CROSS)) {
            if (s_audio_test.paused) {
                audio_stream_resume(s_audio_test.stream);
                s_audio_test.paused = 0;
                LOGLN("[state:audio_test] resume");
            } else {
                audio_stream_pause(s_audio_test.stream);
                s_audio_test.paused = 1;
                LOGLN("[state:audio_test] pause");
            }
        }

        if (input_button_pressed(INPUT_BUTTON_TRIANGLE)) {
            if (audio_stream_is_playing(s_audio_test.stream)) {
                audio_stream_stop(s_audio_test.stream);
                s_audio_test.paused = 0;
                LOGLN("[state:audio_test] stop");
            } else {
                audio_stream_play(s_audio_test.stream, 1);
                s_audio_test.paused = 0;
                LOGLN("[state:audio_test] play");
            }
        }

        if (input_button_pressed(INPUT_BUTTON_UP)) {
            s_audio_test.volume += 10;
            if (s_audio_test.volume > 100)
                s_audio_test.volume = 100;
            audio_stream_set_volume(s_audio_test.stream, s_audio_test.volume);
            LOGLN("[state:audio_test] volume=%d", s_audio_test.volume);
        }

        if (input_button_pressed(INPUT_BUTTON_DOWN)) {
            s_audio_test.volume -= 10;
            if (s_audio_test.volume < 0)
                s_audio_test.volume = 0;
            audio_stream_set_volume(s_audio_test.stream, s_audio_test.volume);
            LOGLN("[state:audio_test] volume=%d", s_audio_test.volume);
        }

        if (input_button_pressed(INPUT_BUTTON_RIGHT)) {
            s_audio_test.speed += 0.25f;
            if (s_audio_test.speed > 3.0f)
                s_audio_test.speed = 3.0f;
            audio_stream_set_speed(s_audio_test.stream, s_audio_test.speed);
            LOGLN("[state:audio_test] speed=%d/100", (int)(s_audio_test.speed * 100.0f));
        }

        if (input_button_pressed(INPUT_BUTTON_LEFT)) {
            s_audio_test.speed -= 0.25f;
            if (s_audio_test.speed < (1.0f / 3.0f))
                s_audio_test.speed = (1.0f / 3.0f);
            audio_stream_set_speed(s_audio_test.stream, s_audio_test.speed);
            LOGLN("[state:audio_test] speed=%d/100", (int)(s_audio_test.speed * 100.0f));
        }
    }

    s_audio_test.log_timer += dt;
    if (s_audio_test.log_timer < AUDIO_TEST_LOG_INTERVAL_SEC)
        return;

    while (s_audio_test.log_timer >= AUDIO_TEST_LOG_INTERVAL_SEC)
        s_audio_test.log_timer -= AUDIO_TEST_LOG_INTERVAL_SEC;

    if (!audio_is_available()) {
        LOGLN("[state:audio_test] audio unavailable");
        return;
    }

    if (!s_audio_test.open_ok || s_audio_test.stream < 0) {
        LOGLN("[state:audio_test] no stream");
        return;
    }

    LOGLN("[state:audio_test] playing=%d paused=%d volume=%d speed=%d/100",
          audio_stream_is_playing(s_audio_test.stream),
          audio_stream_is_paused(s_audio_test.stream),
          s_audio_test.volume,
          (int)(s_audio_test.speed * 100.0f));
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