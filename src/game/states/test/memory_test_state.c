#include "game/states/test/debug_menu_state.h"

#include "engine/debug/screen_console.h"
#include "engine/input/input.h"
#include "engine/logging/log.h"
#include "engine/memory/memory.h"
#include "engine/memory/memory_arena.h"

#include <string.h>

typedef struct memory_test_state_data {
    float enter_time_sec;
    unsigned int alloc_free_runs;
    unsigned int overrun_runs;
    unsigned int underrun_runs;
    unsigned int leak_runs;
    int summary_printed;
    int exit_armed;
} memory_test_state_data_t;

static memory_test_state_data_t *memory_test_data(game_app_t *app)
{
    return GAME_APP_STATE_DATA_AS(app, memory_test_state_data_t);
}

static void memory_test_print_header(void)
{
    screen_console_begin(
        "memory_test",
        "CROSS=alloc/free  CIRCLE=overrun\n"
        "TRIANGLE=underrun SQUARE=leak\n"
        "START=summary/quit"
    );
    screen_console_printf("\n");
}

static void memory_test_print_summary(memory_test_state_data_t *data)
{
    if (!data || data->summary_printed)
        return;

    screen_console_printf("\nSUMMARY:\n");
    screen_console_printf("time_ms=%d alloc_free=%u overrun=%u underrun=%u leak=%u\n",
                          (int)(data->enter_time_sec * 1000.0f),
                          data->alloc_free_runs,
                          data->overrun_runs,
                          data->underrun_runs,
                          data->leak_runs);
    screen_console_printf("PRESS START TO EXIT\n");

    data->summary_printed = 1;
    data->exit_armed = 1;
}

static void run_alloc_free_test(game_app_t *app)
{
    memory_test_state_data_t *data = memory_test_data(app);
    void *p = mem_alloc(32, 16, MEMTAG_TEMP);

    if (data)
        data->alloc_free_runs++;

    if (!p) {
        LOGLN("[state:memory_test] alloc_free: allocation failed");
        screen_console_printf("alloc_free: allocation failed\n");
        return;
    }

    memset(p, 0x11, 32);
    mem_free(p, MEMTAG_TEMP);

    LOGLN("[state:memory_test] alloc_free: done, expected no memory errors");
    screen_console_printf("alloc_free: done, expected no memory errors\n");
}

static void run_overrun_test(game_app_t *app)
{
    memory_test_state_data_t *data = memory_test_data(app);
    unsigned char *p = (unsigned char *)mem_alloc(32, 16, MEMTAG_TEMP);

    if (data)
        data->overrun_runs++;

    if (!p) {
        LOGLN("[state:memory_test] overrun: allocation failed");
        screen_console_printf("overrun: allocation failed\n");
        return;
    }

    memset(p, 0x22, 32);
    p[32] = 0x99;

    LOGLN("[state:memory_test] overrun: wrote 1 byte past end, free expected to report overrun");
    screen_console_printf("overrun: wrote 1 byte past end, free expected to report overrun\n");

    mem_free(p, MEMTAG_TEMP);
}

static void run_underrun_test(game_app_t *app)
{
    memory_test_state_data_t *data = memory_test_data(app);
    unsigned char *p = (unsigned char *)mem_alloc(32, 16, MEMTAG_TEMP);

    if (data)
        data->underrun_runs++;

    if (!p) {
        LOGLN("[state:memory_test] underrun: allocation failed");
        screen_console_printf("underrun: allocation failed\n");
        return;
    }

    memset(p, 0x33, 32);
    p[-1] = 0x77;

    LOGLN("[state:memory_test] underrun: wrote 1 byte before start, free expected to report underrun");
    screen_console_printf("underrun: wrote 1 byte before start, free expected to report underrun\n");

    mem_free(p, MEMTAG_TEMP);
}

static void run_leak_test(game_app_t *app)
{
    memory_test_state_data_t *data = memory_test_data(app);
    void *p = mem_alloc(64, 16, MEMTAG_TEMP);

    if (data)
        data->leak_runs++;

    if (!p) {
        LOGLN("[state:memory_test] leak: allocation failed");
        screen_console_printf("leak: allocation failed\n");
        return;
    }

    memset(p, 0x44, 64);

    LOGLN("[state:memory_test] leak: intentionally leaked 64 bytes, exit should report leak");
    screen_console_printf("leak: intentionally leaked 64 bytes, exit should report leak\n");
}

static int memory_test_enter(game_app_t *app, void *userdata)
{
    memory_test_state_data_t *data;

    (void)userdata;

    data = (memory_test_state_data_t *)mem_arena_calloc(
        game_app_state_arena(app),
        1,
        sizeof(*data),
        16
    );
    if (!data) {
        LOGLN("[state:memory_test] enter failed: no state arena memory");
        return -1;
    }

    data->enter_time_sec = 0.0f;
    data->summary_printed = 0;
    data->exit_armed = 0;
    game_app_set_state_userdata(app, data);

    screen_console_enter();
    memory_test_print_header();

    LOGLN("[state:memory_test] enter");
    LOGLN("[state:memory_test] CROSS=alloc/free, CIRCLE=overrun, TRIANGLE=underrun, SQUARE=leak, START=quit");

    screen_console_printf("enter\n");
    screen_console_printf("CROSS=alloc/free, CIRCLE=overrun, TRIANGLE=underrun, SQUARE=leak\n");

    return 0;
}

static void memory_test_exit(game_app_t *app)
{
    memory_test_state_data_t *data = memory_test_data(app);

    if (data) {
        LOGLN("[state:memory_test] summary time_ms=%d alloc_free=%u overrun=%u underrun=%u leak=%u",
              (int)(data->enter_time_sec * 1000.0f),
              data->alloc_free_runs,
              data->overrun_runs,
              data->underrun_runs,
              data->leak_runs);
    }

    game_app_set_state_userdata(app, NULL);
    LOGLN("[state:memory_test] exit");
    screen_console_exit();
}

static void memory_test_fixed_update(game_app_t *app, float dt)
{
    (void)app;
    (void)dt;
}

static void memory_test_update(game_app_t *app, float dt)
{
    memory_test_state_data_t *data = memory_test_data(app);

    if (data)
        data->enter_time_sec += dt;

    if (input_button_pressed(INPUT_BUTTON_START)) {
        if (!data)
            return;

        if (!data->exit_armed) {
            LOGLN("[state:memory_test] START pressed, show summary");
            memory_test_print_summary(data);
            input_consume();
            return;
        }

        LOGLN("[state:memory_test] START pressed, return to menu");
        input_consume();
        game_app_request_state_change(debug_menu_state_desc(), NULL);
        return;
    }

    if (data && data->exit_armed)
        return;

    if (input_button_pressed(INPUT_BUTTON_CROSS))
        run_alloc_free_test(app);

    if (input_button_pressed(INPUT_BUTTON_CIRCLE))
        run_overrun_test(app);

    if (input_button_pressed(INPUT_BUTTON_TRIANGLE))
        run_underrun_test(app);

    if (input_button_pressed(INPUT_BUTTON_SQUARE))
        run_leak_test(app);
}

static void memory_test_draw(game_app_t *app, float alpha)
{
    (void)app;
    (void)alpha;
}

static const game_state_desc_t g_memory_test_state = {
    "memory_test",
    memory_test_enter,
    memory_test_exit,
    memory_test_fixed_update,
    memory_test_update,
    memory_test_draw
};

const game_state_desc_t *memory_test_state_desc(void)
{
    return &g_memory_test_state;
}