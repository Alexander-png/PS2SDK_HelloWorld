#include "game/states/test/debug_menu_state.h"

#include "engine/logging/log.h"
#include "engine/gfx/gfx2d.h"
#include "engine/input/input.h"
#include "engine/memory/memory_arena.h"
#include "engine/text/text.h"
#include "engine/text/text_block.h"
#include "engine/text/text_font_resource.h"

#include "game/states/test/audio_test_state.h"
#include "game/states/test/resource_test_state.h"
#include "game/states/test/sprite_test_state.h"
#include "game/states/test/memory_test_state.h"
#include "game/states/test/memory_arena_test_state.h"
#include "game/states/test/audio_mix_test_state.h"
#include "game/states/test/text_test_state.h"

#include <stdio.h>
#include <string.h>

#ifndef DEBUG_MENU_FONT_DESC_PATH
#define DEBUG_MENU_FONT_DESC_PATH "8bitoperator_32.fnt"
#endif

#ifndef DEBUG_MENU_FONT_ATLAS_PATH
#define DEBUG_MENU_FONT_ATLAS_PATH "8bitoperator_32.png"
#endif

#ifndef DEBUG_MENU_GLYPH_CAPACITY
#define DEBUG_MENU_GLYPH_CAPACITY 512
#endif

#ifndef DEBUG_MENU_LINE_CAPACITY
#define DEBUG_MENU_LINE_CAPACITY 32
#endif

#ifndef DEBUG_MENU_TEXT_BUF_SIZE
#define DEBUG_MENU_TEXT_BUF_SIZE 1024
#endif

#ifndef DEBUG_MENU_BOX_X
#define DEBUG_MENU_BOX_X 24
#endif

#ifndef DEBUG_MENU_BOX_Y
#define DEBUG_MENU_BOX_Y 24
#endif

#ifndef DEBUG_MENU_BOX_W
#define DEBUG_MENU_BOX_W 560
#endif

#ifndef DEBUG_MENU_BOX_H
#define DEBUG_MENU_BOX_H 400
#endif

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
    int dirty;
    int font_bound_logged;

    text_font_resource_t font_res;
    text_block_t block;

    text_layout_glyph_t *glyphs;
    text_layout_line_t *lines;

    char text_buf[DEBUG_MENU_TEXT_BUF_SIZE];
} debug_menu_state_data_t;

static debug_menu_state_data_t *debug_menu_data(game_app_t *app)
{
    return GAME_APP_STATE_DATA_AS(app, debug_menu_state_data_t);
}

static void debug_menu_apply_style(debug_menu_state_data_t *data)
{
    text_style_t style;

    if (!data)
        return;

    text_style_init(&style);
    style.layer = 100;
    style.color = text_color_white();

    text_block_set_style(&data->block, &style);
}

static void debug_menu_build_text(debug_menu_state_data_t *data)
{
    int i;
    int off = 0;

    if (!data)
        return;

    off += snprintf(data->text_buf + off,
                    sizeof(data->text_buf) - off,
                    "=== Debug Menu ===\n"
                    "\n"
                    "UP/DOWN : select\n"
                    "CROSS   : enter state\n"
                    "START   : quit app\n"
                    "\n");

    for (i = 0; i < DEBUG_MENU_COUNT; ++i) {
        const char *prefix = (data->selected == i) ? "> " : "  ";

        if (off < (int)sizeof(data->text_buf)) {
            off += snprintf(data->text_buf + off,
                            sizeof(data->text_buf) - off,
                            "%s%s\n",
                            prefix,
                            g_menu[i].label);
        }
    }

    data->text_buf[sizeof(data->text_buf) - 1] = '\0';
}

static int debug_menu_refresh(debug_menu_state_data_t *data)
{
    if (!data || !data->block.font)
        return -1;

    debug_menu_build_text(data);
    debug_menu_apply_style(data);

    text_block_set_box(&data->block,
                       DEBUG_MENU_BOX_X,
                       DEBUG_MENU_BOX_Y,
                       DEBUG_MENU_BOX_W,
                       DEBUG_MENU_BOX_H);

    text_block_set_text(&data->block, data->text_buf);

    if (text_block_refresh(&data->block) != 0) {
        LOGLN("[state:debug_menu] text_block_refresh failed");
        return -1;
    }

    data->dirty = 0;
    return 0;
}

static int debug_menu_enter(game_app_t *app, void *userdata)
{
    debug_menu_state_data_t *data;
    text_font_resource_desc_t font_desc;

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

    data->glyphs = (text_layout_glyph_t *)mem_arena_calloc(
        game_app_state_arena(app),
        DEBUG_MENU_GLYPH_CAPACITY,
        sizeof(text_layout_glyph_t),
        16
    );
    if (!data->glyphs) {
        LOGLN("[state:debug_menu] failed to allocate glyph buffer");
        return -1;
    }

    data->lines = (text_layout_line_t *)mem_arena_calloc(
        game_app_state_arena(app),
        DEBUG_MENU_LINE_CAPACITY,
        sizeof(text_layout_line_t),
        16
    );
    if (!data->lines) {
        LOGLN("[state:debug_menu] failed to allocate line buffer");
        return -1;
    }

    text_block_init(&data->block,
                    data->glyphs,
                    DEBUG_MENU_GLYPH_CAPACITY,
                    NULL,
                    0,
                    NULL,
                    0,
                    data->lines,
                    DEBUG_MENU_LINE_CAPACITY,
                    0.0f);

    data->selected = 0;
    data->dirty = 1;
    data->font_bound_logged = 0;

    text_block_set_align_h(&data->block, TEXT_ALIGN_LEFT);
    text_block_set_align_v(&data->block, TEXT_ALIGN_TOP);
    text_block_set_wrap_mode(&data->block, TEXT_WRAP_WORD);

    debug_menu_apply_style(data);

    font_desc.fnt_path = DEBUG_MENU_FONT_DESC_PATH;
    font_desc.atlas_path = DEBUG_MENU_FONT_ATLAS_PATH;

    if (text_font_resource_request(app, &data->font_res, &font_desc) != 0) {
        LOGLN("[state:debug_menu] failed to request font resource");
        return -1;
    }

    LOGLN("[state:debug_menu] enter");
    return 0;
}

static void debug_menu_exit(game_app_t *app)
{
    debug_menu_state_data_t *data = debug_menu_data(app);

    if (data)
        text_font_resource_shutdown(app, &data->font_res);

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

    (void)dt;

    if (!data)
        return;

    text_font_resource_update(app, &data->font_res);

    if (!data->block.font && text_font_resource_is_ready(&data->font_res)) {
        const text_font_t *font = text_font_resource_get_font(&data->font_res);
        text_block_set_font(&data->block, font);

        if (!data->font_bound_logged && font) {
            LOGLN("[state:debug_menu] font bound glyphs=%u kernings=%u",
                  (unsigned int)font->glyph_count,
                  (unsigned int)font->kerning_count);
            data->font_bound_logged = 1;
        }

        data->dirty = 1;
    }

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
        data->dirty = 1;
        input_consume();
    }

    if (input_button_pressed(INPUT_BUTTON_DOWN)) {
        data->selected++;
        if (data->selected >= DEBUG_MENU_COUNT)
            data->selected = 0;
        data->dirty = 1;
        input_consume();
    }

    if (data->block.font && data->dirty) {
        if (debug_menu_refresh(data) != 0)
            LOGLN("[state:debug_menu] refresh failed");
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

    (void)alpha;

    if (!data)
        return;

    gfx2d_draw();
    text_block_draw(&data->block);
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