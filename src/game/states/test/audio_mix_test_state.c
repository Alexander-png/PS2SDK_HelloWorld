
#include "game/states/test/debug_menu_state.h"

#include "audio_mix_test_state.h"
#include "engine/audio/audio.h"
#include "engine/debug/screen_console.h"
#include "engine/input/input.h"
#include "engine/logging/log.h"

#ifndef AUDIO_MIX_TEST_MUSIC_WAV_PATH
#define AUDIO_MIX_TEST_MUSIC_WAV_PATH "scary_music.wav"
#endif

#ifndef AUDIO_MIX_TEST_MUSIC_IO_BUFFER_BYTES
#define AUDIO_MIX_TEST_MUSIC_IO_BUFFER_BYTES (128 * 1024)
#endif

#ifndef AUDIO_MIX_TEST_SFX1_WAV_PATH
#define AUDIO_MIX_TEST_SFX1_WAV_PATH "scary_yelling.wav"
#endif

#ifndef AUDIO_MIX_TEST_SFX2_WAV_PATH
#define AUDIO_MIX_TEST_SFX2_WAV_PATH "scary_long.wav"
#endif

#ifndef AUDIO_MIX_TEST_SFX3_WAV_PATH
#define AUDIO_MIX_TEST_SFX3_WAV_PATH "scary_laugh.wav"
#endif

#ifndef AUDIO_MIX_TEST_LOG_INTERVAL_SEC
#define AUDIO_MIX_TEST_LOG_INTERVAL_SEC 1.0f
#endif

#ifndef AUDIO_MIX_TEST_RECENT_VOICES
#define AUDIO_MIX_TEST_RECENT_VOICES 8
#endif

#ifndef AUDIO_MIX_TEST_BURST_COUNT
#define AUDIO_MIX_TEST_BURST_COUNT 3
#endif

typedef struct audio_mix_test_state {
    int music_stream;
    int music_open_ok;
    int music_paused;
    int music_volume;
    float music_speed;

    int sfx1;
    int sfx2;
    int sfx3;

    int sfx1_ok;
    int sfx2_ok;
    int sfx3_ok;

    int last_sfx_voice;
    int last_sfx_voice_playing;
    int sfx_trigger_count;

    int recent_voices[AUDIO_MIX_TEST_RECENT_VOICES];
    int recent_voice_write_index;

    float log_timer;
    float uptime_sec;
    int screen_dirty;
    int page;
} audio_mix_test_state_t;

static audio_mix_test_state_t s_audio_mix_test;

static void audio_mix_test_reset_state(void)
{
    int i;

    s_audio_mix_test.music_stream = -1;
    s_audio_mix_test.music_open_ok = 0;
    s_audio_mix_test.music_paused = 0;
    s_audio_mix_test.music_volume = 80;
    s_audio_mix_test.music_speed = 1.0f;

    s_audio_mix_test.sfx1 = -1;
    s_audio_mix_test.sfx2 = -1;
    s_audio_mix_test.sfx3 = -1;

    s_audio_mix_test.sfx1_ok = 0;
    s_audio_mix_test.sfx2_ok = 0;
    s_audio_mix_test.sfx3_ok = 0;

    s_audio_mix_test.last_sfx_voice = -1;
    s_audio_mix_test.last_sfx_voice_playing = 0;
    s_audio_mix_test.sfx_trigger_count = 0;

    for (i = 0; i < AUDIO_MIX_TEST_RECENT_VOICES; i++)
        s_audio_mix_test.recent_voices[i] = -1;
    s_audio_mix_test.recent_voice_write_index = 0;

    s_audio_mix_test.log_timer = 0.0f;
    s_audio_mix_test.uptime_sec = 0.0f;
    s_audio_mix_test.screen_dirty = 1;
    s_audio_mix_test.page = 0;
}

static void audio_mix_test_push_recent_voice(int voice)
{
    s_audio_mix_test.recent_voices[s_audio_mix_test.recent_voice_write_index] = voice;
    s_audio_mix_test.recent_voice_write_index++;
    if (s_audio_mix_test.recent_voice_write_index >= AUDIO_MIX_TEST_RECENT_VOICES)
        s_audio_mix_test.recent_voice_write_index = 0;
}

static int audio_mix_test_count_recent_playing(void)
{
    int i;
    int count = 0;

    for (i = 0; i < AUDIO_MIX_TEST_RECENT_VOICES; i++) {
        int voice = s_audio_mix_test.recent_voices[i];
        if (voice >= 0 && audio_voice_is_playing(voice))
            count++;
    }

    return count;
}

static void audio_mix_test_redraw_screen(void)
{
    int audio_available = audio_is_available();
    int music_playing = 0;
    int music_paused = 0;

    if (s_audio_mix_test.music_open_ok && s_audio_mix_test.music_stream >= 0) {
        music_playing = audio_stream_is_playing(s_audio_mix_test.music_stream);
        music_paused = audio_stream_is_paused(s_audio_mix_test.music_stream);
    }

    if (s_audio_mix_test.last_sfx_voice >= 0)
        s_audio_mix_test.last_sfx_voice_playing =
            audio_voice_is_playing(s_audio_mix_test.last_sfx_voice);
    else
        s_audio_mix_test.last_sfx_voice_playing = 0;

    if (s_audio_mix_test.page == 0) {
        screen_console_begin(
            "audio_mix_test [page 1/2]",
            "CROSS=pause/resume music  TRIANGLE=stop/play music\n"
            "SQUARE=sfx1  CIRCLE=sfx2  R1=sfx3  L1=burst sfx1 x3 L2=page\n"
            "UP/DOWN=music volume      LEFT/RIGHT=music speed\n"
            "START=quit"
        );

        screen_console_printf("aud=%d  uptime=%dms\n",
                              audio_available,
                              (int)(s_audio_mix_test.uptime_sec * 1000.0f));

        screen_console_printf("music handle=%d open=%d play=%d pause=%d\n",
                              s_audio_mix_test.music_stream,
                              s_audio_mix_test.music_open_ok,
                              music_playing,
                              music_paused);

        screen_console_printf("music vol=%d speed=%d/100\n",
                              s_audio_mix_test.music_volume,
                              (int)(s_audio_mix_test.music_speed * 100.0f));

        screen_console_printf("sfx1=%d(%d) sfx2=%d(%d) sfx3=%d(%d)\n",
                              s_audio_mix_test.sfx1, s_audio_mix_test.sfx1_ok,
                              s_audio_mix_test.sfx2, s_audio_mix_test.sfx2_ok,
                              s_audio_mix_test.sfx3, s_audio_mix_test.sfx3_ok);

        screen_console_printf("last_voice=%d last_play=%d\n",
                              s_audio_mix_test.last_sfx_voice,
                              s_audio_mix_test.last_sfx_voice_playing);

        screen_console_printf("recent_playing=%d sfx_count=%d\n",
                              audio_mix_test_count_recent_playing(),
                              s_audio_mix_test.sfx_trigger_count);
    } else {
        int i;

        screen_console_begin(
            "audio_mix_test [page 2/2]",
            "CROSS=pause/resume music  TRIANGLE=stop/play music\n"
            "SQUARE=sfx1  CIRCLE=sfx2  R1=sfx3  L1=burst sfx1 x3 L2=page\n"
            "UP/DOWN=music volume      LEFT/RIGHT=music speed\n"
            "START=quit"
        );

        screen_console_printf("recent_voice_widx=%d\n",
                              s_audio_mix_test.recent_voice_write_index);
        screen_console_printf("recent_playing=%d\n\n",
                              audio_mix_test_count_recent_playing());

        for (i = 0; i < AUDIO_MIX_TEST_RECENT_VOICES; i++) {
            int voice = s_audio_mix_test.recent_voices[i];
            int playing = 0;

            if (voice >= 0)
                playing = audio_voice_is_playing(voice);

            screen_console_printf("[%d] voice=%d playing=%d\n",
                                  i, voice, playing);
        }
    }
}

static int audio_mix_test_open_music(void)
{
    s_audio_mix_test.music_stream = audio_stream_open(
        AUDIO_MIX_TEST_MUSIC_WAV_PATH,
        AUDIO_MIX_TEST_MUSIC_IO_BUFFER_BYTES
    );

    if (s_audio_mix_test.music_stream < 0) {
        LOGLN("[state:audio_mix_test] audio_stream_open failed: %d",
              s_audio_mix_test.music_stream);
        s_audio_mix_test.music_stream = -1;
        return -1;
    }

    s_audio_mix_test.music_open_ok = 1;

    if (audio_stream_preload(s_audio_mix_test.music_stream) < 0) {
        LOGLN("[state:audio_mix_test] audio_stream_preload failed: %d",
              s_audio_mix_test.music_stream);
        audio_stream_close(s_audio_mix_test.music_stream);
        s_audio_mix_test.music_stream = -1;
        s_audio_mix_test.music_open_ok = 0;
        return -1;
    }

    audio_stream_set_volume(s_audio_mix_test.music_stream,
                            s_audio_mix_test.music_volume);
    audio_stream_set_speed(s_audio_mix_test.music_stream,
                           s_audio_mix_test.music_speed);

    if (audio_stream_play(s_audio_mix_test.music_stream, 1) < 0) {
        LOGLN("[state:audio_mix_test] audio_stream_play failed");
        audio_stream_close(s_audio_mix_test.music_stream);
        s_audio_mix_test.music_stream = -1;
        s_audio_mix_test.music_open_ok = 0;
        return -1;
    }

    LOGLN("[state:audio_mix_test] music playing handle=%d loop=1",
          s_audio_mix_test.music_stream);
    return 0;
}

static void audio_mix_test_load_sfx_one(const char *tag,
                                        const char *path,
                                        int *out_handle,
                                        int *out_ok)
{
    *out_handle = audio_sfx_load(path);
    if (*out_handle < 0) {
        *out_ok = 0;
        LOGLN("[state:audio_mix_test] %s load failed path=%s rc=%d",
              tag, path, *out_handle);
        return;
    }

    *out_ok = 1;
    LOGLN("[state:audio_mix_test] %s loaded handle=%d path=%s",
          tag, *out_handle, path);
}

static int audio_mix_test_enter(game_app_t *app, void *userdata)
{
    (void)app;
    (void)userdata;

    audio_mix_test_reset_state();

    screen_console_enter();

    LOGLN("[state:audio_mix_test] enter");

    if (!audio_is_available()) {
        LOGLN("[state:audio_mix_test] audio unavailable");
        audio_mix_test_redraw_screen();
        s_audio_mix_test.screen_dirty = 0;
        return 0;
    }

    if (audio_mix_test_open_music() < 0) {
        audio_mix_test_redraw_screen();
        s_audio_mix_test.screen_dirty = 0;
        return 0;
    }

    audio_mix_test_load_sfx_one("sfx1",
                                AUDIO_MIX_TEST_SFX1_WAV_PATH,
                                &s_audio_mix_test.sfx1,
                                &s_audio_mix_test.sfx1_ok);

    audio_mix_test_load_sfx_one("sfx2",
                                AUDIO_MIX_TEST_SFX2_WAV_PATH,
                                &s_audio_mix_test.sfx2,
                                &s_audio_mix_test.sfx2_ok);

    audio_mix_test_load_sfx_one("sfx3",
                                AUDIO_MIX_TEST_SFX3_WAV_PATH,
                                &s_audio_mix_test.sfx3,
                                &s_audio_mix_test.sfx3_ok);

    audio_mix_test_redraw_screen();
    s_audio_mix_test.screen_dirty = 0;
    return 0;
}

static void audio_mix_test_exit(game_app_t *app)
{
    (void)app;

    LOGLN("[state:audio_mix_test] exit");

    if (s_audio_mix_test.last_sfx_voice >= 0)
        audio_voice_stop(s_audio_mix_test.last_sfx_voice);

    if (s_audio_mix_test.sfx1_ok && s_audio_mix_test.sfx1 >= 0)
        audio_sfx_unload(s_audio_mix_test.sfx1);
    if (s_audio_mix_test.sfx2_ok && s_audio_mix_test.sfx2 >= 0)
        audio_sfx_unload(s_audio_mix_test.sfx2);
    if (s_audio_mix_test.sfx3_ok && s_audio_mix_test.sfx3 >= 0)
        audio_sfx_unload(s_audio_mix_test.sfx3);

    if (s_audio_mix_test.music_open_ok && s_audio_mix_test.music_stream >= 0) {
        audio_stream_stop(s_audio_mix_test.music_stream);
        audio_stream_close(s_audio_mix_test.music_stream);
    }

    audio_mix_test_reset_state();
    s_audio_mix_test.screen_dirty = 0;

    screen_console_exit();
}

static void audio_mix_test_fixed_update(game_app_t *app, float dt)
{
    (void)app;
    (void)dt;
}

static void audio_mix_test_trigger_sfx(int sfx_handle,
                                       int sfx_ok,
                                       const char *tag)
{
    int voice;

    if (!sfx_ok || sfx_handle < 0) {
        LOGLN("[state:audio_mix_test] %s unavailable", tag);
        return;
    }

    voice = audio_sfx_play(sfx_handle);
    if (voice < 0) {
        LOGLN("[state:audio_mix_test] %s play failed rc=%d", tag, voice);
        return;
    }

    s_audio_mix_test.last_sfx_voice = voice;
    s_audio_mix_test.sfx_trigger_count++;
    audio_mix_test_push_recent_voice(voice);
    s_audio_mix_test.screen_dirty = 1;

    LOGLN("[state:audio_mix_test] %s voice=%d", tag, voice);
}

static void audio_mix_test_trigger_burst(int sfx_handle,
                                         int sfx_ok,
                                         const char *tag,
                                         int count)
{
    int i;
    int ok_count = 0;

    if (!sfx_ok || sfx_handle < 0) {
        LOGLN("[state:audio_mix_test] %s burst unavailable", tag);
        return;
    }

    for (i = 0; i < count; i++) {
        int voice = audio_sfx_play(sfx_handle);
        if (voice < 0) {
            LOGLN("[state:audio_mix_test] %s burst[%d] failed rc=%d",
                  tag, i, voice);
            continue;
        }

        s_audio_mix_test.last_sfx_voice = voice;
        s_audio_mix_test.sfx_trigger_count++;
        audio_mix_test_push_recent_voice(voice);
        ok_count++;
    }

    LOGLN("[state:audio_mix_test] %s burst count=%d ok=%d",
          tag, count, ok_count);

    s_audio_mix_test.screen_dirty = 1;
}

static void audio_mix_test_update(game_app_t *app, float dt)
{
    int do_periodic_log = 0;

    (void)app;

    s_audio_mix_test.uptime_sec += dt;

    if (input_button_pressed(INPUT_BUTTON_START)) {
        LOGLN("[state:audio_mix_test] START pressed, return to menu");
        input_consume();
        game_app_request_state_change(debug_menu_state_desc(), NULL);
        return;
    }

    if (s_audio_mix_test.music_open_ok && s_audio_mix_test.music_stream >= 0) {
        if (input_button_pressed(INPUT_BUTTON_CROSS)) {
            if (s_audio_mix_test.music_paused) {
                audio_stream_resume(s_audio_mix_test.music_stream);
                s_audio_mix_test.music_paused = 0;
                LOGLN("[state:audio_mix_test] music resume");
            } else {
                audio_stream_pause(s_audio_mix_test.music_stream);
                s_audio_mix_test.music_paused = 1;
                LOGLN("[state:audio_mix_test] music pause");
            }
            s_audio_mix_test.screen_dirty = 1;
        }

        if (input_button_pressed(INPUT_BUTTON_TRIANGLE)) {
            if (audio_stream_is_playing(s_audio_mix_test.music_stream)) {
                audio_stream_stop(s_audio_mix_test.music_stream);
                s_audio_mix_test.music_paused = 0;
                LOGLN("[state:audio_mix_test] music stop");
            } else {
                audio_stream_play(s_audio_mix_test.music_stream, 1);
                s_audio_mix_test.music_paused = 0;
                LOGLN("[state:audio_mix_test] music play");
            }
            s_audio_mix_test.screen_dirty = 1;
        }

        if (input_button_pressed(INPUT_BUTTON_UP)) {
            s_audio_mix_test.music_volume += 10;
            if (s_audio_mix_test.music_volume > 100)
                s_audio_mix_test.music_volume = 100;
            audio_stream_set_volume(s_audio_mix_test.music_stream,
                                    s_audio_mix_test.music_volume);
            LOGLN("[state:audio_mix_test] music volume=%d",
                  s_audio_mix_test.music_volume);
            s_audio_mix_test.screen_dirty = 1;
        }

        if (input_button_pressed(INPUT_BUTTON_DOWN)) {
            s_audio_mix_test.music_volume -= 10;
            if (s_audio_mix_test.music_volume < 0)
                s_audio_mix_test.music_volume = 0;
            audio_stream_set_volume(s_audio_mix_test.music_stream,
                                    s_audio_mix_test.music_volume);
            LOGLN("[state:audio_mix_test] music volume=%d",
                  s_audio_mix_test.music_volume);
            s_audio_mix_test.screen_dirty = 1;
        }

        if (input_button_pressed(INPUT_BUTTON_RIGHT)) {
            s_audio_mix_test.music_speed += 0.25f;
            if (s_audio_mix_test.music_speed > 3.0f)
                s_audio_mix_test.music_speed = 3.0f;
            audio_stream_set_speed(s_audio_mix_test.music_stream,
                                   s_audio_mix_test.music_speed);
            LOGLN("[state:audio_mix_test] music speed=%d/100",
                  (int)(s_audio_mix_test.music_speed * 100.0f));
            s_audio_mix_test.screen_dirty = 1;
        }

        if (input_button_pressed(INPUT_BUTTON_LEFT)) {
            s_audio_mix_test.music_speed -= 0.25f;
            if (s_audio_mix_test.music_speed < (1.0f / 3.0f))
                s_audio_mix_test.music_speed = (1.0f / 3.0f);
            audio_stream_set_speed(s_audio_mix_test.music_stream,
                                   s_audio_mix_test.music_speed);
            LOGLN("[state:audio_mix_test] music speed=%d/100",
                  (int)(s_audio_mix_test.music_speed * 100.0f));
            s_audio_mix_test.screen_dirty = 1;
        }
    }

    if (input_button_pressed(INPUT_BUTTON_SQUARE)) {
        audio_mix_test_trigger_sfx(s_audio_mix_test.sfx1,
                                   s_audio_mix_test.sfx1_ok,
                                   "sfx1");
    }

    if (input_button_pressed(INPUT_BUTTON_CIRCLE)) {
        audio_mix_test_trigger_sfx(s_audio_mix_test.sfx2,
                                   s_audio_mix_test.sfx2_ok,
                                   "sfx2");
    }

    if (input_button_pressed(INPUT_BUTTON_R1)) {
        audio_mix_test_trigger_sfx(s_audio_mix_test.sfx3,
                                   s_audio_mix_test.sfx3_ok,
                                   "sfx3");
    }

    if (input_button_pressed(INPUT_BUTTON_L1)) {
        audio_mix_test_trigger_burst(s_audio_mix_test.sfx1,
                                     s_audio_mix_test.sfx1_ok,
                                     "sfx1",
                                     AUDIO_MIX_TEST_BURST_COUNT);
    }

    if (input_button_pressed(INPUT_BUTTON_L2)) {
        s_audio_mix_test.page++;
        if (s_audio_mix_test.page > 1)
            s_audio_mix_test.page = 0;
        s_audio_mix_test.screen_dirty = 1;
    }

    s_audio_mix_test.log_timer += dt;
    if (s_audio_mix_test.log_timer >= AUDIO_MIX_TEST_LOG_INTERVAL_SEC) {
        while (s_audio_mix_test.log_timer >= AUDIO_MIX_TEST_LOG_INTERVAL_SEC)
            s_audio_mix_test.log_timer -= AUDIO_MIX_TEST_LOG_INTERVAL_SEC;
        do_periodic_log = 1;
        s_audio_mix_test.screen_dirty = 1;
    }

    if (do_periodic_log) {
        LOGLN("[state:audio_mix_test] music playing=%d paused=%d vol=%d speed=%d/100 last_voice=%d last_voice_playing=%d recent_playing=%d sfx_count=%d",
              (s_audio_mix_test.music_open_ok && s_audio_mix_test.music_stream >= 0)
                  ? audio_stream_is_playing(s_audio_mix_test.music_stream) : 0,
              (s_audio_mix_test.music_open_ok && s_audio_mix_test.music_stream >= 0)
                  ? audio_stream_is_paused(s_audio_mix_test.music_stream) : 0,
              s_audio_mix_test.music_volume,
              (int)(s_audio_mix_test.music_speed * 100.0f),
              s_audio_mix_test.last_sfx_voice,
              (s_audio_mix_test.last_sfx_voice >= 0)
                  ? audio_voice_is_playing(s_audio_mix_test.last_sfx_voice) : 0,
              audio_mix_test_count_recent_playing(),
              s_audio_mix_test.sfx_trigger_count);
    }

    if (s_audio_mix_test.screen_dirty) {
        audio_mix_test_redraw_screen();
        s_audio_mix_test.screen_dirty = 0;
    }
}

static void audio_mix_test_draw(game_app_t *app, float alpha)
{
    (void)app;
    (void)alpha;
}

static const game_state_desc_t g_audio_mix_test_state = {
    "audio_mix_test",
    audio_mix_test_enter,
    audio_mix_test_exit,
    audio_mix_test_fixed_update,
    audio_mix_test_update,
    audio_mix_test_draw
};

const game_state_desc_t *audio_mix_test_state_desc(void)
{
    return &g_audio_mix_test_state;
}