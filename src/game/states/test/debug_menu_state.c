#include "engine/logging/log.h"
#include "engine/gfx/gfx2d.h"
#include "engine/input/input.h"
#include "engine/memory/memory_arena.h"

#include "game/states/test/audio_test_state.h"
#include "game/states/test/resource_test_state.h"
#include "game/states/test/sprite_test_state.h"
#include "game/states/test/memory_test_state.h"
#include "game/states/test/memory_arena_test_state.h"

#include <debug.h>

typedef struct debug_menu_state_data {
    int selected;
} debug_menu_state_data_t;

typedef struct debug_menu_entry {
    const char *label;
    const game_state_desc_t *(*state_fn)(void);
} debug_menu_entry_t;

static const debug_menu_entry_t g_menu[] = {
    { "Sprite Test",        sprite_test_state_desc },
    { "Audio Test",         audio_test_state_desc },
    { "Resource Test",      resource_test_state_desc },
    { "Memory Test",        memory_test_state_desc },
    { "Memory Arena Test",  memory_arena_test_state_desc },
};

#define DEBUG_MENU_COUNT ((int)(sizeof(g_menu) / sizeof(g_menu[0])))

static debug_menu_state_data_t *debug_menu_data(game_app_t *app)
{
    return GAME_APP_STATE_DATA_AS(app, debug_menu_state_data_t);
}

static void debug_menu_render(debug_menu_state_data_t *data)
{
    int i;

    scr_setXY(0, 0);
    scr_clear();

    scr_printf("=== Debug Menu ===\n");
    scr_printf("\n");
    scr_printf("UP/DOWN : select\n");
    scr_printf("CROSS   : enter state\n");
    scr_printf("START   : quit app\n");
    scr_printf("\n");

    for (i = 0; i < DEBUG_MENU_COUNT; ++i) {
        scr_printf("%c %s\n",
                   (data && data->selected == i) ? '>' : ' ',
                   g_menu[i].label);
    }
}

static int debug_menu_enter(game_app_t *app, void *userdata)
{
    debug_menu_state_data_t *data;

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

    data->selected = 0;
    game_app_set_state_userdata(app, data);

    // scr_printf prints output into draw queue and
    // setup debug screen subsystem, that breaks gsKit.
    // So on entering debug menu:
    // call gfx2d_shutdown
    // And on exiting:
    // call gfx2d_init
    gfx2d_shutdown();
    log_enable_screen(1);
    init_scr();
    scr_clear();

    LOGLN("[state:debug_menu] enter");
    debug_menu_render(data);
    return 0;
}

static void debug_menu_exit(game_app_t *app)
{
    (void)app;

    input_consume();

    scr_clear();
    log_enable_screen(0);
    if (gfx2d_init() < 0) {
        LOGLN("[state:debug_menu] gfx2d reinit failed");
    } else {
        LOGLN("[state:debug_menu] gfx2d reinit ok");
    }

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

    (void)dt;

    if (!data)
        return;

    if (input_button_pressed(INPUT_BUTTON_START)) {
        LOGLN("[state:debug_menu] START pressed, quit");
        game_app_request_quit();
        return;
    }

    if (input_button_pressed(INPUT_BUTTON_UP)) {
        data->selected--;
        if (data->selected < 0)
            data->selected = DEBUG_MENU_COUNT - 1;
        debug_menu_render(data);
    }

    if (input_button_pressed(INPUT_BUTTON_DOWN)) {
        data->selected++;
        if (data->selected >= DEBUG_MENU_COUNT)
            data->selected = 0;
        debug_menu_render(data);
    }

    if (input_button_pressed(INPUT_BUTTON_CROSS)) {
        const game_state_desc_t *next = g_menu[data->selected].state_fn();
        LOGLN("[state:debug_menu] enter item=%d name=%s",
              data->selected,
              next && next->name ? next->name : "unnamed");
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

    /* scr_printf menu rendered from update/enter only */
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
