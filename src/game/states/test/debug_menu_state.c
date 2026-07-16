#include "game/states/test/debug_menu_state.h"

#include "engine/debug/debug_overlay.h"
#include "engine/input/input.h"
#include "engine/logging/log.h"
#include "engine/memory/memory_arena.h"
#include "engine/gfx/gfx2d.h"

#include "game/states/test/audio_test_state.h"
#include "game/states/test/resource_test_state.h"
#include "game/states/test/sprite_test_state.h"
#include "game/states/test/memory_test_state.h"
#include "game/states/test/memory_arena_test_state.h"
#include "game/states/test/audio_mix_test_state.h"
#include "game/states/test/text_test_state.h"

#include <stdio.h>

typedef struct debug_menu_entry {
    const char *label;
    const game_state_desc_t *(*state_fn)(void);
} debug_menu_entry_t;

static const debug_menu_entry_t g_menu[] = {
    { "Sprite Test",       sprite_test_state_desc },
    { "Audio Test",        audio_test_state_desc },
    { "Resource Test",     resource_test_state_desc },
    { "Memory Test",       memory_test_state_desc },
    { "Memory Arena Test", memory_arena_test_state_desc },
    { "Audio Mix Test",    audio_mix_test_state_desc },
    { "Text Test",         text_test_state_desc },
};

#define DEBUG_MENU_COUNT ((int)(sizeof(g_menu) / sizeof(g_menu[0])))

typedef struct debug_menu_state_data {
    int selected;
    debug_overlay_t overlay;
} debug_menu_state_data_t;

static debug_menu_state_data_t *debug_menu_data(game_app_t *app)
{
    return GAME_APP_STATE_DATA_AS(app, debug_menu_state_data_t);
}

static int debug_menu_rebuild_overlay(debug_menu_state_data_t *data)
{
    int i;
    int off = 0;
    char buf[DEBUG_OVERLAY_TEXT_CAPACITY];

    if (!data)
        return -1;

    off += snprintf(buf + off, sizeof(buf) - off,
                    "=== Debug Menu ===\n"
                    "\n"
                    "UP/DOWN : select\n"
                    "CROSS   : enter state\n"
                    "START   : quit app\n"
                    "\n");

    for (i = 0; i < DEBUG_MENU_COUNT; ++i) {
        if (off >= (int)sizeof(buf))
            break;

        off += snprintf(buf + off, sizeof(buf) - off,
                        "%c %s\n",
                        (data->selected == i) ? '>' : ' ',
                        g_menu[i].label);
    }

    if (off < (int)sizeof(buf)) {
        off += snprintf(buf + off, sizeof(buf) - off,
                        "\n"
                        "Output: console\n");
    }

    buf[sizeof(buf) - 1] = '\0';
    return debug_overlay_set_text(&data->overlay, buf);
}

static int debug_menu_enter(game_app_t *app, void *userdata)
{
    debug_menu_state_data_t *data;
    debug_overlay_desc_t overlay_desc;

    (void)userdata;

    data = (debug_menu_state_data_t *)mem_arena_calloc(
        game_app_state_arena(app),
        1,
        sizeof(*data),
        16
    );
    if (!data) {
        LOGLN("[state:debug_menu] enter failed: no state arena memory");
        return -1;
    }

    game_app_set_state_userdata(app, data);

    data->selected = 0;

    debug_overlay_desc_init(&overlay_desc);
    overlay_desc.x = 24;
    overlay_desc.y = 24;
    overlay_desc.w = 560;
    overlay_desc.h = 400;

    if (debug_overlay_init(app, &data->overlay, &overlay_desc) != 0) {
        LOGLN("[state:debug_menu] overlay init failed");
        return -1;
    }

    if (debug_menu_rebuild_overlay(data) != 0) {
        LOGLN("[state:debug_menu] initial overlay build failed");
        return -1;
    }

    LOGLN("[state:debug_menu] enter");
    return 0;
}

static void debug_menu_exit(game_app_t *app)
{
    debug_menu_state_data_t *data = debug_menu_data(app);

    if (data)
        debug_overlay_shutdown(app, &data->overlay);

    game_app_set_state_userdata(app, NULL);
    LOGLN("[state:debug_menu] exit");
}

static void debug_menu_fixed_update(game_app_t *app, float dt)
{
    (void)app;
    (void)dt;
}

static void debug_menu_update(game_app_t *app, float dt)
{
    debug_menu_state_data_t *data = debug_menu_data(app);

    if (!data)
        return;

    debug_overlay_update(app, &data->overlay, dt);

    if (input_button_pressed(INPUT_BUTTON_START)) {
        LOGLN("[state:debug_menu] START pressed, quit");
        input_consume();
        game_app_request_quit();
        return;
    }

    if (input_button_pressed(INPUT_BUTTON_UP)) {
        data->selected--;
        if (data->selected < 0)
            data->selected = DEBUG_MENU_COUNT - 1;

        if (debug_menu_rebuild_overlay(data) != 0)
            LOGLN("[state:debug_menu] overlay rebuild failed after UP");

        input_consume();
    }

    if (input_button_pressed(INPUT_BUTTON_DOWN)) {
        data->selected++;
        if (data->selected >= DEBUG_MENU_COUNT)
            data->selected = 0;

        if (debug_menu_rebuild_overlay(data) != 0)
            LOGLN("[state:debug_menu] overlay rebuild failed after DOWN");

        input_consume();
    }

    if (input_button_pressed(INPUT_BUTTON_CROSS)) {
        const game_state_desc_t *next = g_menu[data->selected].state_fn();

        LOGLN("[state:debug_menu] enter item=%d name=%s",
              data->selected,
              next && next->name ? next->name : "unnamed");

        input_consume();
        game_app_request_state_change(next, NULL);
        return;
    }
}

static void debug_menu_draw(game_app_t *app, float alpha)
{
    debug_menu_state_data_t *data = debug_menu_data(app);

    (void)app;
    (void)alpha;

    if (!data)
        return;

    gfx2d_draw();
    debug_overlay_draw(&data->overlay);
}

static const game_state_desc_t s_debug_menu_state = {
    "debug_menu",
    debug_menu_enter,
    debug_menu_exit,
    debug_menu_fixed_update,
    debug_menu_update,
    debug_menu_draw
};

const game_state_desc_t *debug_menu_state_desc(void)
{
    return &s_debug_menu_state;
}