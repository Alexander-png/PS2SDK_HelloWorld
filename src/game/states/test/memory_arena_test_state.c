#include "memory_arena_test_state.h"
#include "game_app.h"

#include "engine/logging/log.h"
#include "engine/input/input.h"
#include "engine/memory/memory_arena.h"

#include <string.h>
#include <stdint.h>

typedef struct arena_test_state_data {
    mem_arena_t arena;
    int initialized;
    unsigned int basic_alloc_runs;
    unsigned int mark_release_runs;
    unsigned int reset_runs;
    unsigned int oom_runs;
    unsigned int alignment_runs;
} arena_test_state_data_t;

static arena_test_state_data_t *memory_arena_test_data(game_app_t *app)
{
    return (arena_test_state_data_t *)game_app_state_userdata(app);
}

static void log_arena_stats(game_app_t *app, const char *label)
{
    arena_test_state_data_t *data = memory_arena_test_data(app);

    if (!data || !data->initialized) {
        LOGLN("[state:memory_arena_test] %s arena not initialized", label);
        return;
    }

    LOGLN("[state:memory_arena_test] %s used=%u remaining=%u peak=%u capacity=%u",
          label,
          mem_arena_used(&data->arena),
          mem_arena_remaining(&data->arena),
          mem_arena_peak(&data->arena),
          mem_arena_capacity(&data->arena));
}

static void run_basic_alloc_test(game_app_t *app)
{
    arena_test_state_data_t *data = memory_arena_test_data(app);
    void *p1;
    void *p2;
    void *p3;

    if (!data || !data->initialized)
        return;

    data->basic_alloc_runs++;

    LOGLN("[state:memory_arena_test] basic_alloc begin");
    log_arena_stats(app, "before basic_alloc");

    p1 = mem_arena_alloc(&data->arena, 32, 8);
    p2 = mem_arena_alloc(&data->arena, 64, 16);
    p3 = mem_arena_alloc(&data->arena, 24, 4);

    LOGLN("[state:memory_arena_test] basic_alloc ptrs p1=%p p2=%p p3=%p", p1, p2, p3);

    if (!p1 || !p2 || !p3) {
        LOGLN("[state:memory_arena_test] basic_alloc FAILED");
        return;
    }

    memset(p1, 0x11, 32);
    memset(p2, 0x22, 64);
    memset(p3, 0x33, 24);

    LOGLN("[state:memory_arena_test] basic_alloc OK");
    log_arena_stats(app, "after basic_alloc");
}

static void run_mark_release_test(game_app_t *app)
{
    arena_test_state_data_t *data = memory_arena_test_data(app);
    mem_arena_mark_t mark;
    void *p1;
    void *p2;
    u32 used_before;
    u32 used_after_alloc;
    u32 used_after_release;

    if (!data || !data->initialized)
        return;

    data->mark_release_runs++;

    LOGLN("[state:memory_arena_test] mark_release begin");
    log_arena_stats(app, "before mark_release");

    used_before = mem_arena_used(&data->arena);
    mark = mem_arena_get_mark(&data->arena);

    p1 = mem_arena_alloc(&data->arena, 48, 16);
    p2 = mem_arena_alloc(&data->arena, 80, 16);

    LOGLN("[state:memory_arena_test] mark_release ptrs p1=%p p2=%p", p1, p2);

    if (!p1 || !p2) {
        LOGLN("[state:memory_arena_test] mark_release FAILED during alloc");
        return;
    }

    memset(p1, 0x44, 48);
    memset(p2, 0x55, 80);

    used_after_alloc = mem_arena_used(&data->arena);
    mem_arena_release(&data->arena, mark);
    used_after_release = mem_arena_used(&data->arena);

    LOGLN("[state:memory_arena_test] mark_release used_before=%u used_after_alloc=%u used_after_release=%u",
          used_before, used_after_alloc, used_after_release);

    if (used_after_release != used_before)
        LOGLN("[state:memory_arena_test] mark_release FAILED: offset mismatch");
    else
        LOGLN("[state:memory_arena_test] mark_release OK");

    log_arena_stats(app, "after mark_release");
}

static void run_reset_test(game_app_t *app)
{
    arena_test_state_data_t *data = memory_arena_test_data(app);
    void *p1;
    void *p2;

    if (!data || !data->initialized)
        return;

    data->reset_runs++;

    LOGLN("[state:memory_arena_test] reset begin");
    log_arena_stats(app, "before reset");

    p1 = mem_arena_alloc(&data->arena, 96, 16);
    p2 = mem_arena_alloc(&data->arena, 128, 16);

    LOGLN("[state:memory_arena_test] reset ptrs p1=%p p2=%p", p1, p2);

    if (!p1 || !p2) {
        LOGLN("[state:memory_arena_test] reset FAILED during alloc");
        return;
    }

    memset(p1, 0x66, 96);
    memset(p2, 0x77, 128);

    log_arena_stats(app, "before reset call");
    mem_arena_reset(&data->arena);
    log_arena_stats(app, "after reset call");

    if (mem_arena_used(&data->arena) != 0)
        LOGLN("[state:memory_arena_test] reset FAILED: used != 0");
    else
        LOGLN("[state:memory_arena_test] reset OK");
}

static void run_oom_test(game_app_t *app)
{
    arena_test_state_data_t *data = memory_arena_test_data(app);
    void *p1;
    void *p2;

    if (!data || !data->initialized)
        return;

    data->oom_runs++;

    LOGLN("[state:memory_arena_test] oom begin");
    mem_arena_reset(&data->arena);
    log_arena_stats(app, "after reset for oom");

    p1 = mem_arena_alloc(&data->arena, 896, 16);
    p2 = mem_arena_alloc(&data->arena, 256, 16);

    LOGLN("[state:memory_arena_test] oom ptrs p1=%p p2=%p", p1, p2);

    if (!p1 && !p2) {
        LOGLN("[state:memory_arena_test] oom unexpected: first alloc failed");
        return;
    }

    if (p1 && !p2)
        LOGLN("[state:memory_arena_test] oom OK: second alloc failed as expected");
    else if (p1 && p2)
        LOGLN("[state:memory_arena_test] oom WARNING: arena larger/effective usage smaller than expected");
    else
        LOGLN("[state:memory_arena_test] oom FAILED");

    log_arena_stats(app, "after oom");
}

static void run_alignment_test(game_app_t *app)
{
    arena_test_state_data_t *data = memory_arena_test_data(app);
    void *p1;
    void *p2;
    void *p3;
    uintptr_t a1;
    uintptr_t a2;
    uintptr_t a3;

    if (!data || !data->initialized)
        return;

    data->alignment_runs++;

    LOGLN("[state:memory_arena_test] alignment begin");
    mem_arena_reset(&data->arena);

    p1 = mem_arena_alloc(&data->arena, 1, 8);
    p2 = mem_arena_alloc(&data->arena, 1, 16);
    p3 = mem_arena_alloc(&data->arena, 1, 32);

    a1 = (uintptr_t)p1;
    a2 = (uintptr_t)p2;
    a3 = (uintptr_t)p3;

    LOGLN("[state:memory_arena_test] alignment ptrs p1=%p p2=%p p3=%p", p1, p2, p3);

    if (!p1 || !p2 || !p3) {
        LOGLN("[state:memory_arena_test] alignment FAILED during alloc");
        return;
    }

    LOGLN("[state:memory_arena_test] alignment mods m8=%u m16=%u m32=%u",
          (u32)(a1 & 7u),
          (u32)(a2 & 15u),
          (u32)(a3 & 31u));

    if (((a1 & 7u) == 0u) && ((a2 & 15u) == 0u) && ((a3 & 31u) == 0u))
        LOGLN("[state:memory_arena_test] alignment OK");
    else
        LOGLN("[state:memory_arena_test] alignment FAILED");

    log_arena_stats(app, "after alignment");
}

int memory_arena_test_enter(game_app_t *app, void *userdata)
{
    arena_test_state_data_t *data;

    (void)userdata;

    data = (arena_test_state_data_t *)mem_arena_calloc(
        game_app_state_arena(app),
        1,
        sizeof(*data),
        16
    );
    if (!data) {
        LOGLN("[state:memory_arena_test] enter FAILED: no state arena memory");
        return -1;
    }

    game_app_set_state_userdata(app, data);

    if (mem_arena_init(&data->arena, 1024, MEMTAG_TEMP) != 0) {
        LOGLN("[state:memory_arena_test] enter FAILED: arena init failed");
        game_app_set_state_userdata(app, NULL);
        return -1;
    }

    data->initialized = 1;

    LOGLN("[state:memory_arena_test] enter");
    LOGLN("[state:memory_arena_test] CROSS=basic alloc, CIRCLE=mark/release, TRIANGLE=reset, SQUARE=oom, L1=alignment, START=quit");
    log_arena_stats(app, "after init");

    return 0;
}

void memory_arena_test_exit(game_app_t *app)
{
    arena_test_state_data_t *data = memory_arena_test_data(app);

    if (data && data->initialized) {
        log_arena_stats(app, "before destroy");
        mem_arena_destroy(&data->arena);
        data->initialized = 0;
    }

    if (data) {
        LOGLN("[state:memory_arena_test] summary basic_alloc=%u mark_release=%u reset=%u oom=%u alignment=%u",
              data->basic_alloc_runs,
              data->mark_release_runs,
              data->reset_runs,
              data->oom_runs,
              data->alignment_runs);
    }

    game_app_set_state_userdata(app, NULL);
    LOGLN("[state:memory_arena_test] exit");
}

void memory_arena_test_update(game_app_t *app, float dt)
{
    arena_test_state_data_t *data = memory_arena_test_data(app);

    (void)dt;

    if (!data || !data->initialized)
        return;

    if (input_button_down(INPUT_BUTTON_START)) {
        LOGLN("[state:memory_arena_test] START pressed, quit");
        game_app_request_quit();
        return;
    }

    if (input_button_down(INPUT_BUTTON_CROSS))
        run_basic_alloc_test(app);

    if (input_button_down(INPUT_BUTTON_CIRCLE))
        run_mark_release_test(app);

    if (input_button_down(INPUT_BUTTON_TRIANGLE))
        run_reset_test(app);

    if (input_button_down(INPUT_BUTTON_SQUARE))
        run_oom_test(app);

    if (input_button_down(INPUT_BUTTON_L1))
        run_alignment_test(app);
}

static void memory_arena_test_draw(game_app_t *app)
{
    (void)app;
}

static const game_state_desc_t g_memory_arena_test_state = {
    "memory_arena_test",
    memory_arena_test_enter,
    memory_arena_test_exit,
    memory_arena_test_update,
    memory_arena_test_draw
};

const game_state_desc_t *memory_arena_test_state_desc(void)
{
    return &g_memory_arena_test_state;
}