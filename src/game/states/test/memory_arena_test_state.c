#include "engine/gfx/gfx2d.h"
#include "engine/input/input.h"
#include "engine/logging/log.h"
#include "engine/memory/memory_arena.h"

#include "game/states/test/debug_menu_state.h"
#include "game/states/test/memory_arena_test_state.h"
#include "game/debug/debug_overlay.h"

#include <stdarg.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>

#ifndef MEMORY_ARENA_TEST_EVENT_LINES
#define MEMORY_ARENA_TEST_EVENT_LINES 10
#endif

#ifndef MEMORY_ARENA_TEST_EVENT_LINE_LEN
#define MEMORY_ARENA_TEST_EVENT_LINE_LEN 96
#endif

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

    int overlay_dirty;
    int shutting_down;
    int event_write_index;
    char event_lines[MEMORY_ARENA_TEST_EVENT_LINES][MEMORY_ARENA_TEST_EVENT_LINE_LEN];

    debug_overlay_t overlay;
} arena_test_state_data_t;

static arena_test_state_data_t *memory_arena_test_data(game_app_t *app)
{
    return GAME_APP_STATE_DATA_AS(app, arena_test_state_data_t);
}

static void memory_arena_test_push_event(arena_test_state_data_t *data,
                                         const char *fmt, ...)
{
    va_list ap;
    char *dst;

    if (!data || data->shutting_down)
        return;

    dst = data->event_lines[data->event_write_index];
    dst[0] = '\0';

    va_start(ap, fmt);
    vsnprintf(dst, MEMORY_ARENA_TEST_EVENT_LINE_LEN, fmt, ap);
    va_end(ap);

    data->event_write_index++;
    if (data->event_write_index >= MEMORY_ARENA_TEST_EVENT_LINES)
        data->event_write_index = 0;

    data->overlay_dirty = 1;
}

static void memory_arena_test_log_stats(game_app_t *app, const char *label)
{
    arena_test_state_data_t *data = memory_arena_test_data(app);

    if (!data || !data->initialized) {
        LOGLNC(LOGCAT_MEMORY, "[state:memory_arena_test] %s arena not initialized", label);
        memory_arena_test_push_event(data, "%s arena not initialized", label);
        return;
    }

    LOGLNC(LOGCAT_MEMORY, "[state:memory_arena_test] %s used=%u remaining=%u peak=%u capacity=%u",
          label,
          mem_arena_used(&data->arena),
          mem_arena_remaining(&data->arena),
          mem_arena_peak(&data->arena),
          mem_arena_capacity(&data->arena));

    memory_arena_test_push_event(data,
        "%s used=%u rem=%u peak=%u cap=%u",
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

    memory_arena_test_push_event(data,
        "SUMMARY basic=%u mark=%u reset=%u oom=%u align=%u",
        data->basic_alloc_runs,
        data->mark_release_runs,
        data->reset_runs,
        data->oom_runs,
        data->alignment_runs);
    memory_arena_test_push_event(data, "PRESS START TO EXIT");

    data->summary_printed = 1;
    data->exit_armed = 1;
    data->overlay_dirty = 1;
}

static void memory_arena_test_rebuild_overlay(arena_test_state_data_t *data)
{
    char buf[DEBUG_OVERLAY_TEXT_CAPACITY];
    int off = 0;
    int i;
    int start;

    if (!data || data->shutting_down)
        return;

    if (!data->exit_armed) {
        off += snprintf(buf + off, sizeof(buf) - off,
            "Memory Arena Test\n"
            "CROSS: basic alloc\n"
            "CIRCLE: mark/release\n"
            "TRIANGLE: reset\n"
            "SQUARE: oom\n"
            "L1: alignment\n"
            "START: show summary\n"
            "\n"
            "basic=%u mark=%u reset=%u oom=%u align=%u\n",
            data->basic_alloc_runs,
            data->mark_release_runs,
            data->reset_runs,
            data->oom_runs,
            data->alignment_runs);

        if (data->initialized) {
            off += snprintf(buf + off, sizeof(buf) - off,
                "used=%u rem=%u peak=%u cap=%u\n",
                mem_arena_used(&data->arena),
                mem_arena_remaining(&data->arena),
                mem_arena_peak(&data->arena),
                mem_arena_capacity(&data->arena));
        } else {
            off += snprintf(buf + off, sizeof(buf) - off,
                "arena: not initialized\n");
        }

        off += snprintf(buf + off, sizeof(buf) - off, "\nRecent events:\n");

        start = data->event_write_index;
        for (i = 0; i < MEMORY_ARENA_TEST_EVENT_LINES; ++i) {
            int idx = (start + i) % MEMORY_ARENA_TEST_EVENT_LINES;
            const char *line = data->event_lines[idx];

            if (!line[0])
                continue;

            if (off >= (int)sizeof(buf))
                break;

            off += snprintf(buf + off, sizeof(buf) - off, "%s\n", line);
        }
    } else {
        off += snprintf(buf + off, sizeof(buf) - off,
            "Memory Arena Summary\n"
            "START: return to menu\n"
            "\n"
            "basic_alloc=%u\n"
            "mark_release=%u\n"
            "reset=%u\n"
            "oom=%u\n"
            "alignment=%u\n",
            data->basic_alloc_runs,
            data->mark_release_runs,
            data->reset_runs,
            data->oom_runs,
            data->alignment_runs);

        if (data->initialized) {
            off += snprintf(buf + off, sizeof(buf) - off,
                "\n"
                "final used=%u\n"
                "final remaining=%u\n"
                "final peak=%u\n"
                "capacity=%u\n",
                mem_arena_used(&data->arena),
                mem_arena_remaining(&data->arena),
                mem_arena_peak(&data->arena),
                mem_arena_capacity(&data->arena));
        }

        off += snprintf(buf + off, sizeof(buf) - off,
            "\n"
            "PRESS START TO EXIT\n");
    }

    buf[sizeof(buf) - 1] = '\0';
    debug_overlay_set_text(&data->overlay, buf);
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

    LOGLNC(LOGCAT_MEMORY, "[state:memory_arena_test] basic_alloc begin");
    memory_arena_test_push_event(data, "basic_alloc begin");
    memory_arena_test_log_stats(app, "before basic_alloc");

    p1 = mem_arena_alloc(&data->arena, 32, 8);
    p2 = mem_arena_alloc(&data->arena, 64, 16);
    p3 = mem_arena_alloc(&data->arena, 24, 4);

    LOGLNC(LOGCAT_MEMORY, "[state:memory_arena_test] basic_alloc ptrs p1=%p p2=%p p3=%p", p1, p2, p3);
    memory_arena_test_push_event(data,
        "basic_alloc ptrs p1=%p p2=%p p3=%p", p1, p2, p3);

    if (!p1 || !p2 || !p3) {
        LOGLNC(LOGCAT_MEMORY, "[state:memory_arena_test] basic_alloc FAILED");
        memory_arena_test_push_event(data, "basic_alloc FAILED");
        return;
    }

    memset(p1, 0x11, 32);
    memset(p2, 0x22, 64);
    memset(p3, 0x33, 24);

    LOGLNC(LOGCAT_MEMORY, "[state:memory_arena_test] basic_alloc OK");
    memory_arena_test_push_event(data, "basic_alloc OK");
    memory_arena_test_log_stats(app, "after basic_alloc");
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

    LOGLNC(LOGCAT_MEMORY, "[state:memory_arena_test] mark_release begin");
    memory_arena_test_push_event(data, "mark_release begin");
    memory_arena_test_log_stats(app, "before mark_release");

    used_before = mem_arena_used(&data->arena);
    mark = mem_arena_get_mark(&data->arena);

    p1 = mem_arena_alloc(&data->arena, 48, 16);
    p2 = mem_arena_alloc(&data->arena, 80, 16);

    LOGLNC(LOGCAT_MEMORY, "[state:memory_arena_test] mark_release ptrs p1=%p p2=%p", p1, p2);
    memory_arena_test_push_event(data,
        "mark_release ptrs p1=%p p2=%p", p1, p2);

    if (!p1 || !p2) {
        LOGLNC(LOGCAT_MEMORY, "[state:memory_arena_test] mark_release FAILED during alloc");
        memory_arena_test_push_event(data,
            "mark_release FAILED during alloc");
        return;
    }

    memset(p1, 0x44, 48);
    memset(p2, 0x55, 80);

    used_after_alloc = mem_arena_used(&data->arena);
    mem_arena_release(&data->arena, mark);
    used_after_release = mem_arena_used(&data->arena);

    LOGLNC(LOGCAT_MEMORY, "[state:memory_arena_test] mark_release used_before=%u used_after_alloc=%u used_after_release=%u",
          used_before, used_after_alloc, used_after_release);
    memory_arena_test_push_event(data,
        "mark_release before=%u alloc=%u release=%u",
        used_before, used_after_alloc, used_after_release);

    if (used_after_release != used_before) {
        LOGLNC(LOGCAT_MEMORY, "[state:memory_arena_test] mark_release FAILED: offset mismatch");
        memory_arena_test_push_event(data,
            "mark_release FAILED: offset mismatch");
    } else {
        LOGLNC(LOGCAT_MEMORY, "[state:memory_arena_test] mark_release OK");
        memory_arena_test_push_event(data, "mark_release OK");
    }

    memory_arena_test_log_stats(app, "after mark_release");
}

static void run_reset_test(game_app_t *app)
{
    arena_test_state_data_t *data = memory_arena_test_data(app);
    void *p1;
    void *p2;

    if (!data || !data->initialized)
        return;

    data->reset_runs++;

    LOGLNC(LOGCAT_MEMORY, "[state:memory_arena_test] reset begin");
    memory_arena_test_push_event(data, "reset begin");
    memory_arena_test_log_stats(app, "before reset");

    p1 = mem_arena_alloc(&data->arena, 96, 16);
    p2 = mem_arena_alloc(&data->arena, 128, 16);

    LOGLNC(LOGCAT_MEMORY, "[state:memory_arena_test] reset ptrs p1=%p p2=%p", p1, p2);
    memory_arena_test_push_event(data,
        "reset ptrs p1=%p p2=%p", p1, p2);

    if (!p1 || !p2) {
        LOGLNC(LOGCAT_MEMORY, "[state:memory_arena_test] reset FAILED during alloc");
        memory_arena_test_push_event(data,
            "reset FAILED during alloc");
        return;
    }

    memset(p1, 0x66, 96);
    memset(p2, 0x77, 128);

    memory_arena_test_log_stats(app, "before reset call");
    mem_arena_reset(&data->arena);
    memory_arena_test_log_stats(app, "after reset call");

    if (mem_arena_used(&data->arena) != 0) {
        LOGLNC(LOGCAT_MEMORY, "[state:memory_arena_test] reset FAILED: used != 0");
        memory_arena_test_push_event(data,
            "reset FAILED: used != 0");
    } else {
        LOGLNC(LOGCAT_MEMORY, "[state:memory_arena_test] reset OK");
        memory_arena_test_push_event(data, "reset OK");
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

    LOGLNC(LOGCAT_MEMORY, "[state:memory_arena_test] oom begin");
    memory_arena_test_push_event(data, "oom begin");
    mem_arena_reset(&data->arena);
    memory_arena_test_log_stats(app, "after reset for oom");

    p1 = mem_arena_alloc(&data->arena, 896, 16);
    p2 = mem_arena_alloc(&data->arena, 256, 16);

    LOGLNC(LOGCAT_MEMORY, "[state:memory_arena_test] oom ptrs p1=%p p2=%p", p1, p2);
    memory_arena_test_push_event(data,
        "oom ptrs p1=%p p2=%p", p1, p2);

    if (!p1 && !p2) {
        LOGLNC(LOGCAT_MEMORY, "[state:memory_arena_test] oom unexpected: first alloc failed");
        memory_arena_test_push_event(data,
            "oom unexpected: first alloc failed");
        return;
    }

    if (p1 && !p2) {
        LOGLNC(LOGCAT_MEMORY, "[state:memory_arena_test] oom OK: second alloc failed as expected");
        memory_arena_test_push_event(data,
            "oom OK: second alloc failed as expected");
    } else if (p1 && p2) {
        LOGLNC(LOGCAT_MEMORY, "[state:memory_arena_test] oom WARNING: arena larger/effective usage smaller than expected");
        memory_arena_test_push_event(data,
            "oom WARNING: second alloc succeeded");
    } else {
        LOGLNC(LOGCAT_MEMORY, "[state:memory_arena_test] oom FAILED");
        memory_arena_test_push_event(data, "oom FAILED");
    }

    memory_arena_test_log_stats(app, "after oom");
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

    LOGLNC(LOGCAT_MEMORY, "[state:memory_arena_test] alignment begin");
    memory_arena_test_push_event(data, "alignment begin");
    mem_arena_reset(&data->arena);

    p1 = mem_arena_alloc(&data->arena, 1, 8);
    p2 = mem_arena_alloc(&data->arena, 1, 16);
    p3 = mem_arena_alloc(&data->arena, 1, 32);

    a1 = (uintptr_t)p1;
    a2 = (uintptr_t)p2;
    a3 = (uintptr_t)p3;

    LOGLNC(LOGCAT_MEMORY, "[state:memory_arena_test] alignment ptrs p1=%p p2=%p p3=%p", p1, p2, p3);
    memory_arena_test_push_event(data,
        "alignment ptrs p1=%p p2=%p p3=%p", p1, p2, p3);

    if (!p1 || !p2 || !p3) {
        LOGLNC(LOGCAT_MEMORY, "[state:memory_arena_test] alignment FAILED during alloc");
        memory_arena_test_push_event(data,
            "alignment FAILED during alloc");
        return;
    }

    LOGLNC(LOGCAT_MEMORY, "[state:memory_arena_test] alignment mods m8=%u m16=%u m32=%u",
          (u32)(a1 & 7u),
          (u32)(a2 & 15u),
          (u32)(a3 & 31u));
    memory_arena_test_push_event(data,
        "alignment mods m8=%u m16=%u m32=%u",
        (u32)(a1 & 7u),
        (u32)(a2 & 15u),
        (u32)(a3 & 31u));

    if (((a1 & 7u) == 0u) && ((a2 & 15u) == 0u) && ((a3 & 31u) == 0u)) {
        LOGLNC(LOGCAT_MEMORY, "[state:memory_arena_test] alignment OK");
        memory_arena_test_push_event(data, "alignment OK");
    } else {
        LOGLNC(LOGCAT_MEMORY, "[state:memory_arena_test] alignment FAILED");
        memory_arena_test_push_event(data, "alignment FAILED");
    }

    memory_arena_test_log_stats(app, "after alignment");
}

static int memory_arena_test_enter(game_app_t *app, void *userdata)
{
    arena_test_state_data_t *data;
    debug_overlay_desc_t overlay_desc;

    (void)userdata;

    data = (arena_test_state_data_t *)mem_arena_calloc(
        game_app_state_arena(app),
        1,
        sizeof(*data),
        16
    );
    if (!data) {
        LOGLNC(LOGCAT_STATE, "[state:memory_arena_test] enter FAILED: no state arena memory");
        return -1;
    }

    game_app_set_state_userdata(app, data);

    if (mem_arena_init(&data->arena, 1024, MEMTAG_TEMP) != 0) {
        LOGLNC(LOGCAT_MEMORY, "[state:memory_arena_test] enter FAILED: arena init failed");
        game_app_set_state_userdata(app, NULL);
        return -1;
    }

    data->initialized = 1;
    data->summary_printed = 0;
    data->exit_armed = 0;
    data->overlay_dirty = 1;
    data->shutting_down = 0;
    data->event_write_index = 0;
    memset(data->event_lines, 0, sizeof(data->event_lines));

    debug_overlay_desc_init(&overlay_desc);
    overlay_desc.x = 16;
    overlay_desc.y = 16;
    overlay_desc.w = 620;
    overlay_desc.h = 260;

    if (debug_overlay_init(app, &data->overlay, &overlay_desc) != 0) {
        LOGLNC(LOGCAT_STATE, "[state:memory_arena_test] overlay init failed");
        mem_arena_destroy(&data->arena);
        data->initialized = 0;
        game_app_set_state_userdata(app, NULL);
        return -1;
    }

    LOGLNC(LOGCAT_STATE, "[state:memory_arena_test] enter");
    LOGLNC(LOGCAT_MEMORY, "[state:memory_arena_test] CROSS=basic alloc, CIRCLE=mark/release, TRIANGLE=reset, SQUARE=oom, L1=alignment, START=quit");

    memory_arena_test_push_event(data, "enter");
    memory_arena_test_push_event(data,
        "CROSS=basic alloc CIRCLE=mark/release");
    memory_arena_test_push_event(data,
        "TRIANGLE=reset SQUARE=oom L1=alignment");
    memory_arena_test_log_stats(app, "after init");

    memory_arena_test_rebuild_overlay(data);
    data->overlay_dirty = 0;

    return 0;
}

static void memory_arena_test_exit(game_app_t *app)
{
    arena_test_state_data_t *data = memory_arena_test_data(app);

    if (data && data->initialized) {
        LOGLNC(LOGCAT_MEMORY, "[state:memory_arena_test] before destroy used=%u remaining=%u peak=%u capacity=%u",
              mem_arena_used(&data->arena),
              mem_arena_remaining(&data->arena),
              mem_arena_peak(&data->arena),
              mem_arena_capacity(&data->arena));
    }

    if (data) {
        LOGLNC(LOGCAT_MEMORY, "[state:memory_arena_test] summary basic_alloc=%u mark_release=%u reset=%u oom=%u alignment=%u",
              data->basic_alloc_runs,
              data->mark_release_runs,
              data->reset_runs,
              data->oom_runs,
              data->alignment_runs);
    }

    LOGLNC(LOGCAT_STATE, "[state:memory_arena_test] exit");

    if (!data)
        return;

    data->shutting_down = 1;
    debug_overlay_shutdown(app, &data->overlay);

    if (data->initialized) {
        mem_arena_destroy(&data->arena);
        data->initialized = 0;
    }

    game_app_set_state_userdata(app, NULL);
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

    debug_overlay_update(app, &data->overlay, dt);

    if (input_button_pressed(INPUT_BUTTON_START)) {
        if (!data->exit_armed) {
            LOGLNC(LOGCAT_STATE, "[state:memory_arena_test] START pressed, show summary");
            memory_arena_test_print_summary(data);
            input_consume();

            if (data->overlay_dirty) {
                memory_arena_test_rebuild_overlay(data);
                data->overlay_dirty = 0;
            }
            return;
        }

        LOGLNC(LOGCAT_STATE, "[state:memory_arena_test] START pressed, return to menu");
        input_consume();
        game_app_request_state_change(debug_menu_state_desc(), NULL);
        return;
    }

    if (data->exit_armed) {
        if (data->overlay_dirty) {
            memory_arena_test_rebuild_overlay(data);
            data->overlay_dirty = 0;
        }
        return;
    }

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

    if (data->overlay_dirty) {
        memory_arena_test_rebuild_overlay(data);
        data->overlay_dirty = 0;
    }
}

static void memory_arena_test_draw(game_app_t *app, float alpha)
{
    arena_test_state_data_t *data = memory_arena_test_data(app);

    (void)app;
    (void)alpha;

    gfx2d_draw();

    if (data)
        debug_overlay_draw(&data->overlay);
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