#include "engine/input/input.h"
#include "engine/logging/log.h"
#include "engine/memory/memory.h"
#include "engine/memory/memory_arena.h"

#include "game/states/test/debug_menu_state.h"
#include "game/states/test/memory_test_state.h"
#include "game/debug/debug_overlay.h"

#include <stdarg.h>
#include <string.h>
#include <stdio.h>

#ifndef MEMORY_TEST_EVENT_LINES
#define MEMORY_TEST_EVENT_LINES 12
#endif

#ifndef MEMORY_TEST_EVENT_LINE_LEN
#define MEMORY_TEST_EVENT_LINE_LEN 96
#endif

typedef struct memory_test_state_data {
    float enter_time_sec;
    unsigned int alloc_free_runs;
    unsigned int overrun_runs;
    unsigned int underrun_runs;
    unsigned int leak_runs;
    int summary_printed;
    int exit_armed;

    int overlay_dirty;
    int shutting_down;
    int event_write_index;
    char event_lines[MEMORY_TEST_EVENT_LINES][MEMORY_TEST_EVENT_LINE_LEN];

    debug_overlay_t overlay;
} memory_test_state_data_t;

static memory_test_state_data_t *memory_test_data(game_app_t *app)
{
    return GAME_APP_STATE_DATA_AS(app, memory_test_state_data_t);
}

static void memory_test_push_event(memory_test_state_data_t *data,
                                   const char *fmt, ...)
{
    va_list ap;
    char *dst;

    if (!data || data->shutting_down)
        return;

    dst = data->event_lines[data->event_write_index];
    dst[0] = '\0';

    va_start(ap, fmt);
    vsnprintf(dst, MEMORY_TEST_EVENT_LINE_LEN, fmt, ap);
    va_end(ap);

    data->event_write_index++;
    if (data->event_write_index >= MEMORY_TEST_EVENT_LINES)
        data->event_write_index = 0;

    data->overlay_dirty = 1;
}

static void memory_test_print_summary(memory_test_state_data_t *data)
{
    if (!data || data->summary_printed)
        return;

    memory_test_push_event(data,
        "SUMMARY: time_ms=%d alloc_free=%u overrun=%u underrun=%u leak=%u",
        (int)(data->enter_time_sec * 1000.0f),
        data->alloc_free_runs,
        data->overrun_runs,
        data->underrun_runs,
        data->leak_runs);
    memory_test_push_event(data, "PRESS START TO EXIT");

    data->summary_printed = 1;
    data->exit_armed = 1;
}

static void memory_test_rebuild_overlay(memory_test_state_data_t *data)
{
    char buf[DEBUG_OVERLAY_TEXT_CAPACITY];
    int off = 0;
    int i;
    int start;

    if (!data || data->shutting_down)
        return;

    if (!data->exit_armed) {
        off += snprintf(buf + off, sizeof(buf) - off,
            "Memory Test\n"
            "CROSS: alloc/free\n"
            "CIRCLE: overrun\n"
            "TRIANGLE: underrun\n"
            "SQUARE: leak\n"
            "START: show summary\n"
            "\n"
            "time_ms=%d\n"
            "alloc_free=%u overrun=%u underrun=%u leak=%u\n",
            (int)(data->enter_time_sec * 1000.0f),
            data->alloc_free_runs,
            data->overrun_runs,
            data->underrun_runs,
            data->leak_runs);
    } else {
        off += snprintf(buf + off, sizeof(buf) - off,
            "Memory Test Summary\n"
            "START: return to menu\n"
            "\n"
            "time_ms=%d\n"
            "alloc_free=%u\n"
            "overrun=%u\n"
            "underrun=%u\n"
            "leak=%u\n"
            "\n"
            "PRESS START TO EXIT\n",
            (int)(data->enter_time_sec * 1000.0f),
            data->alloc_free_runs,
            data->overrun_runs,
            data->underrun_runs,
            data->leak_runs);
    }

    if (!data->exit_armed) {
        off += snprintf(buf + off, sizeof(buf) - off, "\nRecent events:\n");

        start = data->event_write_index;
        for (i = 0; i < MEMORY_TEST_EVENT_LINES; ++i) {
            int idx = (start + i) % MEMORY_TEST_EVENT_LINES;
            const char *line = data->event_lines[idx];

            if (!line[0])
                continue;

            if (off >= (int)sizeof(buf))
                break;

            off += snprintf(buf + off, sizeof(buf) - off, "%s\n", line);
        }
    }

    off += snprintf(buf + off, sizeof(buf) - off, "\nRecent events:\n");

    start = data->event_write_index;
    for (i = 0; i < MEMORY_TEST_EVENT_LINES; ++i) {
        int idx = (start + i) % MEMORY_TEST_EVENT_LINES;
        const char *line = data->event_lines[idx];

        if (!line[0])
            continue;

        if (off >= (int)sizeof(buf))
            break;

        off += snprintf(buf + off, sizeof(buf) - off, "%s\n", line);
    }

    buf[sizeof(buf) - 1] = '\0';
    debug_overlay_set_text(&data->overlay, buf);
}

static void run_alloc_free_test(game_app_t *app)
{
    memory_test_state_data_t *data = memory_test_data(app);
    void *p = mem_alloc(32, 16, MEMTAG_TEMP);

    if (data)
        data->alloc_free_runs++;

    if (!p) {
        LOGLNC(LOGCAT_MEMORY, "[state:memory_test] alloc_free: allocation failed");
        memory_test_push_event(data, "alloc_free: allocation failed");
        return;
    }

    memset(p, 0x11, 32);
    mem_free(p, MEMTAG_TEMP);

    LOGLNC(LOGCAT_MEMORY, "[state:memory_test] alloc_free: done, expected no memory errors");
    memory_test_push_event(data,
        "alloc_free: done, expected no memory errors");
}

static void run_overrun_test(game_app_t *app)
{
    memory_test_state_data_t *data = memory_test_data(app);
    unsigned char *p = (unsigned char *)mem_alloc(32, 16, MEMTAG_TEMP);

    if (data)
        data->overrun_runs++;

    if (!p) {
        LOGLNC(LOGCAT_MEMORY, "[state:memory_test] overrun: allocation failed");
        memory_test_push_event(data, "overrun: allocation failed");
        return;
    }

    memset(p, 0x22, 32);
    p[32] = 0x99;

    LOGLNC(LOGCAT_MEMORY, "[state:memory_test] overrun: wrote 1 byte past end, free expected to report overrun");
    memory_test_push_event(data,
        "overrun: wrote 1 byte past end, free expected to report overrun");

    mem_free(p, MEMTAG_TEMP);
}

static void run_underrun_test(game_app_t *app)
{
    memory_test_state_data_t *data = memory_test_data(app);
    unsigned char *p = (unsigned char *)mem_alloc(32, 16, MEMTAG_TEMP);

    if (data)
        data->underrun_runs++;

    if (!p) {
        LOGLNC(LOGCAT_MEMORY, "[state:memory_test] underrun: allocation failed");
        memory_test_push_event(data, "underrun: allocation failed");
        return;
    }

    memset(p, 0x33, 32);
    p[-1] = 0x77;

    LOGLNC(LOGCAT_MEMORY, "[state:memory_test] underrun: wrote 1 byte before start, free expected to report underrun");
    memory_test_push_event(data,
        "underrun: wrote 1 byte before start, free expected to report underrun");

    mem_free(p, MEMTAG_TEMP);
}

static void run_leak_test(game_app_t *app)
{
    memory_test_state_data_t *data = memory_test_data(app);
    void *p = mem_alloc(64, 16, MEMTAG_TEMP);

    if (data)
        data->leak_runs++;

    if (!p) {
        LOGLNC(LOGCAT_MEMORY, "[state:memory_test] leak: allocation failed");
        memory_test_push_event(data, "leak: allocation failed");
        return;
    }

    memset(p, 0x44, 64);

    LOGLNC(LOGCAT_MEMORY, "[state:memory_test] leak: intentionally leaked 64 bytes, exit should report leak");
    memory_test_push_event(data,
        "leak: intentionally leaked 64 bytes, exit should report leak");

    (void)p;
}

static int memory_test_enter(game_app_t *app, void *userdata)
{
    memory_test_state_data_t *data;
    debug_overlay_desc_t overlay_desc;

    (void)userdata;

    data = (memory_test_state_data_t *)mem_arena_calloc(
        game_app_state_arena(app),
        1,
        sizeof(*data),
        16
    );
    if (!data) {
        LOGLNC(LOGCAT_STATE, "[state:memory_test] enter failed: no state arena memory");
        return -1;
    }

    data->enter_time_sec = 0.0f;
    data->summary_printed = 0;
    data->exit_armed = 0;
    data->overlay_dirty = 1;
    data->shutting_down = 0;
    data->event_write_index = 0;
    memset(data->event_lines, 0, sizeof(data->event_lines));

    game_app_set_state_userdata(app, data);

    debug_overlay_desc_init(&overlay_desc);
    overlay_desc.x = 16;
    overlay_desc.y = 16;
    overlay_desc.w = 620;
    overlay_desc.h = 260;

    if (debug_overlay_init(app, &data->overlay, &overlay_desc) != 0) {
        LOGLNC(LOGCAT_STATE, "[state:memory_test] overlay init failed");
        return -1;
    }

    LOGLNC(LOGCAT_STATE, "[state:memory_test] enter");
    LOGLNC(LOGCAT_MEMORY, "[state:memory_test] CROSS=alloc/free, CIRCLE=overrun, TRIANGLE=underrun, SQUARE=leak, START=quit");

    memory_test_push_event(data, "enter");
    memory_test_push_event(data,
        "CROSS=alloc/free, CIRCLE=overrun, TRIANGLE=underrun, SQUARE=leak");
    memory_test_push_event(data, "START=summary/quit");

    memory_test_rebuild_overlay(data);
    data->overlay_dirty = 0;

    return 0;
}

static void memory_test_exit(game_app_t *app)
{
    memory_test_state_data_t *data = memory_test_data(app);

    if (data) {
        LOGLNC(LOGCAT_MEMORY, "[state:memory_test] summary time_ms=%d alloc_free=%u overrun=%u underrun=%u leak=%u",
              (int)(data->enter_time_sec * 1000.0f),
              data->alloc_free_runs,
              data->overrun_runs,
              data->underrun_runs,
              data->leak_runs);
    }

    LOGLNC(LOGCAT_STATE, "[state:memory_test] exit");

    if (!data)
        return;

    data->shutting_down = 1;
    debug_overlay_shutdown(app, &data->overlay);
    game_app_set_state_userdata(app, NULL);
}

static void memory_test_fixed_update(game_app_t *app, float dt)
{
    (void)app;
    (void)dt;
}

static void memory_test_update(game_app_t *app, float dt)
{
    memory_test_state_data_t *data = memory_test_data(app);

    if (!data)
        return;

    debug_overlay_update(app, &data->overlay, dt);

    data->enter_time_sec += dt;
    data->overlay_dirty = 1;

    if (input_button_pressed(INPUT_BUTTON_START)) {
        if (!data)
            return;

        if (!data->exit_armed) {
            LOGLNC(LOGCAT_STATE, "[state:memory_test] START pressed, show summary");
            memory_test_print_summary(data);
            input_consume();
            if (data->overlay_dirty) {
                memory_test_rebuild_overlay(data);
                data->overlay_dirty = 0;
            }
            return;
        }

        LOGLNC(LOGCAT_STATE, "[state:memory_test] START pressed, return to menu");
        input_consume();
        game_app_request_state_change(debug_menu_state_desc(), NULL);
        return;
    }

    if (data->exit_armed) {
        if (data->overlay_dirty) {
            memory_test_rebuild_overlay(data);
            data->overlay_dirty = 0;
        }
        return;
    }

    if (input_button_pressed(INPUT_BUTTON_CROSS))
        run_alloc_free_test(app);

    if (input_button_pressed(INPUT_BUTTON_CIRCLE))
        run_overrun_test(app);

    if (input_button_pressed(INPUT_BUTTON_TRIANGLE))
        run_underrun_test(app);

    if (input_button_pressed(INPUT_BUTTON_SQUARE))
        run_leak_test(app);

    if (data->overlay_dirty) {
        memory_test_rebuild_overlay(data);
        data->overlay_dirty = 0;
    }
}

static void memory_test_draw(game_app_t *app, float alpha)
{
    memory_test_state_data_t *data = memory_test_data(app);

    (void)app;
    (void)alpha;

    if (data)
        debug_overlay_draw(&data->overlay);
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