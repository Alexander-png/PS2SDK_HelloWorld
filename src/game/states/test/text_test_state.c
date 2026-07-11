#include "game/states/test/text_test_state.h"
#include "game/states/test/debug_menu_state.h"

#include "engine/logging/log.h"
#include "engine/gfx/gfx2d.h"
#include "engine/input/input.h"
#include "engine/memory/memory_arena.h"
#include "engine/resources/resources.h"
#include "engine/streaming/texture_assets.h"
#include "engine/text/text.h"
#include "engine/text/text_bmfont.h"

#ifndef TEST_FONT_DESC_PATH
#define TEST_FONT_DESC_PATH "8bitoperator_32.fnt"
#endif

#ifndef TEST_FONT_ATLAS_PATH
#define TEST_FONT_ATLAS_PATH "8bitoperator_32.png"
#endif

#ifndef TEXT_TEST_GLYPH_CAPACITY
#define TEXT_TEST_GLYPH_CAPACITY 512
#endif

#ifndef TEXT_TEST_LINE_CAPACITY
#define TEXT_TEST_LINE_CAPACITY 32
#endif

#ifndef TEXT_TEST_REVEAL_SECONDS_PER_GLYPH
#define TEXT_TEST_REVEAL_SECONDS_PER_GLYPH 0.03f
#endif

typedef struct text_test_state_data {
    resource_handle_t font_desc_res;
    texture_handle_t font_atlas_tex;

    int tex_id;
    int font_built;

    int desc_failed_logged;
    int atlas_failed_logged;
    int font_ready_logged;
    int prewarmed;

    text_font_t font;

    text_layout_t layout;
    text_layout_glyph_t *glyphs;
    text_layout_line_t *lines;

    text_layout_params_t params;
    text_reveal_state_t reveal;

    int use_yellow;
    int font_build_failed;
} text_test_state_data_t;

static resource_handle_t text_test_invalid_resource(void)
{
    resource_handle_t h;
    h.index = 0xffffu;
    h.generation = 0;
    return h;
}

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

    data->font_desc_res = text_test_invalid_resource();
    data->font_atlas_tex = text_test_invalid_texture();

    data->tex_id = -1;
    data->font_built = 0;

    data->desc_failed_logged = 0;
    data->atlas_failed_logged = 0;
    data->font_ready_logged = 0;
    data->prewarmed = 0;

    data->use_yellow = 0;
    data->font_build_failed = 0;

    text_font_init(&data->font);
}

static int text_test_rebuild_layout(text_test_state_data_t *data)
{
    const char *text_utf8;

    if (!data)
        return -1;

    if (!data->font.glyphs || data->font.glyph_count == 0 || data->font.tex_id < 0)
        return -1;

    text_utf8 = "Hello, world!\nПривет, мир!\nBMFont loader test";
    

    text_style_init(&data->params.style);
    data->params.style.layer = 100;
    data->params.style.color = data->use_yellow ? text_color_yellow() : text_color_white();
    data->params.origin_x = 32;
    data->params.origin_y = 32;
    data->params.max_width = 0;

    text_layout_reset(&data->layout);
    text_reveal_state_reset(&data->reveal);

    if (text_layout_build_plain(&data->layout, &data->font, text_utf8, &data->params) != 0) {
        LOGLN("[state:text_test] text_layout_build_plain failed");
        return -1;
    }

    return 0;
}

static int text_test_try_build_font(game_app_t *app, text_test_state_data_t *data)
{
    resource_status_t desc_status;
    texture_status_t atlas_status;
    const void *desc_data;
    u32 desc_size;
    text_bmfont_load_desc_t load_desc;

    if (!app || !data)
        return 0;

    if (!resource_is_valid(data->font_desc_res))
        return 0;

    if (!texture_is_valid(data->font_atlas_tex))
        return 0;

    if (data->font_build_failed)
        return 0;

    desc_status = resource_status(data->font_desc_res);
    atlas_status = texture_status(data->font_atlas_tex);

    if (desc_status == RESOURCE_STATUS_FAILED) {
        if (!data->desc_failed_logged) {
            LOGLN("[state:text_test] font descriptor failed path=%s",
                  resource_path(data->font_desc_res));
            data->desc_failed_logged = 1;
            data->font_build_failed = 1;
        }
        return 0;
    }

    if (atlas_status == TEXTURE_STATUS_FAILED) {
        if (!data->atlas_failed_logged) {
            LOGLN("[state:text_test] font atlas failed path=%s",
                  texture_path(data->font_atlas_tex));
            data->atlas_failed_logged = 1;
            data->font_build_failed = 1;
        }
        return 0;
    }

    if (desc_status != RESOURCE_STATUS_READY)
        return 0;

    if (atlas_status != TEXTURE_STATUS_READY)
        return 0;

    data->tex_id = texture_tex_id(data->font_atlas_tex);
    if (data->tex_id < 0)
        return 0;

    if (!data->prewarmed) {
        int warm = texture_prewarm(data->font_atlas_tex);
        LOGLN("[state:text_test] prewarm tex_id=%d result=%d", data->tex_id, warm);
        data->prewarmed = 1;
    }

    if (data->font_built)
        return 1;

    desc_data = resource_data(data->font_desc_res);
    desc_size = resource_size(data->font_desc_res);

    if (!desc_data || desc_size == 0) {
        LOGLN("[state:text_test] descriptor ready but empty");
        data->font_build_failed = 1;
        return 0;
    }

    load_desc.fnt_text = (const char *)desc_data;
    load_desc.fnt_size = (unsigned int)desc_size;
    load_desc.debug_name = resource_path(data->font_desc_res);
    load_desc.tex_id = data->tex_id;

    if (text_bmfont_load_from_memory(game_app_state_arena(app), &data->font, &load_desc) != 0) {
        LOGLN("[state:text_test] text_bmfont_load_from_memory failed");
        data->font_build_failed = 1;
        return 0;
    }

    if (text_test_rebuild_layout(data) != 0) {
        LOGLN("[state:text_test] failed to build layout");
        data->font_build_failed = 1;
        return 0;
    }

    data->font_built = 1;
    return 1;
}

static int text_test_enter(game_app_t *app, void *userdata)
{
    text_test_state_data_t *data;
    resource_load_desc_t res_desc;

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

    res_desc.path = TEST_FONT_DESC_PATH;
    res_desc.type = RESOURCE_TYPE_RAW;
    res_desc.priority = STREAM_PRIORITY_NORMAL;
    res_desc.callback = NULL;
    res_desc.userdata = NULL;

    data->font_desc_res = resource_load_file(&res_desc);
    if (!resource_is_valid(data->font_desc_res)) {
        LOGLN("[state:text_test] failed to request font descriptor path=%s",
              TEST_FONT_DESC_PATH);
        return -1;
    }

    data->font_atlas_tex = texture_load_png(TEST_FONT_ATLAS_PATH, STREAM_PRIORITY_NORMAL);
    if (!texture_is_valid(data->font_atlas_tex)) {
        LOGLN("[state:text_test] failed to request font atlas path=%s",
              TEST_FONT_ATLAS_PATH);
        return -1;
    }

    LOGLN("[state:text_test] enter requested descriptor=%s atlas=%s",
          TEST_FONT_DESC_PATH,
          TEST_FONT_ATLAS_PATH);
    return 0;
}

static void text_test_exit(game_app_t *app)
{
    text_test_state_data_t *data = text_test_data(app);

    if (!data) {
        LOGLN("[state:text_test] exit");
        return;
    }

    if (resource_is_valid(data->font_desc_res))
        resource_release(data->font_desc_res);

    if (texture_is_valid(data->font_atlas_tex))
        texture_release(data->font_atlas_tex);

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

    if (input_button_pressed(INPUT_BUTTON_START)) {
        LOGLN("[state:text_test] START pressed, return to menu");
        input_consume();
        game_app_request_state_change(debug_menu_state_desc(), NULL);
        return;
    }

    if (!data)
        return;

    if (text_test_try_build_font(app, data)) {
        if (!data->font_ready_logged) {
            LOGLN("[state:text_test] font ready glyphs=%u kernings=%u lines=%u",
                  (unsigned int)data->font.glyph_count,
                  (unsigned int)data->font.kerning_count,
                  (unsigned int)data->layout.line_count);
            data->font_ready_logged = 1;
        }

        if (input_button_pressed(INPUT_BUTTON_CROSS)) {
            data->use_yellow = !data->use_yellow;
            if (text_test_rebuild_layout(data) != 0)
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
}

static void text_test_draw(game_app_t *app, float alpha)
{
    text_test_state_data_t *data = text_test_data(app);

    (void)alpha;

    if (!data)
        return;

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