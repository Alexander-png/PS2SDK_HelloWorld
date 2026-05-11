#include "resource_test_state.h"
#include "engine/logging/log.h"
#include "engine/input/input.h"

static int resource_test_enter(game_app_t *app, void *userdata)
{
    (void)app;
    (void)userdata;

    
    return 0;
}

static void resource_test_exit(game_app_t *app)
{
    (void)app;

    LOGLN("[state:resource_test] exit");
}

static void resource_test_update(game_app_t *app, float dt)
{
    (void)dt;

    if (input_button_pressed(INPUT_BUTTON_START)) {
        LOGLN("[state:resource_test] START pressed, quit");
        game_app_request_quit();
        return;
    }
}

static void resource_test_draw(game_app_t *app)
{
    (void)app;
}

static const game_state_desc_t g_resource_test_state = {
    "resource_test",
    resource_test_enter,
    resource_test_exit,
    resource_test_update,
    resource_test_draw
};

const game_state_desc_t *resource_test_state_desc(void)
{
    return &g_resource_test_state;
}
