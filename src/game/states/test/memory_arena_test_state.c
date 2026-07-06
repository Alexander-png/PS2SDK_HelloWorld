#include "game/states/test/debug_menu_state.h"

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
    return GAME_APP_STATE_DATA_AS(app, arena_test_state_data_t);
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

    