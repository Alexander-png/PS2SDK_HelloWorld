#include "audio_test_state.h"
#include "audio.h"
#include "log.h"

#ifndef AUDIO_TEST_WAV_PATH
#define AUDIO_TEST_WAV_PATH "test.wav"
#endif

#ifndef AUDIO_TEST_IO_BUFFER_BYTES
#define AUDIO_TEST_IO_BUFFER_BYTES (128 * 1024)
#endif

typedef struct audio_test_state {
    int stream;
    int open_ok;
    unsigned int last_log_frame;
} audio_test_state_t;

static audio_test_state_t s_audio_test;

static int audio_test_enter(game_app_t *app, void *userdata)
{
    (void)app;
    (void)userdata;

    s_audio_test.stream = -1;
    s_audio_test.open_ok = 0;
    s_audio_test.last_log_frame = 0;

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

    audio_stream_set_volume(s_audio_test.stream, 100);
    audio_stream_set_speed(s_audio_test.stream, 1.0f);

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
}

static void audio_test_update(game_app_t *app, float dt)
{
    unsigned int frame;

    (void)dt;

    frame = game_app_frame_index();

    /*
     * Log roughly once per second with current fixed 60 FPS app pacing.
     */
    if (frame - s_audio_test.last_log_frame >= 60) {
        s_audio_test.last_log_frame = frame;

        if (!audio_is_available()) {
            LOGLN("[state:audio_test] frame=%u audio unavailable", frame);
            return;
        }

        if (!s_audio_test.open_ok || s_audio_test.stream < 0) {
            LOGLN("[state:audio_test] frame=%u no stream", frame);
            return;
        }

        LOGLN("[state:audio_test] frame=%u playing=%d paused=%d",
              frame,
              audio_stream_is_playing(s_audio_test.stream),
              audio_stream_is_paused(s_audio_test.stream));
    }
}

static void audio_test_draw(game_app_t *app)
{
    (void)app;
}

static const game_state_desc_t g_audio_test_state = {
    "audio_test",
    audio_test_enter,
    audio_test_exit,
    audio_test_update,
    audio_test_draw
};

const game_state_desc_t *audio_test_state_desc(void)
{
    return &g_audio_test_state;
}
