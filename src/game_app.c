#include "game_app.h"
#include "engine/logging/log.h"
#include "engine/audio/audio.h"
#include "engine/input/input.h"
#include "engine/platform/platform.h"
#include "engine/streaming/streaming.h"
#include "engine/streaming/texture_assets.h"
#include "engine/resources/resources.h"
#include "engine/gfx/gfx2d.h"
#include "game/states/test/audio_test_state.h"
#include "game/states/test/resource_test_state.h"
#include "game/states/test/sprite_test_state.h"

#include <string.h>

#ifndef GAME_APP_FIXED_DT
#define GAME_APP_FIXED_DT (1.0f / 60.0f)
#endif

#ifndef GAME_APP_FRAME_DELAY_US
#define GAME_APP_FRAME_DELAY_US 16666
#endif

struct game_app {
    int initialized;
    int running;

    const game_state_desc_t *state;
    void *state_userdata;

    unsigned int frame_index;
    float dt;
};

static game_app_t g_app;

static int empty_state_enter(game_app_t *app, void *userdata)
{
    (void)app;
    (void)userdata;

    LOGLN("[state:empty] enter");
    return 0;
}

static void empty_state_exit(game_app_t *app)
{
    (void)app;

    LOGLN("[state:empty] exit");
}

static void empty_state_update(game_app_t *app, float dt)
{
    (void)app;
    (void)dt;
}

static void empty_state_draw(game_app_t *app)
{
    (void)app;
}

static const game_state_desc_t g_empty_state = {
    "empty",
    empty_state_enter,
    empty_state_exit,
    empty_state_update,
    empty_state_draw
};

int game_app_init(void)
{
    int rc;

    memset(&g_app, 0, sizeof(g_app));

    g_app.initialized = 1;
    g_app.running = 1;
    g_app.dt = GAME_APP_FIXED_DT;

    LOGLN("[game_app] init");

    //rc = game_app_change_state(sprite_test_state_desc(), NULL);
    rc = game_app_change_state(audio_test_state_desc(), NULL);
    //rc = game_app_change_state(resource_test_state_desc(), NULL);
    if (rc < 0) {
        LOGLN("[game_app] failed to enter initial state: %d", rc);
        memset(&g_app, 0, sizeof(g_app));
        return rc;
    }

    LOGLN("[game_app] ready");
    return 0;
}

void game_app_shutdown(void)
{
    if (!g_app.initialized)
        return;

    LOGLN("[game_app] shutdown");

    if (g_app.state && g_app.state->exit)
        g_app.state->exit(&g_app);

    memset(&g_app, 0, sizeof(g_app));
}

void game_app_tick(void)
{
    if (!g_app.initialized || !g_app.running)
        return;

    /*
     * Current first-pass order:
     *
     *   audio_update
     *   state update
     *   state draw
     *
     * Later this becomes:
     *
     *   input_update
     *   streaming_update
     *   resource_manager_update
     *   audio_update
     *   state update
     *   scene2d_update
     *   animation_update
     *   gfx2d_begin_frame
     *   state draw
     *   debug_overlay_drawe
     *   gfx2d_end_frame
     */

    input_update();

    streaming_update();
    resources_update();
    texture_assets_update();

    audio_update(g_app.dt);

    if (g_app.state && g_app.state->update)
        g_app.state->update(&g_app, g_app.dt);

    gfx2d_begin_frame();

    if (g_app.state && g_app.state->draw)
        g_app.state->draw(&g_app);

    gfx2d_end_frame();

    g_app.frame_index++;

    /*
     * Temporary pacing until vblank/timer-based frame timing exists.
     */     
    platform_delay_us(GAME_APP_FRAME_DELAY_US);
}

int game_app_is_running(void)
{
    return g_app.initialized && g_app.running;
}

void game_app_request_quit(void)
{
    if (!g_app.initialized)
        return;

    g_app.running = 0;
}

int game_app_change_state(const game_state_desc_t *state, void *userdata)
{
    int rc;

    if (!g_app.initialized)
        return -1;

    if (!state)
        state = &g_empty_state;

    if (g_app.state == state)
        return 0;

    LOGLN("[game_app] state change: %s -> %s",
          g_app.state && g_app.state->name ? g_app.state->name : "none",
          state->name ? state->name : "unnamed");

    if (g_app.state && g_app.state->exit)
        g_app.state->exit(&g_app);

    g_app.state = state;
    g_app.state_userdata = userdata;

    if (g_app.state->enter) {
        rc = g_app.state->enter(&g_app, userdata);
        if (rc < 0) {
            LOGLN("[game_app] state enter failed: %s rc=%d",
                  g_app.state->name ? g_app.state->name : "unnamed",
                  rc);

            g_app.state = &g_empty_state;
            g_app.state_userdata = NULL;

            if (g_app.state->enter)
                g_app.state->enter(&g_app, NULL);

            return rc;
        }
    }

    return 0;
}

const char *game_app_current_state_name(void)
{
    if (!g_app.state || !g_app.state->name)
        return "none";

    return g_app.state->name;
}

unsigned int game_app_frame_index(void)
{
    return g_app.frame_index;
}

float game_app_delta_time(void)
{
    return g_app.dt;
}
