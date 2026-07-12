#include "game/states/test/text_test_state.h"
#include "game/states/test/debug_menu_state.h"

#include "engine/logging/log.h"
#include "engine/gfx/gfx2d.h"
#include "engine/input/input.h"
#include "engine/memory/memory_arena.h"
#include "engine/text/text.h"
#include "engine/text/text_block.h"
#include "engine/text/text_font_resource.h"

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

#ifndef TEXT_TEST_BOX_X
#define TEXT_TEST_BOX_X 32
#endif

#ifndef TEXT_TEST_BOX_Y
#define TEXT_TEST_BOX_Y 32
#endif

#ifndef TEXT_TEST_BOX_W
#define TEXT_TEST_BOX_W 400
#endif

#ifndef TEXT_TEST_BOX_H
#define TEXT_TEST_BOX_H 400
#endif

typedef struct text_test_state_data {
    text_font_resource_t font_res;
    text_block_t block;

    text_layout_glyph_t *glyphs;
    text_layout_item_t *rich_items;
    text_layout_line_t *lines;

    int use_yellow;
    int font_bound_logged;
    int test_case_index;
} text_test_state_data_t;

typedef enum text_test_case_kind {
    TEXT_TEST_CASE_PLAIN = 0,
    TEXT_TEST_CASE_RICH  = 1
} text_test_case_kind_t;

typedef struct text_test_case {
    const char *name;
    text_test_case_kind_t kind;
    text_wrap_mode_t wrap_mode;
    text_reveal_mode_t reveal_mode;
    const char *text_utf8;
} text_test_case_t;

static text_test_state_data_t *text_test_data(game_app_t *app)
{
    return GAME_APP_STATE_DATA_AS(app, text_test_state_data_t);
}

static const char s_demo_word[] =
    "Hello, world! This is a longer bitmap text block used to test word wrapping, "
    "horizontal alignment, and vertical alignment inside a fixed box.\n"
    "Привет, мир! Это более длинный текстовый блок для проверки переноса слов, "
    "выравнивания по горизонтали и выравнивания по вертикали внутри заданного прямоугольника.";

static const char s_long_mixed_char[] =
    "SupercalifragilisticexpialidociousLongWordWrapTest1234567890 "
    "ПриветМирОченьДлинноеСловоБезПробеловДляПроверкиПереноса";

static const char s_word_simple[] =
    "Hello world this should wrap by words naturally";

static const char s_char_cyrillic_long[] =
    "ОченьДлинноеСловоБезПробеловДляПроверкиПереноса";

static const char s_word_cyrillic_mixed[] =
    "Привет мир оченьдлинноесловобезпробелов";

static const char s_rich_color_rgb[] =
    "Hello [color=#FFFF00]yellow[/color] world";

static const char s_rich_color_rgba[] =
    "Hello [color=#FFFF0080]yellow[/color] world";

static const char s_rich_shake_basic[] =
    "Normal [shake]danger[/shake] zone";

static const char s_rich_color_and_shake[] =
    "[color=#00FF00]Green[/color] words and [shake]shaky[/shake] words";

static const char s_rich_nested_shake_in_color[] =
    "[color=#00FF00]Green [shake]shaky[/shake] text[/color] end";

static const char s_rich_adjacent_tags[] =
    "[color=#FF0000]Red[/color][color=#00FF00]Green[/color][color=#0000FF]Blue[/color]";

static const char s_rich_unclosed_color[] =
    "Hello [color=#FFFF00]yellow world";

static const char s_rich_reveal_words_basic[] =
    "[color=#FFFF00]One[/color] two [shake]three[/shake] four";

static const text_test_case_t s_text_test_cases[] = {
    { "demo_word",            TEXT_TEST_CASE_PLAIN, TEXT_WRAP_WORD, TEXT_REVEAL_GLYPH, s_demo_word },
    { "long_mixed_char",      TEXT_TEST_CASE_PLAIN, TEXT_WRAP_CHAR, TEXT_REVEAL_GLYPH, s_long_mixed_char },
    { "word_simple",          TEXT_TEST_CASE_PLAIN, TEXT_WRAP_WORD, TEXT_REVEAL_WORD,  s_word_simple },
    { "char_cyrillic_long",   TEXT_TEST_CASE_PLAIN, TEXT_WRAP_CHAR, TEXT_REVEAL_GLYPH, s_char_cyrillic_long },
    { "word_cyrillic_mixed",  TEXT_TEST_CASE_PLAIN, TEXT_WRAP_WORD, TEXT_REVEAL_WORD,  s_word_cyrillic_mixed },

    { "rich_color_rgb",        TEXT_TEST_CASE_RICH,  TEXT_WRAP_WORD, TEXT_REVEAL_GLYPH, s_rich_color_rgb },
    { "rich_color_rgba",       TEXT_TEST_CASE_RICH,  TEXT_WRAP_WORD, TEXT_REVEAL_GLYPH, s_rich_color_rgba },
    { "rich_shake_basic",      TEXT_TEST_CASE_RICH,  TEXT_WRAP_WORD, TEXT_REVEAL_GLYPH, s_rich_shake_basic },
    { "rich_color_shake",      TEXT_TEST_CASE_RICH,  TEXT_WRAP_WORD, TEXT_REVEAL_GLYPH,  s_rich_color_and_shake },
    { "rich_shake_in_color",   TEXT_TEST_CASE_RICH,  TEXT_WRAP_WORD, TEXT_REVEAL_WORD,  s_rich_nested_shake_in_color },
    { "rich_adjacent_tags",    TEXT_TEST_CASE_RICH,  TEXT_WRAP_WORD, TEXT_REVEAL_GLYPH,  s_rich_adjacent_tags },
    { "rich_unclosed_tag",     TEXT_TEST_CASE_RICH,  TEXT_WRAP_WORD, TEXT_REVEAL_WORD,  s_rich_unclosed_color },
    { "rich_reveal_words",     TEXT_TEST_CASE_RICH,  TEXT_WRAP_WORD, TEXT_REVEAL_WORD,  s_rich_reveal_words_basic },
};

static int text_test_case_count(void)
{
    return (int)(sizeof(s_text_test_cases) / sizeof(s_text_test_cases[0]));
}

static const char *text_test_case_kind_name(text_test_case_kind_t kind)
{
    switch (kind) {
        case TEXT_TEST_CASE_PLAIN: return "plain";
        case TEXT_TEST_CASE_RICH:  return "rich";
        default:                   return "unknown";
    }
}

static const char *text_test_reveal_mode_name(text_reveal_mode_t mode)
{
    switch (mode) {
        case TEXT_REVEAL_GLYPH: return "glyph";
        case TEXT_REVEAL_WORD:  return "word";
        default:                return "unknown";
    }
}

static const char *text_test_align_h_name(text_align_h_t align_h)
{
    switch (align_h) {
        case TEXT_ALIGN_LEFT:   return "left";
        case TEXT_ALIGN_CENTER: return "center";
        case TEXT_ALIGN_RIGHT:  return "right";
        default:                return "unknown";
    }
}

static const char *text_test_align_v_name(text_align_v_t align_v)
{
    switch (align_v) {
        case TEXT_ALIGN_TOP:    return "top";
        case TEXT_ALIGN_MIDDLE: return "middle";
        case TEXT_ALIGN_BOTTOM: return "bottom";
        default:                return "unknown";
    }
}

static void text_test_apply_style(text_test_state_data_t *data)
{
    text_style_t style;

    if (!data)
        return;

    text_style_init(&style);
    style.layer = 100;
    style.color = data->use_yellow ? text_color_yellow() : text_color_white();

    text_block_set_style(&data->block, &style);
}

static const text_test_case_t *text_test_current_case(const text_test_state_data_t *data)
{
    int count;

    if (!data)
        return NULL;

    count = text_test_case_count();
    if (count <= 0)
        return NULL;

    if (data->test_case_index < 0 || data->test_case_index >= count)
        return &s_text_test_cases[0];

    return &s_text_test_cases[data->test_case_index];
}

static void text_test_apply_case(text_test_state_data_t *data)
{
    const text_test_case_t *tc;

    if (!data)
        return;

    tc = text_test_current_case(data);
    if (!tc)
        return;

    text_block_set_wrap_mode(&data->block, tc->wrap_mode);
    text_block_set_reveal_mode(&data->block, tc->reveal_mode);

    if (tc->kind == TEXT_TEST_CASE_RICH)
        text_block_set_rich_text(&data->block, tc->text_utf8);
    else
        text_block_set_text(&data->block, tc->text_utf8);
}

static int text_test_refresh_block(text_test_state_data_t *data)
{
    const text_test_case_t *tc;

    if (!data)
        return -1;

    tc = text_test_current_case(data);
    if (!tc)
        return -1;

    text_block_set_box(&data->block,
                       TEXT_TEST_BOX_X,
                       TEXT_TEST_BOX_Y,
                       TEXT_TEST_BOX_W,
                       TEXT_TEST_BOX_H);

    text_test_apply_case(data);
    text_test_apply_style(data);

    if (text_block_refresh(&data->block) != 0) {
        LOGLN("[state:text_test] text_block_refresh failed case=%s kind=%s",
              tc->name,
              text_test_case_kind_name(tc->kind));
        return -1;
    }

    LOGLN("[state:text_test] block refreshed case=%d/%d name=%s kind=%s wrap=%d reveal=%s w=%d h=%d align_h=%s align_v=%s",
          (int)(data->test_case_index + 1),
          (int)text_test_case_count(),
          tc->name,
          text_test_case_kind_name(tc->kind),
          (int)tc->wrap_mode,
          text_test_reveal_mode_name(tc->reveal_mode),
          (int)text_block_width(&data->block),
          (int)text_block_height(&data->block),
          text_test_align_h_name(data->block.align_h),
          text_test_align_v_name(data->block.align_v));

    return 0;
}

static int text_test_cycle_case(text_test_state_data_t *data, int delta)
{
    int count;
    int next;

    if (!data)
        return -1;

    count = text_test_case_count();
    if (count <= 0)
        return -1;

    next = data->test_case_index + delta;

    while (next < 0)
        next += count;
    while (next >= count)
        next -= count;

    data->test_case_index = next;
    return text_test_refresh_block(data);
}

static int text_test_enter(game_app_t *app, void *userdata)
{
    text_test_state_data_t *data;
    text_font_resource_desc_t font_desc;

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

    data->rich_items = (text_layout_item_t *)mem_arena_calloc(
        game_app_state_arena(app),
        TEXT_TEST_GLYPH_CAPACITY,
        sizeof(text_layout_item_t),
        16
    );
    if (!data->rich_items) {
        LOGLN("[state:text_test] failed to allocate rich layout item buffer");
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

    text_block_init(&data->block,
        data->glyphs,
        TEXT_TEST_GLYPH_CAPACITY,
        data->rich_items,
        TEXT_TEST_GLYPH_CAPACITY,
        data->lines,
        TEXT_TEST_LINE_CAPACITY,
        TEXT_TEST_REVEAL_SECONDS_PER_GLYPH);

    data->use_yellow = 0;
    data->font_bound_logged = 0;
    data->test_case_index = 0;

    text_block_set_box(&data->block,
        TEXT_TEST_BOX_X,
        TEXT_TEST_BOX_Y,
        TEXT_TEST_BOX_W,
        TEXT_TEST_BOX_H);

    text_block_set_align_h(&data->block, TEXT_ALIGN_LEFT);
    text_block_set_align_v(&data->block, TEXT_ALIGN_TOP);

    text_test_apply_case(data);
    text_test_apply_style(data);

    font_desc.fnt_path = TEST_FONT_DESC_PATH;
    font_desc.atlas_path = TEST_FONT_ATLAS_PATH;

    if (text_font_resource_request(app, &data->font_res, &font_desc) != 0) {
        LOGLN("[state:text_test] failed to request font resource");
        return -1;
    }

    LOGLN("[state:text_test] enter requested descriptor=%s atlas=%s box=(%d,%d,%d,%d)",
          TEST_FONT_DESC_PATH,
          TEST_FONT_ATLAS_PATH,
          (int)TEXT_TEST_BOX_X,
          (int)TEXT_TEST_BOX_Y,
          (int)TEXT_TEST_BOX_W,
          (int)TEXT_TEST_BOX_H);

    LOGLN("[state:text_test] controls: CROSS=color CIRCLE=reveal_reset TRIANGLE=reveal_finish SQUARE=align_h L1=align_v R1/R2=case");
    return 0;
}

static void text_test_exit(game_app_t *app)
{
    text_test_state_data_t *data = text_test_data(app);

    if (!data) {
        LOGLN("[state:text_test] exit");
        return;
    }

    text_font_resource_shutdown(app, &data->font_res);
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

    text_font_resource_update(app, &data->font_res);

    if (!data->block.font &&
        text_font_resource_is_ready(&data->font_res)) {
        const text_font_t *font = text_font_resource_get_font(&data->font_res);

        text_block_set_font(&data->block, font);

        if (text_test_refresh_block(data) != 0) {
            LOGLN("[state:text_test] initial refresh failed");
            return;
        }

        if (!data->font_bound_logged) {
            LOGLN("[state:text_test] font bound glyphs=%u kernings=%u w=%d h=%d align_h=%s align_v=%s",
                (unsigned int)font->glyph_count,
                (unsigned int)font->kerning_count,
                (int)text_block_width(&data->block),
                (int)text_block_height(&data->block),
                text_test_align_h_name(data->block.align_h),
                text_test_align_v_name(data->block.align_v));
            data->font_bound_logged = 1;
        }
    }

    if (data->block.font) {
        if (input_button_pressed(INPUT_BUTTON_CROSS)) {
            data->use_yellow = !data->use_yellow;

            if (text_test_refresh_block(data) != 0)
                LOGLN("[state:text_test] refresh failed after CROSS");

            input_consume();
        }

        if (input_button_pressed(INPUT_BUTTON_CIRCLE)) {
            text_block_reveal_reset(&data->block);
            input_consume();
        }

        if (input_button_pressed(INPUT_BUTTON_TRIANGLE)) {
            text_block_reveal_finish(&data->block);
            input_consume();
        }

        if (input_button_pressed(INPUT_BUTTON_SQUARE)) {
            if (data->block.align_h == TEXT_ALIGN_LEFT)
                text_block_set_align_h(&data->block, TEXT_ALIGN_CENTER);
            else if (data->block.align_h == TEXT_ALIGN_CENTER)
                text_block_set_align_h(&data->block, TEXT_ALIGN_RIGHT);
            else
                text_block_set_align_h(&data->block, TEXT_ALIGN_LEFT);

            if (text_test_refresh_block(data) != 0)
                LOGLN("[state:text_test] refresh failed after SQUARE");
            else
                LOGLN("[state:text_test] horizontal align -> %s",
                      text_test_align_h_name(data->block.align_h));

            input_consume();
        }

        if (input_button_pressed(INPUT_BUTTON_L1)) {
            if (data->block.align_v == TEXT_ALIGN_TOP)
                text_block_set_align_v(&data->block, TEXT_ALIGN_MIDDLE);
            else if (data->block.align_v == TEXT_ALIGN_MIDDLE)
                text_block_set_align_v(&data->block, TEXT_ALIGN_BOTTOM);
            else
                text_block_set_align_v(&data->block, TEXT_ALIGN_TOP);

            if (text_test_refresh_block(data) != 0)
                LOGLN("[state:text_test] refresh failed after L1");
            else
                LOGLN("[state:text_test] vertical align -> %s",
                      text_test_align_v_name(data->block.align_v));

            input_consume();
        }

        if (input_button_pressed(INPUT_BUTTON_R1)) {
            if (text_test_cycle_case(data, +1) != 0)
                LOGLN("[state:text_test] refresh failed after R1");
            else {
                const text_test_case_t *tc = text_test_current_case(data);
                LOGLN("[state:text_test] case -> %s", tc ? tc->name : "unknown");
            }

            input_consume();
        }

        if (input_button_pressed(INPUT_BUTTON_R2)) {
            if (text_test_cycle_case(data, -1) != 0)
                LOGLN("[state:text_test] refresh failed after R2");
            else {
                const text_test_case_t *tc = text_test_current_case(data);
                LOGLN("[state:text_test] case -> %s", tc ? tc->name : "unknown");
            }

            input_consume();
        }

        text_block_update(&data->block, dt);
    }
}

static void text_test_draw(game_app_t *app, float alpha)
{
    text_test_state_data_t *data = text_test_data(app);

    (void)alpha;

    if (!data)
        return;

    gfx2d_draw();
    text_block_draw(&data->block);
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