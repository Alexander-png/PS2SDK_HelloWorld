#include "game_app.h"
#include "engine/logging/log.h"
#include "engine/audio/audio.h"
#include "engine/input/input.h"
#include "engine/platform/platform.h"
#include "engine/streaming/streaming.h"
#include "engine/resources/texture_assets.h"
#include "engine/memory/memory_arena.h"
#include "engine/resources/resources.h"
#include "engine/gfx/gfx2d.h"
#include "game/states/test/debug_menu_state.h"

#include <string.h>

#ifndef GAME_APP_FIXED_DT
#define GAME_APP_FIXED_DT (1.0f / 60.0f)
#endif

#ifndef GAME_APP_RENDER_DT
#define GAME_APP_RENDER_DT (1.0f / 30.0f)
#endif

#ifndef GAME_APP_MAX_FRAME_DT
#define GAME_APP_MAX_FRAME_DT (0.25f)
#endif

#ifndef GAME_APP_MAX_FIXED_STEPS
#define GAME_APP_MAX_FIXED_STEPS 4
#endif

#ifndef GAME_APP_IDLE_SLEEP_US
#define GAME_APP_IDLE_SLEEP_US 1000
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

    const game_state_desc_t *pending_state;
    void *pending_state_userdata;
    int has_pending_state_change;

    mem_arena_t temp_arena;
    mem_arena_t state_arena;

    unsigned int frame_index;

    float frame_dt;
    float fixed_dt;
    float accumulator;
    float max_frame_dt;
    int max_fixed_steps_per_frame;

    float render_dt;
    float render_accumulator;

    unsigned long long last_time_us;
};

static game_app_t g_app;

static int empty_state_enter(game_app_t *app, void *userdata)
{
    (void)app;
    (void)userdata;
    LOGLNC(LOGCAT_APP, "[state:empty] enter");
    return 0;
}

static void empty_state_exit(game_app_t *app)
{
    (void)app;
    LOGLNC(LOGCAT_APP, "[state:empty] exit");
}

static void empty_state_fixed_update(game_app_t *app, float dt)
{
    (void)app;
    (void)dt;
}

static void empty_state_update(game_app_t *app, float dt)
{
    (void)app;
    (void)dt;
}

static void empty_state_draw(game_app_t *app, float alpha)
{
    (void)app;
    (void)alpha;
}

static const game_state_desc_t g_empty_state = {
    "empty",
    empty_state_enter,
    empty_state_exit,
    empty_state_fixed_update,
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
        LOGLNC(LOGCAT_APP, "[game_app] failed to init fallback empty state arena");
        return -1;
    }

    g_app.state = &g_empty_state;
    g_app.state_userdata = NULL;

    if (g_app.state->enter) {
        rc = g_app.state->enter(&g_app, NULL);
        if (rc < 0) {
            LOGLNC(LOGCAT_APP, "[game_app] fallback empty state enter failed rc=%d", rc);
            return -1;
        }
    }

    return 0;
}

static int game_app_apply_pending_state_change(void)
{
    const game_state_desc_t *pending_state;
    void *pending_userdata;
    int change_rc;

    if (!g_app.has_pending_state_change)
        return 0;

    pending_state = g_app.pending_state;
    pending_userdata = g_app.pending_state_userdata;

    g_app.pending_state = NULL;
    g_app.pending_state_userdata = NULL;
    g_app.has_pending_state_change = 0;

    change_rc = game_app_change_state(pending_state, pending_userdata);
    if (change_rc < 0) {
        LOGLNC(LOGCAT_APP, "[game_app] deferred state change failed rc=%d", change_rc);
        return change_rc;
    }

    input_consume();
    return 0;
}

int game_app_init(void)
{
    int rc;

    memset(&g_app, 0, sizeof(g_app));

    g_app.initialized = 1;
    g_app.running = 1;
    g_app.frame_dt = GAME_APP_FIXED_DT;
    g_app.fixed_dt = GAME_APP_FIXED_DT;
    g_app.accumulator = 0.0f;
    g_app.max_frame_dt = GAME_APP_MAX_FRAME_DT;
    g_app.max_fixed_steps_per_frame = GAME_APP_MAX_FIXED_STEPS;
    g_app.render_dt = GAME_APP_RENDER_DT;
    g_app.render_accumulator = 0.0f;
    g_app.state = &g_empty_state;
    g_app.state_userdata = NULL;

    LOGLNC(LOGCAT_APP, "[game_app] init");

    if (mem_arena_init(&g_app.temp_arena, GAME_APP_TEMP_ARENA_SIZE, MEMTAG_TEMP) != 0) {
        LOGLNC(LOGCAT_APP, "[game_app] failed to init temp arena");
        memset(&g_app, 0, sizeof(g_app));
        return -1;
    }

    game_app_clear_state_arena(&g_app.state_arena);

    g_app.last_time_us = platform_time_now_us();

    rc = game_app_change_state(debug_menu_state_desc(), NULL);
    if (rc < 0) {
        LOGLNC(LOGCAT_APP, "[game_app] failed to enter initial state: %d", rc);
        mem_arena_destroy(&g_app.state_arena);
        mem_arena_destroy(&g_app.temp_arena);
        memset(&g_app, 0, sizeof(g_app));
        return rc;
    }

    LOGLNC(LOGCAT_APP, "[game_app] ready");
    return 0;
}

void game_app_shutdown(void)
{
    if (!g_app.initialized)
        return;

    LOGLNC(LOGCAT_APP, "[game_app] shutdown");

    if (g_app.state && g_app.state->exit)
        g_app.state->exit(&g_app);

    mem_arena_destroy(&g_app.state_arena);
    mem_arena_destroy(&g_app.temp_arena);

    memset(&g_app, 0, sizeof(g_app));
}

void game_app_tick(void)
{
    unsigned long long now_us;
    float frame_dt;
    int fixed_steps = 0;
    int should_render = 0;

    if (!g_app.initialized || !g_app.running)
        return;

    now_us = platform_time_now_us();

    if (g_app.last_time_us == 0)
        g_app.last_time_us = now_us;

    frame_dt = (float)(now_us - g_app.last_time_us) / 1000000.0f;
    g_app.last_time_us = now_us;

    if (frame_dt < 0.0f)
        frame_dt = 0.0f;
    if (frame_dt > g_app.max_frame_dt)
        frame_dt = g_app.max_frame_dt;

    g_app.frame_dt = frame_dt;
    g_app.accumulator += frame_dt;
    g_app.render_accumulator += frame_dt;

    input_update();

    if (g_app.state && g_app.state->update)
        g_app.state->update(&g_app, frame_dt);

    streaming_update();
    resources_update();
    texture_assets_update();
    audio_update(frame_dt);

    while (g_app.accumulator >= g_app.fixed_dt &&
           fixed_steps < g_app.max_fixed_steps_per_frame) {
        if (g_app.state && g_app.state->fixed_update)
            g_app.state->fixed_update(&g_app, g_app.fixed_dt);

        g_app.accumulator -= g_app.fixed_dt;
        fixed_steps++;
    }

    if (g_app.accumulator >= g_app.fixed_dt) {
        LOGLNC(LOGCAT_APP, "[game_app] fixed-step overload: dropping lag");
        g_app.accumulator = 0.0f;
    }

    if (g_app.render_accumulator >= g_app.render_dt) {
        should_render = 1;
        g_app.render_accumulator -= g_app.render_dt;

        if (g_app.render_accumulator >= g_app.render_dt)
            g_app.render_accumulator = 0.0f;
    }

    if (should_render) {
        float alpha = 0.0f;

        gfx2d_begin_frame();

        if (g_app.state && g_app.state->draw) {
            if (g_app.fixed_dt > 0.0f)
                alpha = g_app.accumulator / g_app.fixed_dt;

            if (alpha < 0.0f) alpha = 0.0f;
            if (alpha > 1.0f) alpha = 1.0f;

            g_app.state->draw(&g_app, alpha);
        }

        gfx2d_end_frame();
        g_app.frame_index++;
    } else {
        platform_delay_us(GAME_APP_IDLE_SLEEP_US);
    }

    game_app_apply_pending_state_change();
    mem_arena_reset(&g_app.temp_arena);
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

    if (g_app.state == state && g_app.state_userdata == userdata)
        return 0;

    LOGLNC(LOGCAT_APP, "[game_app] state change: %s -> %s",
          g_app.state && g_app.state->name ? g_app.state->name : "none",
          state->name ? state->name : "unnamed");

    if (g_app.state && g_app.state->exit)
        g_app.state->exit(&g_app);

    mem_arena_destroy(&g_app.state_arena);
    game_app_clear_state_arena(&g_app.state_arena);

    if (mem_arena_init(&g_app.state_arena, GAME_APP_STATE_ARENA_SIZE, MEMTAG_STATE) != 0) {
        LOGLNC(LOGCAT_APP, "[game_app] failed to init state arena");

        if (game_app_enter_fallback_empty_state() != 0)
            return -1;

        return -1;
    }

    g_app.state = state;
    g_app.state_userdata = userdata;

    if (g_app.state->enter) {
        rc = g_app.state->enter(&g_app, userdata);
        if (rc < 0) {
            LOGLNC(LOGCAT_APP, "[game_app] state enter failed: %s rc=%d",
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

int game_app_request_state_change(const game_state_desc_t *state, void *userdata)
{
    if (!g_app.initialized)
        return -1;

    if (!state)
        state = &g_empty_state;

    g_app.pending_state = state;
    g_app.pending_state_userdata = userdata;
    g_app.has_pending_state_change = 1;
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
    return g_app.frame_dt;
}

float game_app_fixed_delta_time(void)
{
    return g_app.fixed_dt;
}

float game_app_draw_alpha(void)
{
    if (g_app.fixed_dt <= 0.0f)
        return 0.0f;

    return g_app.accumulator / g_app.fixed_dt;
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