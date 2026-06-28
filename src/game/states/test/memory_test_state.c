#include "memory_test_state.h"

#include "engine/logging/log.h"
#include "engine/input/input.h"
#include "engine/memory/memory.h"
#include "engine/memory/memory_arena.h"

#include <string.h>

typedef struct memory_test_state_data {
    unsigned int enter_frame;
    unsigned int alloc_free_runs;
    unsigned int overrun_runs;
    unsigned int underrun_runs;
    unsigned int leak_runs;
} memory_test_state_data_t;

static memory_test_state_data_t *memory_test_data(game_app_t *app)
{
    return (memory_test_state_data_t *)game_app_state_userdata(app);
}

static void run_alloc_free_test(game_app_t *app)
{
    memory_test_state_data_t *data = memory_test_data(app);
    void *p = mem_alloc(32, 16, MEMTAG_TEMP);

    if (data)
        data->alloc_free_runs++;

    if (!p) {
        LOGLN("[state:memory_test] alloc_free: allocation failed");
        return;
    }

    memset(p, 0x11, 32);
    mem_free(p, MEMTAG_TEMP);
    LOGLN("[state:memory_test] alloc_free: done, expected no memory errors");
}

static void run_overrun_test(game_app_t *app)
{
    memory_test_state_data_t *data = memory_test_data(app);
    unsigned char *p = (unsigned char *)mem_alloc(32, 16, MEMTAG_TEMP);

    if (data)
        data->overrun_runs++;

    if (!p) {
        LOGLN("[state:memory_test] overrun: allocation failed");
        return;
    }

    memset(p, 0x22, 32);
    p[32] = 0x99;
    LOGLN("[state:memory_test] overrun: wrote 1 byte past end, free expected to report overrun");
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
        return;
    }

    memset(p, 0x33, 32);
    p[-1] = 0x77;
    LOGLN("[state:memory_test] underrun: wrote 1 byte before start, free expected to report underrun");
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
        return;
    }

    memset(p, 0x44, 64);
    LOGLN("[state:memory_test] leak: intentionally leaked 64 bytes, shutdown should report leak");
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

    data->enter_frame = game_app_frame_index();
    game_app_set_state_userdata(app, data);

    LOGLN("[state:memory_test] enter");
    LOGLN("[state:memory_test] CROSS=alloc/free, CIRCLE=overrun, TRIANGLE=underrun, SQUARE=leak, START=quit");
    return 0;
}

static void memory_test_exit(game_app_t *app)
{
    memory_test_state_data_t *data = memory_test_data(app);

    if (data) {
        LOGLN("[state:memory_test] summary enter_frame=%u alloc_free=%u overrun=%u underrun=%u leak=%u",
              data->enter_frame,
              data->alloc_free_runs,
              data->overrun_runs,
              data->underrun_runs,
              data->leak_runs);
    }

    game_app_set_state_userdata(app, NULL);
    LOGLN("[state:memory_test] exit");
}

static void memory_test_update(game_app_t *app, float dt)
{
    (void)dt;

    if (input_button_down(INPUT_BUTTON_START)) {
        LOGLN("[state:memory_test] START pressed, quit");
        game_app_request_quit();
        return;
    }

    if (input_button_down(INPUT_BUTTON_CROSS))
        run_alloc_free_test(app);

    if (input_button_down(INPUT_BUTTON_CIRCLE))
        run_overrun_test(app);

    if (input_button_down(INPUT_BUTTON_TRIANGLE))
        run_underrun_test(app);

    if (input_button_down(INPUT_BUTTON_SQUARE))
        run_leak_test(app);
}

static void memory_test_draw(game_app_t *app)
{
    (void)app;
}

static const game_state_desc_t g_memory_test_state = {
    "memory_test",
    memory_test_enter,
    memory_test_exit,
    memory_test_update,
    memory_test_draw
};

const game_state_desc_t *memory_test_state_desc(void)
{
    return &g_memory_test_state;
}