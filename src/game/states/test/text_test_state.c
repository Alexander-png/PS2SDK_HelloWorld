#include "game/states/test/text_test_state.h"
#include "game/states/test/debug_menu_state.h"

#include "engine/logging/log.h"
#include "engine/gfx/gfx2d.h"
#include "engine/input/input.h"
#include "engine/memory/memory_arena.h"
#include "engine/streaming/texture_assets.h"
#include "engine/text/text.h"

#ifndef TEST_FONT_ATLAS_PATH
#define TEST_FONT_ATLAS_PATH "font_test.png"
#endif

#ifndef TEXT_TEST_GLYPH_CAPACITY
#define TEXT_TEST_GLYPH_CAPACITY 256
#endif

#ifndef TEXT_TEST_LINE_CAPACITY
#define TEXT_TEST_LINE_CAPACITY 16
#endif

#ifndef TEXT_TEST_REVEAL_SECONDS_PER_GLYPH
#define TEXT_TEST_REVEAL_SECONDS_PER_GLYPH 0.03f
#endif

typedef struct text_test_state_data {
    texture_handle_t texture;
    int tex_id;
    int font_built;
    int atlas_failed_logged;
    int atlas_ready_logged;
    int prewarmed;

    text_font_t font;

    text_layout_t layout;
    text_layout_glyph_t *glyphs;
    text_layout_line_t *lines;

    text_layout_params_t params;
    text_reveal_state_t reveal;

    int use_yellow;
} text_test_state_data_t;

static texture_handle_t text_test_invalid_texture(void)
{
    texture_handle_t h;
    h.index = 0xffffu;
    h.generation = 0;
    return h;
}

static text_test_state_data_t *text_test_data(game_app_t *app)
{
    return GAME_APP_STATE_DATA_AS(app, text_test_state_data_t);
}

static void text_test_reset_runtime(text_test_state_data_t *data)
{
    if (!data)
        return;

    data->texture = text_test_invalid_texture();
    data->tex_id = -1;
    data->font_built = 0;
    data->atlas_failed_logged = 0;
    data->atlas_ready_logged = 0;
    data->prewarmed = 0;
    data->use_yellow = 0;

    text_font_init(&data->font);
}

static text_glyph_t *text_test_find_glyph_slot(text_font_t *font, text_codepoint_t cp)
{
    unsigned int i;

    if (!font || !font->glyphs)
        return NULL;

    for (i = 0; i < font->glyph_count; ++i) {
        if (font->glyphs[i].valid && font->glyphs[i].codepoint == cp)
            return &font->glyphs[i];
    }

    for (i = 0; i < font->glyph_count; ++i) {
        if (!font->glyphs[i].valid)
            return &font->glyphs[i];
    }

    return NULL;
}

static int text_test_set_glyph(text_font_t *font,
                               text_codepoint_t cp,
                               unsigned short atlas_x,
                               unsigned short atlas_y,
                               unsigned short atlas_w,
                               unsigned short atlas_h,
                               short xoffset,
                               short yoffset,
                               short xadvance)
{
    text_glyph_t *g = text_test_find_glyph_slot(font, cp);

    if (!g)
        return -1;

    g->codepoint = cp;
    g->atlas_x = atlas_x;
    g->atlas_y = atlas_y;
    g->atlas_w = atlas_w;
    g->atlas_h = atlas_h;
    g->xoffset = xoffset;
    g->yoffset = yoffset;
    g->xadvance = xadvance;
    g->page = 0;
    g->valid = 1;

    return 0;
}

static int text_test_build_font(game_app_t *app, text_test_state_data_t *data)
{
    text_font_t *font;

    if (!app || !data)
        return -1;

    font = &data->font;
    text_font_init(font);

    font->name = "text_test_font";
    font->tex_id = data->tex_id;
    font->atlas_width = 256;
    font->atlas_height = 256;
    font->line_height = 16;
    font->base = 12;
    font->fallback_codepoint = '?';

    font->glyph_count = 128;
    font->kerning_count = 0;

    font->glyphs = (text_glyph_t *)mem_arena_calloc(
        game_app_state_arena(app),
        font->glyph_count,
        sizeof(text_glyph_t),
        16
    );
    if (!font->glyphs) {
        LOGLN("[state:text_test] failed to allocate font glyph table");
        return -1;
    }

    font->kernings = NULL;

    /*
      TODO:
      Эти координаты временные и должны соответствовать реальному atlas.
      Сейчас это smoke-test scaffold.
    */

    if (text_test_set_glyph(font, 'H',  0,  0, 8, 16, 0, 0, 8) < 0) return -1;
    if (text_test_set_glyph(font, 'e',  8,  0, 8, 16, 0, 0, 8) < 0) return -1;
    if (text_test_set_glyph(font, 'l', 16,  0, 8, 16, 0, 0, 8) < 0) return -1;
    if (text_test_set_glyph(font, 'o', 24,  0, 8, 16, 0, 0, 8) < 0) return -1;
    if (text_test_set_glyph(font, ',', 32,  0, 8, 16, 0, 0, 8) < 0) return -1;
    if (text_test_set_glyph(font, ' ', 40,  0, 8, 16, 0, 0, 8) < 0) return -1;
    if (text_test_set_glyph(font, 'w', 48,  0, 8, 16, 0, 0, 8) < 0) return -1;
    if (text_test_set_glyph(font, 'r', 56,  0, 8, 16, 0, 0, 8) < 0) return -1;
    if (text_test_set_glyph(font, 'd', 64,  0, 8, 16, 0, 0, 8) < 0) return -1;
    if (text_test_set_glyph(font, '!', 72,  0, 8, 16, 0, 0, 8) < 0) return -1;
    if (text_test_set_glyph(font, '?', 80,  0, 8, 16, 0, 0, 8) < 0) return -1;
    if (text_test_set_glyph(font, 'm', 88,  0, 8, 16, 0, 0, 8) < 0) return -1;
    if (text_test_set_glyph(font, 'i', 96,  0, 8, 16, 0, 0, 8) < 0) return -1;

    if (text_test_set_glyph(font, 0x041F,  0, 16, 8, 16, 0, 0, 8) < 0) return -1; /* П */
    if (text_test_set_glyph(font, 0x0440,  8, 16, 8, 16, 0, 0, 8) < 0) return -1; /* р */
    if (text_test_set_glyph(font, 0x0438, 16, 16, 8, 16, 0, 0, 8) < 0) return -1; /* и */
    if (text_test_set_glyph(font, 0x0432, 24, 16, 8, 16, 0, 0, 8) < 0) return -1; /* в */
    if (text_test_set_glyph(font, 0x0435, 32, 16, 8, 16, 0, 0, 8) < 0) return -1; /* е */
    if (text_test_set_glyph(font, 0x0442, 40, 16, 8, 16, 0, 0, 8) < 0) return -1; /* т */
    if (text_test_set_glyph(font, 0x043C, 48, 16, 8, 16, 0, 0, 8) < 0) return -1; /* м */
    if (text_test_set_glyph(font, 0x0438, 56, 16, 8, 16, 0, 0, 8) < 0) return -1; /* и */
    if (text_test_set_glyph(font, 0x0440, 64, 16, 8, 16, 0, 0, 8) < 0) return -1; /* р */

    data->font_built = 1;
    return 0;
}

static int text_test_rebuild_layout(text_test_state_data_t *data)
{
    const char *text_utf8;

    if (!data || !data->font_built)
        return -1;

    text_utf8 = "Hello, world!\nПривет, мир!";

    text_style_init(&data->params.style);
    data->params.style.layer = 100;
    data->params.style.color = data->use_yellow ? text_color_yellow() : text_color_white();
    data->params.origin_x = 32;
    data->params.origin_y = 32;
    data->params.max_width = 0;

    text_layout_reset(&data->layout);
    text_reveal_state_reset(&data->reveal);

    if (!text_layout_build_plain(&data->layout, &data->font, text_utf8, &data->params)) {
        LOGLN("[state:text_test] text_layout_build_plain failed");
        return -1;
    }

    return 0;
}

static int text_test_enter(game_app_t *app, void *userdata)
{
    text_test_state_data_t *data;

    (void)userdata;

    data = (text_test_state_data_t *)mem_arena_calloc(
        game_app_state_arena(app),
        1,
        sizeof(*data),
        16
    );
    if (!data) {
        LOGLN("[state:text_test] enter failed: no state arena memory");
        return -1;
    }

    game_app_set_state_userdata(app, data);
    text_test_reset_runtime(data);

    data->glyphs = (text_layout_glyph_t *)mem_arena_calloc(
        game_app_state_arena(app),
        TEXT_TEST_GLYPH_CAPACITY,
        sizeof(text_layout_glyph_t),
        16
    );
    if (!data->glyphs) {
        LOGLN("[state:text_test] failed to allocate glyph layout buffer");
        return -1;
    }

    data->lines = (text_layout_line_t *)mem_arena_calloc(
        game_app_state_arena(app),
        TEXT_TEST_LINE_CAPACITY,
        sizeof(text_layout_line_t),
        16
    );
    if (!data->lines) {
        LOGLN("[state:text_test] failed to allocate line layout buffer");
        return -1;
    }

    text_layout_init(&data->layout,
                     data->glyphs,
                     TEXT_TEST_GLYPH_CAPACITY,
                     data->lines,
                     TEXT_TEST_LINE_CAPACITY);

    text_reveal_state_init(&data->reveal, TEXT_TEST_REVEAL_SECONDS_PER_GLYPH);

    data->texture = texture_load_png(TEST_FONT_ATLAS_PATH, STREAM_PRIORITY_NORMAL);
    if (!texture_is_valid(data->texture)) {
        LOGLN("[state:text_test] failed to request font atlas path=%s", TEST_FONT_ATLAS_PATH);
        return -1;
    }

    LOGLN("[state:text_test] enter requested atlas path=%s", TEST_FONT_ATLAS_PATH);
    return 0;
}

static void text_test_exit(game_app_t *app)
{
    text_test_state_data_t *data = text_test_data(app);

    if (!data) {
        LOGLN("[state:text_test] exit");
        return;
    }

    if (texture_is_valid(data->texture))
        texture_release(data->texture);

    game_app_set_state_userdata(app, NULL);

    LOGLN("[state:text_test] exit");
}

static void text_test_fixed_update(game_app_t *app, float dt)
{
    (void)app;
    (void)dt;
}

static void text_test_update(game_app_t *app, float dt)
{
    text_test_state_data_t *data = text_test_data(app);
    texture_status_t st;

    if (input_button_pressed(INPUT_BUTTON_START)) {
        LOGLN("[state:text_test] START pressed, return to menu");
        input_consume();
        game_app_request_state_change(debug_menu_state_desc(), NULL);
        return;
    }

    if (!data)
        return;

    if (!texture_is_valid(data->texture))
        return;

    st = texture_status(data->texture);

    if (st == TEXTURE_STATUS_FAILED) {
        if (!data->atlas_failed_logged) {
            LOGLN("[state:text_test] atlas failed path=%s", texture_path(data->texture));
            data->atlas_failed_logged = 1;
        }
        return;
    }

    if (st != TEXTURE_STATUS_READY)
        return;

    data->tex_id = texture_tex_id(data->texture);
    if (data->tex_id < 0)
        return;

    if (!data->prewarmed) {
        int warm = texture_prewarm(data->texture);
        LOGLN("[state:text_test] prewarm tex_id=%d result=%d", data->tex_id, warm);
        data->prewarmed = 1;
    }

    if (!data->font_built) {
        if (text_test_build_font(app, data) < 0) {
            LOGLN("[state:text_test] failed to build font");
            return;
        }

        if (text_test_rebuild_layout(data) < 0) {
            LOGLN("[state:text_test] failed to build layout");
            return;
        }
    }

    if (!data->atlas_ready_logged) {
        LOGLN("[state:text_test] atlas ready tex_id=%d glyphs=%u lines=%u",
              data->tex_id,
              (unsigned int)data->layout.glyph_count,
              (unsigned int)data->layout.line_count);
        data->atlas_ready_logged = 1;
    }

    if (input_button_pressed(INPUT_BUTTON_CROSS)) {
        data->use_yellow = !data->use_yellow;
        if (text_test_rebuild_layout(data) < 0)
            LOGLN("[state:text_test] rebuild layout failed after CROSS");
        input_consume();
    }

    if (input_button_pressed(INPUT_BUTTON_CIRCLE)) {
        text_reveal_state_reset(&data->reveal);
        input_consume();
    }

    if (input_button_pressed(INPUT_BUTTON_TRIANGLE)) {
        text_reveal_state_finish(&data->reveal, data->layout.glyph_count);
        input_consume();
    }

    text_reveal_state_update(&data->reveal, data->layout.glyph_count, dt);
}

static void text_test_draw(game_app_t *app, float alpha)
{
    text_test_state_data_t *data = text_test_data(app);

    (void)alpha;

    if (!data)
        return;

    /*
      В текущем loop обычные спрайты state-а рисуются через gfx2d_draw(),
      а текст затем кладётся поверх тем же кадром.
    */
    gfx2d_draw();

    if (data->font_built)
        text_draw_layout(&data->font, &data->layout, &data->reveal);
}

static const game_state_desc_t s_text_test_state = {
    "text_test",
    text_test_enter,
    text_test_exit,
    text_test_fixed_update,
    text_test_update,
    text_test_draw
};

const game_state_desc_t *text_test_state_desc(void)
{
    return &s_text_test_state;
}