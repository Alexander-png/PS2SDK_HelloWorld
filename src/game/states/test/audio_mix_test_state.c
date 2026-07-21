#include "engine/audio/audio.h"
#include "engine/input/input.h"
#include "engine/logging/log.h"
#include "engine/memory/memory_arena.h"

#include "game/states/test/debug_menu_state.h"
#include "game/states/test/audio_mix_test_state.h"
#include "game/debug/debug_overlay.h"

#include <stdio.h>

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

#ifndef AUDIO_MIX_TEST_PAN_STEP
#define AUDIO_MIX_TEST_PAN_STEP 0.2f
#endif

typedef struct audio_mix_test_state_data {
    int music_asset;
    int music_asset_ok;
    int music_voice;
    int music_paused;
    int music_volume;
    float music_speed;
    float music_pan;

    int sfx1;
    int sfx2;
    int sfx3;

    int sfx1_ok;
    int sfx2_ok;
    int sfx3_ok;

    float sfx_pan;

    int last_sfx_voice;
    int last_sfx_voice_playing;
    int sfx_trigger_count;

    int recent_voices[AUDIO_MIX_TEST_RECENT_VOICES];
    int recent_voice_write_index;

    float log_timer;
    float uptime_sec;
    int overlay_dirty;
    int page;

    int shutting_down;

    debug_overlay_t overlay;
} audio_mix_test_state_data_t;

static audio_mix_test_state_data_t *audio_mix_test_data(game_app_t *app)
{
    return GAME_APP_STATE_DATA_AS(app, audio_mix_test_state_data_t);
}

static void audio_mix_test_detach_voice_callbacks(audio_mix_test_state_data_t *s)
{
    int i;

    if (!s)
        return;

    if (s->music_voice >= 0) {
        audio_voice_set_callbacks(s->music_voice, NULL, NULL, NULL);
    }

    if (s->last_sfx_voice >= 0) {
        audio_voice_set_callbacks(s->last_sfx_voice, NULL, NULL, NULL);
    }

    for (i = 0; i < AUDIO_MIX_TEST_RECENT_VOICES; ++i) {
        int voice = s->recent_voices[i];
        if (voice >= 0)
            audio_voice_set_callbacks(voice, NULL, NULL, NULL);
    }
}

static int audio_mix_test_voice_in_recent(const audio_mix_test_state_data_t *s, int voice)
{
    int i;

    if (!s || voice < 0)
        return 0;

    for (i = 0; i < AUDIO_MIX_TEST_RECENT_VOICES; ++i) {
        if (s->recent_voices[i] == voice)
            return 1;
    }

    return 0;
}

static float audio_mix_test_clamp_pan(float pan)
{
    if (pan < -1.0f)
        return -1.0f;
    if (pan > 1.0f)
        return 1.0f;
    return pan;
}

static void audio_mix_test_reset_state(audio_mix_test_state_data_t *s)
{
    int i;

    if (!s)
        return;

    s->music_asset = -1;
    s->music_asset_ok = 0;
    s->music_voice = -1;
    s->music_paused = 0;
    s->music_volume = 80;
    s->music_speed = 1.0f;
    s->music_pan = 0.0f;

    s->sfx1 = -1;
    s->sfx2 = -1;
    s->sfx3 = -1;

    s->sfx1_ok = 0;
    s->sfx2_ok = 0;
    s->sfx3_ok = 0;

    s->sfx_pan = 0.0f;

    s->last_sfx_voice = -1;
    s->last_sfx_voice_playing = 0;
    s->sfx_trigger_count = 0;

    for (i = 0; i < AUDIO_MIX_TEST_RECENT_VOICES; i++)
        s->recent_voices[i] = -1;
    s->recent_voice_write_index = 0;

    s->log_timer = 0.0f;
    s->uptime_sec = 0.0f;
    s->overlay_dirty = 1;
    s->page = 0;

    s->shutting_down = 0;
}

static void audio_mix_test_push_recent_voice(audio_mix_test_state_data_t *s, int voice)
{
    if (!s)
        return;

    s->recent_voices[s->recent_voice_write_index] = voice;
    s->recent_voice_write_index++;
    if (s->recent_voice_write_index >= AUDIO_MIX_TEST_RECENT_VOICES)
        s->recent_voice_write_index = 0;
}

static int audio_mix_test_count_recent_playing(const audio_mix_test_state_data_t *s)
{
    int i, j;
    int unique[AUDIO_MIX_TEST_RECENT_VOICES];
    int unique_count = 0;

    if (!s)
        return 0;

    for (i = 0; i < AUDIO_MIX_TEST_RECENT_VOICES; i++)
        unique[i] = -1;

    for (i = 0; i < AUDIO_MIX_TEST_RECENT_VOICES; i++) {
        int voice = s->recent_voices[i];
        int already_seen = 0;

        if (voice < 0)
            continue;

        for (j = 0; j < unique_count; j++) {
            if (unique[j] == voice) {
                already_seen = 1;
                break;
            }
        }

        if (already_seen)
            continue;

        if (audio_voice_is_playing(voice))
            unique[unique_count++] = voice;
    }

    return unique_count;
}

static void audio_mix_test_on_voice_started(audio_mixer_t *m,
                                            int voice_handle,
                                            audio_voice_t *voice,
                                            void *userdata)
{
    audio_mix_test_state_data_t *s = (audio_mix_test_state_data_t *)userdata;

    (void)m;
    (void)voice;

    if (!s)
        return;

    if (s->shutting_down)
        return;

    LOGLNC(LOGCAT_AUDIO, "[state:audio_mix_test] callback started voice=%d", voice_handle);
    s->overlay_dirty = 1;
}

static void audio_mix_test_on_voice_stopped(audio_mixer_t *m,
                                            int voice_handle,
                                            audio_voice_t *voice,
                                            void *userdata)
{
    audio_mix_test_state_data_t *s = (audio_mix_test_state_data_t *)userdata;

    (void)m;
    (void)voice;

    if (!s)
        return;

    if (s->shutting_down)
        return;

    if (voice_handle == s->music_voice) {
        LOGLNC(LOGCAT_AUDIO, "[state:audio_mix_test] callback stopped music voice=%d",
              voice_handle);
        s->music_voice = -1;
        s->music_paused = 0;
    } else if (voice_handle == s->last_sfx_voice ||
               audio_mix_test_voice_in_recent(s, voice_handle)) {
        LOGLNC(LOGCAT_AUDIO, "[state:audio_mix_test] callback stopped sfx voice=%d",
              voice_handle);

        if (voice_handle == s->last_sfx_voice)
            s->last_sfx_voice = -1;
    } else {
        LOGLNC(LOGCAT_AUDIO, "[state:audio_mix_test] callback stopped unknown voice=%d",
              voice_handle);
    }

    s->overlay_dirty = 1;
}

static int audio_mix_test_music_playing(const audio_mix_test_state_data_t *s)
{
    if (!s || s->music_voice < 0)
        return 0;

    return audio_voice_is_playing(s->music_voice);
}

static int audio_mix_test_music_paused_now(const audio_mix_test_state_data_t *s)
{
    if (!s || s->music_voice < 0)
        return 0;

    return audio_voice_is_paused(s->music_voice);
}

static void audio_mix_test_refresh_last_sfx_playing(audio_mix_test_state_data_t *s)
{
    if (!s)
        return;

    if (s->last_sfx_voice >= 0)
        s->last_sfx_voice_playing = audio_voice_is_playing(s->last_sfx_voice);
    else
        s->last_sfx_voice_playing = 0;
}

static void audio_mix_test_rebuild_overlay(audio_mix_test_state_data_t *s)
{
    int audio_available;
    int music_playing;
    int music_paused;

    if (!s)
        return;

    if (s->shutting_down)
    return;

    audio_available = audio_is_available();
    music_playing = audio_mix_test_music_playing(s);
    music_paused = audio_mix_test_music_paused_now(s);

    audio_mix_test_refresh_last_sfx_playing(s);

    if (s->page == 0) {
        debug_overlay_printf(
            &s->overlay,
            "Audio Mix Test [1/2]\n"
            "CROSS pause/resume music  TRIANGLE stop/play music\n"
            "SQUARE sfx1  CIRCLE sfx2  R1 sfx3  L1 burst x3  L2 page\n"
            "UP/DOWN music volume      LEFT/RIGHT music speed\n"
            "START return to menu\n"
            "\n"
            "aud=%d uptime=%dms\n"
            "music asset=%d ok=%d voice=%d play=%d pause=%d\n"
            "music vol=%d speed=%d/100 pan=%d/100\n"
            "sfx1=%d(%d) sfx2=%d(%d) sfx3=%d(%d)\n"
            "sfx pan=%d/100\n"
            "last_voice=%d last_play=%d\n"
            "recent_playing=%d sfx_count=%d\n",
            audio_available,
            (int)(s->uptime_sec * 1000.0f),
            s->music_asset,
            s->music_asset_ok,
            s->music_voice,
            music_playing,
            music_paused,
            s->music_volume,
            (int)(s->music_speed * 100.0f),
            (int)(s->music_pan * 100.0f),
            s->sfx1, s->sfx1_ok,
            s->sfx2, s->sfx2_ok,
            s->sfx3, s->sfx3_ok,
            (int)(s->sfx_pan * 100.0f),
            s->last_sfx_voice,
            s->last_sfx_voice_playing,
            audio_mix_test_count_recent_playing(s),
            s->sfx_trigger_count
        );
    } else {
        int i;
        char buf[DEBUG_OVERLAY_TEXT_CAPACITY];
        int off = 0;

        off += snprintf(buf + off, sizeof(buf) - off,
            "Audio Mix Test [2/2]\n"
            "LEFT/RIGHT music pan  UP/DOWN sfx pan\n"
            "SQUARE/CIRCLE/R1 play sfx with current sfx pan\n"
            "TRIANGLE center pans  L1 burst x3  L2 page\n"
            "START return to menu\n"
            "\n"
            "music pan=%d/100\n"
            "sfx pan=%d/100\n"
            "last_voice=%d last_play=%d\n"
            "recent_widx=%d\n"
            "recent_playing=%d\n"
            "\n",
            (int)(s->music_pan * 100.0f),
            (int)(s->sfx_pan * 100.0f),
            s->last_sfx_voice,
            s->last_sfx_voice_playing,
            s->recent_voice_write_index,
            audio_mix_test_count_recent_playing(s));

        for (i = 0; i < AUDIO_MIX_TEST_RECENT_VOICES; i++) {
            int voice = s->recent_voices[i];
            int playing = 0;

            if (voice >= 0)
                playing = audio_voice_is_playing(voice);

            if (off >= (int)sizeof(buf))
                break;

            off += snprintf(buf + off, sizeof(buf) - off,
                            "[%d] v=%d p=%d\n",
                            i, voice, playing);
        }

        buf[sizeof(buf) - 1] = '\0';
        debug_overlay_set_text(&s->overlay, buf);
    }
}

static int audio_mix_test_start_music_voice(audio_mix_test_state_data_t *s)
{
    int voice;

    if (!s || !s->music_asset_ok || s->music_asset < 0)
        return -1;

    voice = audio_play_ex(s->music_asset,
                          s->music_volume,
                          s->music_speed,
                          1,
                          audio_mix_test_on_voice_started,
                          audio_mix_test_on_voice_stopped,
                          s);

    if (voice < 0) {
        LOGLNC(LOGCAT_AUDIO, "[state:audio_mix_test] music audio_play failed rc=%d", voice);
        return -1;
    }

    s->music_voice = voice;
    s->music_paused = 0;

    audio_voice_set_pan(s->music_voice, s->music_pan);

    LOGLNC(LOGCAT_AUDIO, "[state:audio_mix_test] music playing asset=%d voice=%d loop=1 pan=%d/100",
          s->music_asset,
          s->music_voice,
          (int)(s->music_pan * 100.0f));
    return 0;
}

static int audio_mix_test_open_music(audio_mix_test_state_data_t *s)
{
    if (!s)
        return -1;

    s->music_asset = audio_asset_load_stream(
        AUDIO_MIX_TEST_MUSIC_WAV_PATH,
        AUDIO_MIX_TEST_MUSIC_IO_BUFFER_BYTES
    );

    if (s->music_asset < 0) {
        LOGLNC(LOGCAT_AUDIO, "[state:audio_mix_test] audio_asset_load_stream failed: %d",
              s->music_asset);
        s->music_asset = -1;
        return -1;
    }

    s->music_asset_ok = 1;

    if (audio_asset_preload(s->music_asset) < 0) {
        LOGLNC(LOGCAT_AUDIO, "[state:audio_mix_test] audio_asset_preload failed: %d",
              s->music_asset);
        audio_asset_unload(s->music_asset);
        s->music_asset = -1;
        s->music_asset_ok = 0;
        return -1;
    }

    if (audio_mix_test_start_music_voice(s) < 0) {
        audio_asset_unload(s->music_asset);
        s->music_asset = -1;
        s->music_asset_ok = 0;
        return -1;
    }

    return 0;
}

static void audio_mix_test_load_sfx_one(const char *tag,
                                        const char *path,
                                        int *out_handle,
                                        int *out_ok)
{
    *out_handle = audio_asset_load_sfx(path);
    if (*out_handle < 0) {
        *out_ok = 0;
        LOGLNC(LOGCAT_AUDIO, "[state:audio_mix_test] %s load failed path=%s rc=%d",
              tag, path, *out_handle);
        return;
    }

    *out_ok = 1;
    LOGLNC(LOGCAT_AUDIO, "[state:audio_mix_test] %s loaded handle=%d path=%s",
          tag, *out_handle, path);
}

static void audio_mix_test_trigger_sfx(audio_mix_test_state_data_t *s,
                                       int sfx_handle,
                                       int sfx_ok,
                                       const char *tag)
{
    int voice;

    if (!sfx_ok || sfx_handle < 0) {
        LOGLNC(LOGCAT_AUDIO, "[state:audio_mix_test] %s unavailable", tag);
        return;
    }

    voice = audio_play_ex(sfx_handle,
                          100,
                          1.0f,
                          0,
                          audio_mix_test_on_voice_started,
                          audio_mix_test_on_voice_stopped,
                          s);

    if (voice < 0) {
        LOGLNC(LOGCAT_AUDIO, "[state:audio_mix_test] %s play failed rc=%d", tag, voice);
        return;
    }

    audio_voice_set_pan(voice, s->sfx_pan);

    s->last_sfx_voice = voice;
    s->sfx_trigger_count++;
    audio_mix_test_push_recent_voice(s, voice);
    s->overlay_dirty = 1;

    LOGLNC(LOGCAT_AUDIO, "[state:audio_mix_test] %s voice=%d pan=%d/100",
          tag, voice, (int)(s->sfx_pan * 100.0f));
}

static void audio_mix_test_trigger_burst(audio_mix_test_state_data_t *s,
                                         int sfx_handle,
                                         int sfx_ok,
                                         const char *tag,
                                         int count)
{
    int i;
    int ok_count = 0;

    if (!s || !sfx_ok || sfx_handle < 0) {
        LOGLNC(LOGCAT_AUDIO, "[state:audio_mix_test] %s burst unavailable", tag);
        return;
    }

    for (i = 0; i < count; i++) {
        int voice = audio_play_ex(sfx_handle,
                                  100,
                                  1.0f,
                                  0,
                                  audio_mix_test_on_voice_started,
                                  audio_mix_test_on_voice_stopped,
                                  s);

        if (voice < 0) {
            LOGLNC(LOGCAT_AUDIO, "[state:audio_mix_test] %s burst[%d] failed rc=%d",
                  tag, i, voice);
            continue;
        }

        audio_voice_set_pan(voice, s->sfx_pan);

        s->last_sfx_voice = voice;
        s->sfx_trigger_count++;
        audio_mix_test_push_recent_voice(s, voice);
        ok_count++;
    }

    LOGLNC(LOGCAT_AUDIO, "[state:audio_mix_test] %s burst count=%d ok=%d pan=%d/100",
          tag, count, ok_count, (int)(s->sfx_pan * 100.0f));

    s->overlay_dirty = 1;
}

static void audio_mix_test_update_page0_music_controls(audio_mix_test_state_data_t *s)
{
    if (!s)
        return;

    if (s->music_asset_ok && s->music_asset >= 0) {
        if (input_button_pressed(INPUT_BUTTON_CROSS)) {
            if (s->music_paused) {
                if (s->music_voice >= 0)
                    audio_voice_resume(s->music_voice);
                s->music_paused = 0;
                LOGLNC(LOGCAT_AUDIO, "[state:audio_mix_test] music resume");
            } else {
                if (s->music_voice >= 0)
                    audio_voice_pause(s->music_voice);
                s->music_paused = 1;
                LOGLNC(LOGCAT_AUDIO, "[state:audio_mix_test] music pause");
            }
            s->overlay_dirty = 1;
        }

        if (input_button_pressed(INPUT_BUTTON_TRIANGLE)) {
            if (s->music_voice >= 0 &&
                audio_voice_is_playing(s->music_voice)) {
                audio_voice_stop(s->music_voice);
                s->music_paused = 0;
                LOGLNC(LOGCAT_AUDIO, "[state:audio_mix_test] music stop");
            } else {
                if (audio_mix_test_start_music_voice(s) == 0)
                    LOGLNC(LOGCAT_AUDIO, "[state:audio_mix_test] music play");
            }
            s->overlay_dirty = 1;
        }

        if (input_button_pressed(INPUT_BUTTON_UP)) {
            s->music_volume += 10;
            if (s->music_volume > 100)
                s->music_volume = 100;
            if (s->music_voice >= 0)
                audio_voice_set_volume(s->music_voice, s->music_volume);
            LOGLNC(LOGCAT_AUDIO, "[state:audio_mix_test] music volume=%d", s->music_volume);
            s->overlay_dirty = 1;
        }

        if (input_button_pressed(INPUT_BUTTON_DOWN)) {
            s->music_volume -= 10;
            if (s->music_volume < 0)
                s->music_volume = 0;
            if (s->music_voice >= 0)
                audio_voice_set_volume(s->music_voice, s->music_volume);
            LOGLNC(LOGCAT_AUDIO, "[state:audio_mix_test] music volume=%d", s->music_volume);
            s->overlay_dirty = 1;
        }

        if (input_button_pressed(INPUT_BUTTON_RIGHT)) {
            s->music_speed += 0.25f;
            if (s->music_speed > 3.0f)
                s->music_speed = 3.0f;
            if (s->music_voice >= 0)
                audio_voice_set_speed(s->music_voice, s->music_speed);
            LOGLNC(LOGCAT_AUDIO, "[state:audio_mix_test] music speed=%d/100",
                  (int)(s->music_speed * 100.0f));
            s->overlay_dirty = 1;
        }

        if (input_button_pressed(INPUT_BUTTON_LEFT)) {
            s->music_speed -= 0.25f;
            if (s->music_speed < (1.0f / 3.0f))
                s->music_speed = (1.0f / 3.0f);
            if (s->music_voice >= 0)
                audio_voice_set_speed(s->music_voice, s->music_speed);
            LOGLNC(LOGCAT_AUDIO, "[state:audio_mix_test] music speed=%d/100",
                  (int)(s->music_speed * 100.0f));
            s->overlay_dirty = 1;
        }
    }
}

static void audio_mix_test_update_page1_pan_controls(audio_mix_test_state_data_t *s)
{
    if (!s)
        return;

    if (input_button_pressed(INPUT_BUTTON_RIGHT)) {
        s->music_pan = audio_mix_test_clamp_pan(s->music_pan + AUDIO_MIX_TEST_PAN_STEP);

        if (s->music_voice >= 0)
            audio_voice_set_pan(s->music_voice, s->music_pan);

        LOGLNC(LOGCAT_AUDIO, "[state:audio_mix_test] music pan=%d/100",
              (int)(s->music_pan * 100.0f));
        s->overlay_dirty = 1;
    }

    if (input_button_pressed(INPUT_BUTTON_LEFT)) {
        s->music_pan = audio_mix_test_clamp_pan(s->music_pan - AUDIO_MIX_TEST_PAN_STEP);

        if (s->music_voice >= 0)
            audio_voice_set_pan(s->music_voice, s->music_pan);

        LOGLNC(LOGCAT_AUDIO, "[state:audio_mix_test] music pan=%d/100",
              (int)(s->music_pan * 100.0f));
        s->overlay_dirty = 1;
    }

    if (input_button_pressed(INPUT_BUTTON_UP)) {
        s->sfx_pan = audio_mix_test_clamp_pan(s->sfx_pan + AUDIO_MIX_TEST_PAN_STEP);
        LOGLNC(LOGCAT_AUDIO, "[state:audio_mix_test] sfx pan=%d/100",
              (int)(s->sfx_pan * 100.0f));
        s->overlay_dirty = 1;
    }

    if (input_button_pressed(INPUT_BUTTON_DOWN)) {
        s->sfx_pan = audio_mix_test_clamp_pan(s->sfx_pan - AUDIO_MIX_TEST_PAN_STEP);
        LOGLNC(LOGCAT_AUDIO, "[state:audio_mix_test] sfx pan=%d/100",
              (int)(s->sfx_pan * 100.0f));
        s->overlay_dirty = 1;
    }

    if (input_button_pressed(INPUT_BUTTON_TRIANGLE)) {
        s->music_pan = 0.0f;
        s->sfx_pan = 0.0f;

        if (s->music_voice >= 0)
            audio_voice_set_pan(s->music_voice, s->music_pan);

        LOGLNC(LOGCAT_AUDIO, "[state:audio_mix_test] pans centered");
        s->overlay_dirty = 1;
    }
}

static void audio_mix_test_update_common_sfx_controls(audio_mix_test_state_data_t *s)
{
    if (!s)
        return;

    if (input_button_pressed(INPUT_BUTTON_SQUARE)) {
        audio_mix_test_trigger_sfx(s, s->sfx1, s->sfx1_ok, "sfx1");
    }

    if (input_button_pressed(INPUT_BUTTON_CIRCLE)) {
        audio_mix_test_trigger_sfx(s, s->sfx2, s->sfx2_ok, "sfx2");
    }

    if (input_button_pressed(INPUT_BUTTON_R1)) {
        audio_mix_test_trigger_sfx(s, s->sfx3, s->sfx3_ok, "sfx3");
    }

    if (input_button_pressed(INPUT_BUTTON_L1)) {
        audio_mix_test_trigger_burst(s,
                                     s->sfx1,
                                     s->sfx1_ok,
                                     "sfx1",
                                     AUDIO_MIX_TEST_BURST_COUNT);
    }

    if (input_button_pressed(INPUT_BUTTON_L2)) {
        s->page++;
        if (s->page > 1)
            s->page = 0;
        s->overlay_dirty = 1;
    }
}

static int audio_mix_test_enter(game_app_t *app, void *userdata)
{
    audio_mix_test_state_data_t *s;
    debug_overlay_desc_t overlay_desc;

    (void)userdata;

    s = (audio_mix_test_state_data_t *)mem_arena_calloc(
        game_app_state_arena(app),
        1,
        sizeof(*s),
        16
    );
    if (!s) {
        LOGLNC(LOGCAT_STATE, "[state:audio_mix_test] enter failed: no state arena memory");
        return -1;
    }

    game_app_set_state_userdata(app, s);
    audio_mix_test_reset_state(s);

    debug_overlay_desc_init(&overlay_desc);
    overlay_desc.x = 16;
    overlay_desc.y = 16;
    overlay_desc.w = 620;
    overlay_desc.h = 320;

    if (debug_overlay_init(app, &s->overlay, &overlay_desc) != 0) {
        LOGLNC(LOGCAT_STATE, "[state:audio_mix_test] overlay init failed");
        return -1;
    }

    LOGLNC(LOGCAT_STATE, "[state:audio_mix_test] enter");

    if (!audio_is_available()) {
        LOGLNC(LOGCAT_AUDIO, "[state:audio_mix_test] audio unavailable");
        audio_mix_test_rebuild_overlay(s);
        s->overlay_dirty = 0;
        return 0;
    }

    if (audio_mix_test_open_music(s) < 0) {
        audio_mix_test_rebuild_overlay(s);
        s->overlay_dirty = 0;
        return 0;
    }

    audio_mix_test_load_sfx_one("sfx1",
                                AUDIO_MIX_TEST_SFX1_WAV_PATH,
                                &s->sfx1,
                                &s->sfx1_ok);

    audio_mix_test_load_sfx_one("sfx2",
                                AUDIO_MIX_TEST_SFX2_WAV_PATH,
                                &s->sfx2,
                                &s->sfx2_ok);

    audio_mix_test_load_sfx_one("sfx3",
                                AUDIO_MIX_TEST_SFX3_WAV_PATH,
                                &s->sfx3,
                                &s->sfx3_ok);

    audio_mix_test_rebuild_overlay(s);
    s->overlay_dirty = 0;
    return 0;
}

static void audio_mix_test_exit(game_app_t *app)
{
    audio_mix_test_state_data_t *s = audio_mix_test_data(app);

    LOGLNC(LOGCAT_STATE, "[state:audio_mix_test] exit");

    if (!s)
        return;

    s->shutting_down = 1;

    audio_mix_test_detach_voice_callbacks(s);

    if (s->last_sfx_voice >= 0)
        audio_voice_stop(s->last_sfx_voice);

    if (s->sfx1_ok && s->sfx1 >= 0)
        audio_asset_unload(s->sfx1);
    if (s->sfx2_ok && s->sfx2 >= 0)
        audio_asset_unload(s->sfx2);
    if (s->sfx3_ok && s->sfx3 >= 0)
        audio_asset_unload(s->sfx3);

    if (s->music_voice >= 0)
        audio_voice_stop(s->music_voice);

    if (s->music_asset_ok && s->music_asset >= 0)
        audio_asset_unload(s->music_asset);

    debug_overlay_shutdown(app, &s->overlay);

    audio_mix_test_reset_state(s);
    s->overlay_dirty = 0;
    game_app_set_state_userdata(app, NULL);
}

static void audio_mix_test_fixed_update(game_app_t *app, float dt)
{
    (void)app;
    (void)dt;
}

static void audio_mix_test_update(game_app_t *app, float dt)
{
    audio_mix_test_state_data_t *s = audio_mix_test_data(app);
    int do_periodic_log = 0;

    if (!s)
        return;

    debug_overlay_update(app, &s->overlay, dt);

    s->uptime_sec += dt;

    if (input_button_pressed(INPUT_BUTTON_START)) {
        LOGLNC(LOGCAT_STATE, "[state:audio_mix_test] START pressed, return to menu");
        input_consume();
        game_app_request_state_change(debug_menu_state_desc(), NULL);
        return;
    }

    if (s->page == 0)
        audio_mix_test_update_page0_music_controls(s);
    else
        audio_mix_test_update_page1_pan_controls(s);

    audio_mix_test_update_common_sfx_controls(s);

    s->log_timer += dt;
    if (s->log_timer >= AUDIO_MIX_TEST_LOG_INTERVAL_SEC) {
        while (s->log_timer >= AUDIO_MIX_TEST_LOG_INTERVAL_SEC)
            s->log_timer -= AUDIO_MIX_TEST_LOG_INTERVAL_SEC;
        do_periodic_log = 1;
        s->overlay_dirty = 1;
    }

    if (do_periodic_log) {
        LOGLNC(LOGCAT_AUDIO, "[state:audio_mix_test] music playing=%d paused=%d vol=%d speed=%d/100 music_pan=%d/100 sfx_pan=%d/100 last_voice=%d last_voice_playing=%d recent_playing=%d sfx_count=%d",
              (s->music_voice >= 0) ? audio_voice_is_playing(s->music_voice) : 0,
              (s->music_voice >= 0) ? audio_voice_is_paused(s->music_voice) : 0,
              s->music_volume,
              (int)(s->music_speed * 100.0f),
              (int)(s->music_pan * 100.0f),
              (int)(s->sfx_pan * 100.0f),
              s->last_sfx_voice,
              (s->last_sfx_voice >= 0) ? audio_voice_is_playing(s->last_sfx_voice) : 0,
              audio_mix_test_count_recent_playing(s),
              s->sfx_trigger_count);
    }

    if (s->overlay_dirty) {
        audio_mix_test_rebuild_overlay(s);
        s->overlay_dirty = 0;
    }
}

static void audio_mix_test_draw(game_app_t *app, float alpha)
{
    audio_mix_test_state_data_t *s = audio_mix_test_data(app);

    (void)app;
    (void)alpha;

    if (s)
        debug_overlay_draw(&s->overlay);
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
