#include "game_app.h"
#include "engine/logging/log.h"
#include "engine/audio/audio.h"
#include "engine/input/input.h"
#include "engine/platform/platform.h"
#include "engine/streaming/streaming.h"
#include "engine/streaming/texture_assets.h"
#include "engine/memory/memory_arena.h"
#include "engine/resources/resources.h"
#include "engine/gfx/gfx2d.h"
#include "game/states/test/audio_test_state.h"
#include "game/states/test/resource_test_state.h"
#include "game/states/test/sprite_test_state.h"
#include "game/states/test/memory_test_state.h"
#include "game/states/test/memory_arena_test_state.h"

#include <string.h>

#ifndef GAME_APP_FIXED_DT
#define GAME_APP_FIXED_DT (1.0f / 60.0f)
#endif

#ifndef GAME_APP_FRAME_DELAY_US
#define GAME_APP_FRAME_DELAY_US 16666
#endif

#ifndef GAME_APP_TEMP_ARENA_SIZE
#define GAME_APP_TEMP_ARENA_SIZE (64 * 1024)
#endif

#ifndef GAME_APP_STATE_ARENA_SIZE
#define GAME_APP_STATE_ARENA_SIZE (64 * 1024)
#endif

struct game_app {
    int initialized;
    int running;

    const game_state_desc_t *state;
    void *state_userdata;

    mem_arena_t temp_arena;
    mem_arena_t state_arena;

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

static void game_app_clear_state_arena(mem_arena_t *arena)
{
    if (!arena)
        return;

    memset(arena, 0, sizeof(*arena));
    arena->tag = MEMTAG_STATE;
}

static int game_app_enter_fallback_empty_state(void)
{
    int rc = 0;

    game_app_clear_state_arena(&g_app.state_arena);

    if (mem_arena_init(&g_app.state_arena, GAME_APP_STATE_ARENA_SIZE, MEMTAG_STATE) != 0) {
        LOGLN("[game_app] failed to init fallback empty state arena");
        return -1;
    }

    g_app.state = &g_empty_state;
    g_app.state_userdata = NULL;

    if (g_app.state->enter) {
        rc = g_app.state->enter(&g_app, NULL);
        if (rc < 0) {
            LOGLN("[game_app] fallback empty state enter failed rc=%d", rc);
            return -1;
        }
    }

    return 0;
}

int game_app_init(void)
{
    int rc;

    memset(&g_app, 0, sizeof(g_app));

    g_app.initialized = 1;
    g_app.running = 1;
    g_app.dt = GAME_APP_FIXED_DT;
    g_app.state = &g_empty_state;
    g_app.state_userdata = NULL;

    LOGLN("[game_app] init");

    if (mem_arena_init(&g_app.temp_arena, GAME_APP_TEMP_ARENA_SIZE, MEMTAG_TEMP) != 0) {
        LOGLN("[game_app] failed to init temp arena");
        memset(&g_app, 0, sizeof(g_app));
        return -1;
    }

    game_app_clear_state_arena(&g_app.state_arena);

    //rc = game_app_change_state(sprite_test_state_desc(), NULL);
    //rc = game_app_change_state(audio_test_state_desc(), NULL);
    //rc = game_app_change_state(resource_test_state_desc(), NULL);
    rc = game_app_change_state(memory_test_state_desc(), NULL);
    //rc = game_app_change_state(memory_arena_test_state_desc(), NULL);

    if (rc < 0) {
        LOGLN("[game_app] failed to enter initial state: %d", rc);

        mem_arena_destroy(&g_app.state_arena);
        mem_arena_destroy(&g_app.temp_arena);
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

    mem_arena_destroy(&g_app.state_arena);
    mem_arena_destroy(&g_app.temp_arena);

    memset(&g_app, 0, sizeof(g_app));
}

void game_app_tick(void)
{
    if (!g_app.initialized || !g_app.running)
        return;

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

    mem_arena_reset(&g_app.temp_arena);

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

    mem_arena_destroy(&g_app.state_arena);
    game_app_clear_state_arena(&g_app.state_arena);

    if (mem_arena_init(&g_app.state_arena, GAME_APP_STATE_ARENA_SIZE, MEMTAG_STATE) != 0) {
        LOGLN("[game_app] failed to init state arena");

        if (game_app_enter_fallback_empty_state() != 0)
            return -1;

        return -1;
    }

    g_app.state = state;
    g_app.state_userdata = userdata;

    if (g_app.state->enter) {
        rc = g_app.state->enter(&g_app, userdata);
        if (rc < 0) {
            LOGLN("[game_app] state enter failed: %s rc=%d",
                  g_app.state->name ? g_app.state->name : "unnamed",
                  rc);

            if (g_app.state->exit)
                g_app.state->exit(&g_app);

            mem_arena_destroy(&g_app.state_arena);

            if (game_app_enter_fallback_empty_state() != 0)
                return -1;

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

mem_arena_t *game_app_temp_arena(game_app_t *app)
{
    if (!app)
        return NULL;
    return &app->temp_arena;
}

mem_arena_t *game_app_state_arena(game_app_t *app)
{
    if (!app)
        return NULL;
    return &app->state_arena;
}

void *game_app_state_userdata(game_app_t *app)
{
    if (!app)
        return NULL;
    return app->state_userdata;
}

void game_app_set_state_userdata(game_app_t *app, void *userdata)
{
    if (!app)
        return;
    app->state_userdata = userdata;
}