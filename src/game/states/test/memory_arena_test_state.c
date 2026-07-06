#include "game/states/test/debug_menu_state.h"

#include "engine/debug/screen_console.h"
#include "engine/input/input.h"
#include "engine/logging/log.h"
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
    int summary_printed;
    int exit_armed;
} arena_test_state_data_t;

static arena_test_state_data_t *memory_arena_test_data(game_app_t *app)
{
    return GAME_APP_STATE_DATA_AS(app, arena_test_state_data_t);
}

static void memory_arena_test_print_header(void)
{
    screen_console_begin(
        "memory_arena_test",
        "CROSS=basic alloc   CIRCLE=mark/release\n"
        "TRIANGLE=reset      SQUARE=oom\n"
        "L1=alignment        START=summary/quit"
    );
    screen_console_printf("\n");
}

static void log_arena_stats(game_app_t *app, const char *label)
{
    arena_test_state_data_t *data = memory_arena_test_data(app);

    if (!data || !data->initialized) {
        LOGLN("[state:memory_arena_test] %s arena not initialized", label);
        screen_console_printf("%s arena not initialized\n", label);
        return;
    }

    LOGLN("[state:memory_arena_test] %s used=%u remaining=%u peak=%u capacity=%u",
          label,
          mem_arena_used(&data->arena),
          mem_arena_remaining(&data->arena),
          mem_arena_peak(&data->arena),
          mem_arena_capacity(&data->arena));

    screen_console_printf("%s used=%u remaining=%u peak=%u capacity=%u\n",
                          label,
                          mem_arena_used(&data->arena),
                          mem_arena_remaining(&data->arena),
                          mem_arena_peak(&data->arena),
                          mem_arena_capacity(&data->arena));
}

static void memory_arena_test_print_summary(arena_test_state_data_t *data)
{
    if (!data || data->summary_printed)
        return;

    screen_console_printf("\nSUMMARY:\n");
    screen_console_printf("basic_alloc=%u mark_release=%u reset=%u oom=%u alignment=%u\n",
                          data->basic_alloc_runs,
                          data->mark_release_runs,
                          data->reset_runs,
                          data->oom_runs,
                          data->alignment_runs);
    screen_console_printf("PRESS START TO EXIT\n");

    data->summary_printed = 1;
    data->exit_armed = 1;
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
    screen_console_printf("basic_alloc begin\n");
    log_arena_stats(app, "before basic_alloc");

    p1 = mem_arena_alloc(&data->arena, 32, 8);
    p2 = mem_arena_alloc(&data->arena, 64, 16);
    p3 = mem_arena_alloc(&data->arena, 24, 4);

    LOGLN("[state:memory_arena_test] basic_alloc ptrs p1=%p p2=%p p3=%p", p1, p2, p3);
    screen_console_printf("basic_alloc ptrs p1=%p p2=%p p3=%p\n", p1, p2, p3);

    if (!p1 || !p2 || !p3) {
        LOGLN("[state:memory_arena_test] basic_alloc FAILED");
        screen_console_printf("basic_alloc FAILED\n");
        return;
    }

    memset(p1, 0x11, 32);
    memset(p2, 0x22, 64);
    memset(p3, 0x33, 24);

    LOGLN("[state:memory_arena_test] basic_alloc OK");
    screen_console_printf("basic_alloc OK\n");
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
    screen_console_printf("mark_release begin\n");
    log_arena_stats(app, "before mark_release");

    used_before = mem_arena_used(&data->arena);
    mark = mem_arena_get_mark(&data->arena);

    p1 = mem_arena_alloc(&data->arena, 48, 16);
    p2 = mem_arena_alloc(&data->arena, 80, 16);

    LOGLN("[state:memory_arena_test] mark_release ptrs p1=%p p2=%p", p1, p2);
    screen_console_printf("mark_release ptrs p1=%p p2=%p\n", p1, p2);

    if (!p1 || !p2) {
        LOGLN("[state:memory_arena_test] mark_release FAILED during alloc");
        screen_console_printf("mark_release FAILED during alloc\n");
        return;
    }

    memset(p1, 0x44, 48);
    memset(p2, 0x55, 80);

    used_after_alloc = mem_arena_used(&data->arena);
    mem_arena_release(&data->arena, mark);
    used_after_release = mem_arena_used(&data->arena);

    LOGLN("[state:memory_arena_test] mark_release used_before=%u used_after_alloc=%u used_after_release=%u",
          used_before, used_after_alloc, used_after_release);
    screen_console_printf("mark_release used_before=%u used_after_alloc=%u used_after_release=%u\n",
                          used_before, used_after_alloc, used_after_release);

    if (used_after_release != used_before) {
        LOGLN("[state:memory_arena_test] mark_release FAILED: offset mismatch");
        screen_console_printf("mark_release FAILED: offset mismatch\n");
    } else {
        LOGLN("[state:memory_arena_test] mark_release OK");
        screen_console_printf("mark_release OK\n");
    }

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
    screen_console_printf("reset begin\n");
    log_arena_stats(app, "before reset");

    p1 = mem_arena_alloc(&data->arena, 96, 16);
    p2 = mem_arena_alloc(&data->arena, 128, 16);

    LOGLN("[state:memory_arena_test] reset ptrs p1=%p p2=%p", p1, p2);
    screen_console_printf("reset ptrs p1=%p p2=%p\n", p1, p2);

    if (!p1 || !p2) {
        LOGLN("[state:memory_arena_test] reset FAILED during alloc");
        screen_console_printf("reset FAILED during alloc\n");
        return;
    }

    memset(p1, 0x66, 96);
    memset(p2, 0x77, 128);

    log_arena_stats(app, "before reset call");
    mem_arena_reset(&data->arena);
    log_arena_stats(app, "after reset call");

    if (mem_arena_used(&data->arena) != 0) {
        LOGLN("[state:memory_arena_test] reset FAILED: used != 0");
        screen_console_printf("reset FAILED: used != 0\n");
    } else {
        LOGLN("[state:memory_arena_test] reset OK");
        screen_console_printf("reset OK\n");
    }
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
    screen_console_printf("oom begin\n");
    mem_arena_reset(&data->arena);
    log_arena_stats(app, "after reset for oom");

    p1 = mem_arena_alloc(&data->arena, 896, 16);
    p2 = mem_arena_alloc(&data->arena, 256, 16);

    LOGLN("[state:memory_arena_test] oom ptrs p1=%p p2=%p", p1, p2);
    screen_console_printf("oom ptrs p1=%p p2=%p\n", p1, p2);

    if (!p1 && !p2) {
        LOGLN("[state:memory_arena_test] oom unexpected: first alloc failed");
        screen_console_printf("oom unexpected: first alloc failed\n");
        return;
    }

    if (p1 && !p2) {
        LOGLN("[state:memory_arena_test] oom OK: second alloc failed as expected");
        screen_console_printf("oom OK: second alloc failed as expected\n");
    } else if (p1 && p2) {
        LOGLN("[state:memory_arena_test] oom WARNING: arena larger/effective usage smaller than expected");
        screen_console_printf("oom WARNING: arena larger/effective usage smaller than expected\n");
    } else {
        LOGLN("[state:memory_arena_test] oom FAILED");
        screen_console_printf("oom FAILED\n");
    }

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
    screen_console_printf("alignment begin\n");
    mem_arena_reset(&data->arena);

    p1 = mem_arena_alloc(&data->arena, 1, 8);
    p2 = mem_arena_alloc(&data->arena, 1, 16);
    p3 = mem_arena_alloc(&data->arena, 1, 32);

    a1 = (uintptr_t)p1;
    a2 = (uintptr_t)p2;
    a3 = (uintptr_t)p3;

    LOGLN("[state:memory_arena_test] alignment ptrs p1=%p p2=%p p3=%p", p1, p2, p3);
    screen_console_printf("alignment ptrs p1=%p p2=%p p3=%p\n", p1, p2, p3);

    if (!p1 || !p2 || !p3) {
        LOGLN("[state:memory_arena_test] alignment FAILED during alloc");
        screen_console_printf("alignment FAILED during alloc\n");
        return;
    }

    LOGLN("[state:memory_arena_test] alignment mods m8=%u m16=%u m32=%u",
          (u32)(a1 & 7u),
          (u32)(a2 & 15u),
          (u32)(a3 & 31u));
    screen_console_printf("alignment mods m8=%u m16=%u m32=%u\n",
                          (u32)(a1 & 7u),
                          (u32)(a2 & 15u),
                          (u32)(a3 & 31u));

    if (((a1 & 7u) == 0u) && ((a2 & 15u) == 0u) && ((a3 & 31u) == 0u)) {
        LOGLN("[state:memory_arena_test] alignment OK");
        screen_console_printf("alignment OK\n");
    } else {
        LOGLN("[state:memory_arena_test] alignment FAILED");
        screen_console_printf("alignment FAILED\n");
    }

    log_arena_stats(app, "after alignment");
}

static int memory_arena_test_enter(game_app_t *app, void *userdata)
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
    data->summary_printed = 0;
    data->exit_armed = 0;

    screen_console_enter();
    memory_arena_test_print_header();

    LOGLN("[state:memory_arena_test] enter");
    LOGLN("[state:memory_arena_test] CROSS=basic alloc, CIRCLE=mark/release, TRIANGLE=reset, SQUARE=oom, L1=alignment, START=quit");
    log_arena_stats(app, "after init");

    screen_console_printf("enter\n");
    screen_console_printf("CROSS=basic alloc, CIRCLE=mark/release, TRIANGLE=reset, SQUARE=oom, L1=alignment\n");

    return 0;
}

static void memory_arena_test_exit(game_app_t *app)
{
    arena_test_state_data_t *data = memory_arena_test_data(app);

    if (data && data->initialized) {
        LOGLN("[state:memory_arena_test] before destroy used=%u remaining=%u peak=%u capacity=%u",
              mem_arena_used(&data->arena),
              mem_arena_remaining(&data->arena),
              mem_arena_peak(&data->arena),
              mem_arena_capacity(&data->arena));
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
    screen_console_exit();
}

static void memory_arena_test_fixed_update(game_app_t *app, float dt)
{
    (void)app;
    (void)dt;
}

static void memory_arena_test_update(game_app_t *app, float dt)
{
    arena_test_state_data_t *data = memory_arena_test_data(app);

    (void)dt;

    if (!data || !data->initialized)
        return;

    if (input_button_pressed(INPUT_BUTTON_START)) {
        if (!data->exit_armed) {
            LOGLN("[state:memory_arena_test] START pressed, show summary");
            memory_arena_test_print_summary(data);
            input_consume();
            return;
        }

        LOGLN("[state:memory_arena_test] START pressed, return to menu");
        input_consume();
        game_app_request_state_change(debug_menu_state_desc(), NULL);
        return;
    }

    if (data->exit_armed)
        return;

    if (input_button_pressed(INPUT_BUTTON_CROSS))
        run_basic_alloc_test(app);

    if (input_button_pressed(INPUT_BUTTON_CIRCLE))
        run_mark_release_test(app);

    if (input_button_pressed(INPUT_BUTTON_TRIANGLE))
        run_reset_test(app);

    if (input_button_pressed(INPUT_BUTTON_SQUARE))
        run_oom_test(app);

    if (input_button_pressed(INPUT_BUTTON_L1))
        run_alignment_test(app);
}

static void memory_arena_test_draw(game_app_t *app, float alpha)
{
    (void)app;
    (void)alpha;
}

static const game_state_desc_t g_memory_arena_test_state = {
    "memory_arena_test",
    memory_arena_test_enter,
    memory_arena_test_exit,
    memory_arena_test_fixed_update,
    memory_arena_test_update,
    memory_arena_test_draw
};

const game_state_desc_t *memory_arena_test_state_desc(void)
{
    return &g_memory_arena_test_state;
}