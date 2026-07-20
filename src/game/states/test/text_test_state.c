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

#ifndef TEXT_TEST_RICH_RUN_CAPACITY
#define TEXT_TEST_RICH_RUN_CAPACITY 128
#endif

#ifndef TEXT_TEST_REVEAL_SECONDS_PER_GLYPH
#define TEXT_TEST_REVEAL_SECONDS_PER_GLYPH 0.03f
#endif

#ifndef TEXT_TEST_SHAKE_AMP_STEP
#define TEXT_TEST_SHAKE_AMP_STEP   0.25f
#endif

#ifndef TEXT_TEST_SHAKE_AMP_MIN 
#define TEXT_TEST_SHAKE_AMP_MIN    0.0f
#endif

#ifndef TEXT_TEST_SHAKE_AMP_MAX  
#define TEXT_TEST_SHAKE_AMP_MAX    4.0f
#endif

#ifndef TEXT_TEST_SHAKE_SPEED_STEP 
#define TEXT_TEST_SHAKE_SPEED_STEP 0.25f
#endif

#ifndef TEXT_TEST_SHAKE_SPEED_MIN 
#define TEXT_TEST_SHAKE_SPEED_MIN  0.25f
#endif

#ifndef TEXT_TEST_SHAKE_SPEED_MAX 
#define TEXT_TEST_SHAKE_SPEED_MAX  4.0f
#endif

#ifndef TEXT_TEST_REVEAL_SPEED_MIN 
#define TEXT_TEST_REVEAL_SPEED_MIN  0.25f
#endif

#ifndef TEXT_TEST_REVEAL_SPEED_MAX
#define TEXT_TEST_REVEAL_SPEED_MAX 4.0f
#endif

#ifndef TEXT_TEST_REVEAL_SPEED_STEP
#define TEXT_TEST_REVEAL_SPEED_STEP   0.25f
#endif

#ifndef TEXT_TEST_WAVE_SCALE_MIN 
#define TEXT_TEST_WAVE_SCALE_MIN  0.25f
#endif

#ifndef TEXT_TEST_WAVE_SCALE_MAX
#define TEXT_TEST_WAVE_SCALE_MAX 4.0f
#endif

#ifndef TEXT_TEST_WAVE_SCALE_STEP
#define TEXT_TEST_WAVE_SCALE_STEP   0.25f
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

    text_layout_item_t *rich_items;
    text_layout_line_t *lines;
    text_rich_draw_params_t rich_draw_params;
    text_rich_run_t rich_runs[TEXT_TEST_RICH_RUN_CAPACITY];

    int use_yellow;
    int font_bound_logged;
    int test_case_index;

    float shake_amp_scale;
    float shake_speed_scale;
    float reveal_speed_scale;
    float wave_scale;
} text_test_state_data_t;

typedef struct text_test_case {
    const char *name;
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

static const char s_rich_color_yellow_ok[] =
    "[color=#FFFF00]ok[/color]";

static const char s_rich_shake_ok[] =
    "[shake]ok[/shake]";

static const char s_rich_yello_shake_nested[] =
    "[color=#FFFF00][shake]nested[/shake][/color]";

static const char s_rich_mismatch[] =
    "[color=#FFFF00][shake]mismatch[/color][/shake]";

static const char s_rich_leading_close[] =
    "[/color] leading close";

static const char s_rich_plain[] =
    "[color=#ffd000]Plain [/color]";

static const char s_rich_shake[] =
    "[shake]Shake [/shake]";

static const char s_rich_wave[] =
    "[wave]Wave, волна, волнуется [/wave]";

static const char s_rich_wave_shake[] =
    "[shake][wave]Both[/wave][/shake]";

static const text_test_case_t s_text_test_cases[] = {
    { "demo_word",            TEXT_WRAP_WORD, TEXT_REVEAL_GLYPH, s_demo_word },
    { "long_mixed_char",      TEXT_WRAP_CHAR, TEXT_REVEAL_GLYPH, s_long_mixed_char },
    { "word_simple",          TEXT_WRAP_WORD, TEXT_REVEAL_WORD,  s_word_simple },
    { "char_cyrillic_long",   TEXT_WRAP_CHAR, TEXT_REVEAL_GLYPH, s_char_cyrillic_long },
    { "word_cyrillic_mixed",  TEXT_WRAP_WORD, TEXT_REVEAL_WORD,  s_word_cyrillic_mixed },

    { "rich_color_rgb",       TEXT_WRAP_WORD, TEXT_REVEAL_GLYPH, s_rich_color_rgb },
    { "rich_color_rgba",      TEXT_WRAP_WORD, TEXT_REVEAL_GLYPH, s_rich_color_rgba },
    { "rich_shake_basic",     TEXT_WRAP_WORD, TEXT_REVEAL_GLYPH, s_rich_shake_basic },
    { "rich_color_shake",     TEXT_WRAP_WORD, TEXT_REVEAL_GLYPH, s_rich_color_and_shake },
    { "rich_shake_in_color",  TEXT_WRAP_WORD, TEXT_REVEAL_WORD,  s_rich_nested_shake_in_color },
    { "rich_adjacent_tags",   TEXT_WRAP_WORD, TEXT_REVEAL_GLYPH, s_rich_adjacent_tags },
    { "rich_unclosed_tag",    TEXT_WRAP_WORD, TEXT_REVEAL_WORD,  s_rich_unclosed_color },
    { "rich_reveal_words",    TEXT_WRAP_WORD, TEXT_REVEAL_WORD,  s_rich_reveal_words_basic },
    { "rich_yellow_ok",       TEXT_WRAP_WORD, TEXT_REVEAL_GLYPH, s_rich_color_yellow_ok },
    { "rich_shake_ok",        TEXT_WRAP_WORD, TEXT_REVEAL_WORD,  s_rich_shake_ok },
    { "rich_shake_nested",    TEXT_WRAP_WORD, TEXT_REVEAL_GLYPH, s_rich_yello_shake_nested },
    { "rich_mismatch",        TEXT_WRAP_WORD, TEXT_REVEAL_WORD,  s_rich_mismatch },
    { "rich_leading_close",   TEXT_WRAP_WORD, TEXT_REVEAL_WORD,  s_rich_leading_close },

    { "rich_plain",           TEXT_WRAP_WORD, TEXT_REVEAL_GLYPH, s_rich_plain },
    { "rich_shake",           TEXT_WRAP_WORD, TEXT_REVEAL_GLYPH, s_rich_shake },
    { "rich_wave",            TEXT_WRAP_WORD, TEXT_REVEAL_GLYPH, s_rich_wave },
    { "rich_wave_shake",      TEXT_WRAP_WORD, TEXT_REVEAL_GLYPH, s_rich_wave_shake },

};

static int text_test_case_count(void)
{
    return (int)(sizeof(s_text_test_cases) / sizeof(s_text_test_cases[0]));
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

text_color_t text_color_white(void)
{
    return text_color_rgba(0xFF, 0xFF, 0xFF, 0xFF);
}

text_color_t text_color_yellow(void)
{
    return text_color_rgba(0xFF, 0xFF, 0x00, 0xFF);
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
    text_block_reveal_reset(&data->block);

    if (text_block_refresh(&data->block) != 0) {
        LOGLNC(LOGCAT_TEXT, "[state:text_test] text_block_refresh failed case=%s",
              tc->name);
        return -1;
    }

    LOGLNC(LOGCAT_TEXT, "[state:text_test] block refreshed case=%d/%d name=%s wrap=%d reveal=%s w=%d h=%d align_h=%s align_v=%s",
          (int)(data->test_case_index + 1),
          (int)text_test_case_count(),
          tc->name,
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
        LOGLNC(LOGCAT_STATE, "[state:text_test] enter failed: no state arena memory");
        return -1;
    }

    text_rich_draw_params_init(&data->rich_draw_params);
    
    game_app_set_state_userdata(app, data);

    data->rich_items = (text_layout_item_t *)mem_arena_calloc(
        game_app_state_arena(app),
        TEXT_TEST_GLYPH_CAPACITY,
        sizeof(text_layout_item_t),
        16
    );
    if (!data->rich_items) {
        LOGLNC(LOGCAT_TEXT, "[state:text_test] failed to allocate rich layout item buffer");
        return -1;
    }

    data->lines = (text_layout_line_t *)mem_arena_calloc(
        game_app_state_arena(app),
        TEXT_TEST_LINE_CAPACITY,
        sizeof(text_layout_line_t),
        16
    );
    if (!data->lines) {
        LOGLNC(LOGCAT_TEXT, "[state:text_test] failed to allocate line layout buffer");
        return -1;
    }

    text_block_init(&data->block,
        data->rich_runs,
        TEXT_TEST_RICH_RUN_CAPACITY,
        data->rich_items,
        TEXT_TEST_GLYPH_CAPACITY,
        data->lines,
        TEXT_TEST_LINE_CAPACITY,
        TEXT_TEST_REVEAL_SECONDS_PER_GLYPH);

    data->use_yellow = 0;
    data->font_bound_logged = 0;
    data->test_case_index = 0;
    data->shake_amp_scale = 1.0f;
    data->shake_speed_scale = 1.0f;
    data->reveal_speed_scale = 1.0f;
    data->wave_scale = 1.0f;

    text_block_set_shake_scale(&data->block, data->shake_amp_scale);
    text_block_set_shake_speed_scale(&data->block, data->shake_speed_scale);
    text_block_set_wave_scale(&data->block, data->wave_scale);
    text_block_set_wave_speed_scale(&data->block, data->shake_speed_scale);

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
        LOGLNC(LOGCAT_TEXT, "[state:text_test] failed to request font resource");
        return -1;
    }

    LOGLNC(LOGCAT_TEXT, "[state:text_test] enter requested descriptor=%s atlas=%s box=(%d,%d,%d,%d)",
          TEST_FONT_DESC_PATH,
          TEST_FONT_ATLAS_PATH,
          (int)TEXT_TEST_BOX_X,
          (int)TEXT_TEST_BOX_Y,
          (int)TEXT_TEST_BOX_W,
          (int)TEXT_TEST_BOX_H);

    LOGLNC(LOGCAT_TEXT, "[state:text_test] controls: CROSS=color CIRCLE=reveal_reset TRIANGLE=reveal_finish SQUARE=align_h L1=align_v R1/R2=case");
    return 0;
}

static void text_test_exit(game_app_t *app)
{
    text_test_state_data_t *data = text_test_data(app);

    if (!data) {
        LOGLNC(LOGCAT_STATE, "[state:text_test] exit");
        return;
    }

    text_font_resource_shutdown(app, &data->font_res);
    game_app_set_state_userdata(app, NULL);

    LOGLNC(LOGCAT_STATE, "[state:text_test] exit");
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
        LOGLNC(LOGCAT_STATE, "[state:text_test] START pressed, return to menu");
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
            LOGLNC(LOGCAT_TEXT, "[state:text_test] initial refresh failed");
            return;
        }

        if (!data->font_bound_logged) {
            LOGLNC(LOGCAT_TEXT, "[state:text_test] font bound glyphs=%u kernings=%u w=%d h=%d align_h=%s align_v=%s",
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
                LOGLNC(LOGCAT_TEXT, "[state:text_test] refresh failed after CROSS");

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
                LOGLNC(LOGCAT_TEXT, "[state:text_test] refresh failed after SQUARE");
            else
                LOGLNC(LOGCAT_TEXT, "[state:text_test] horizontal align -> %s",
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
                LOGLNC(LOGCAT_TEXT, "[state:text_test] refresh failed after L1");
            else
                LOGLNC(LOGCAT_TEXT, "[state:text_test] vertical align -> %s",
                      text_test_align_v_name(data->block.align_v));

            input_consume();
        }

        if (input_button_pressed(INPUT_BUTTON_R1)) {
            if (text_test_cycle_case(data, +1) != 0)
                LOGLNC(LOGCAT_TEXT, "[state:text_test] refresh failed after R1");
            else {
                const text_test_case_t *tc = text_test_current_case(data);
                LOGLNC(LOGCAT_TEXT, "[state:text_test] case -> %s", tc ? tc->name : "unknown");
            }

            input_consume();
        }

        if (input_button_pressed(INPUT_BUTTON_L2)) {
            if (input_button_down(INPUT_BUTTON_TRIANGLE)) {
                data->shake_speed_scale -= TEXT_TEST_SHAKE_SPEED_STEP;
                if (data->shake_speed_scale < TEXT_TEST_SHAKE_SPEED_MIN)
                    data->shake_speed_scale = TEXT_TEST_SHAKE_SPEED_MIN;

                text_block_set_shake_speed_scale(&data->block, data->shake_speed_scale);

                LOGLNC(LOGCAT_TEXT, "[state:text_test] shake speed scale -> %.2f",
                      data->shake_speed_scale);
            } else {
                data->shake_amp_scale -= TEXT_TEST_SHAKE_AMP_STEP;
                if (data->shake_amp_scale < TEXT_TEST_SHAKE_AMP_MIN)
                    data->shake_amp_scale = TEXT_TEST_SHAKE_AMP_MIN;

                text_block_set_shake_scale(&data->block, data->shake_amp_scale);

                LOGLNC(LOGCAT_TEXT, "[state:text_test] shake amp scale -> %.2f",
                      data->shake_amp_scale);
            }

            input_consume();
        }

        if (input_button_pressed(INPUT_BUTTON_R2)) {
            if (input_button_down(INPUT_BUTTON_TRIANGLE)) {
                data->shake_speed_scale += TEXT_TEST_SHAKE_SPEED_STEP;
                if (data->shake_speed_scale > TEXT_TEST_SHAKE_SPEED_MAX)
                    data->shake_speed_scale = TEXT_TEST_SHAKE_SPEED_MAX;

                text_block_set_shake_speed_scale(&data->block, data->shake_speed_scale);

                LOGLNC(LOGCAT_TEXT, "[state:text_test] shake speed scale -> %.2f",
                      data->shake_speed_scale);
            } else {
                data->shake_amp_scale += TEXT_TEST_SHAKE_AMP_STEP;
                if (data->shake_amp_scale > TEXT_TEST_SHAKE_AMP_MAX)
                    data->shake_amp_scale = TEXT_TEST_SHAKE_AMP_MAX;

                text_block_set_shake_scale(&data->block, data->shake_amp_scale);

                LOGLNC(LOGCAT_TEXT, "[state:text_test] shake amp scale -> %.2f",
                      data->shake_amp_scale);
            }

            input_consume();
        }

        if (input_button_pressed(INPUT_BUTTON_RIGHT)) {
            
            data->reveal_speed_scale += TEXT_TEST_REVEAL_SPEED_STEP;
            if (data->reveal_speed_scale > TEXT_TEST_REVEAL_SPEED_MAX)
                data->reveal_speed_scale = TEXT_TEST_REVEAL_SPEED_MAX;

            text_block_set_reveal_speed_scale(&data->block, data->reveal_speed_scale);

            LOGLNC(LOGCAT_TEXT, "[state:text_test] reveal speed scale -> %.2f",
                data->reveal_speed_scale);

            input_consume();
        }

        if (input_button_pressed(INPUT_BUTTON_LEFT)) {
            data->reveal_speed_scale -= TEXT_TEST_REVEAL_SPEED_STEP;
            if (data->reveal_speed_scale < TEXT_TEST_REVEAL_SPEED_MIN)
                data->reveal_speed_scale = TEXT_TEST_REVEAL_SPEED_MIN;

            text_block_set_reveal_speed_scale(&data->block, data->reveal_speed_scale);

            LOGLNC(LOGCAT_TEXT, "[state:text_test] reveal speed scale -> %.2f",
                data->reveal_speed_scale);

            input_consume();
        }

        if (input_button_pressed(INPUT_BUTTON_UP)) {
            data->wave_scale += TEXT_TEST_WAVE_SCALE_STEP;
            if (data->wave_scale > TEXT_TEST_WAVE_SCALE_MAX)
                data->wave_scale = TEXT_TEST_WAVE_SCALE_MAX;

            text_block_set_wave_scale(&data->block, data->wave_scale);

            LOGLNC(LOGCAT_TEXT, "[state:text_test] wave scale -> %.2f",
                data->wave_scale);

            input_consume();
        }

        if (input_button_pressed(INPUT_BUTTON_DOWN)) {
            data->wave_scale -= TEXT_TEST_WAVE_SCALE_STEP;
            if (data->wave_scale < TEXT_TEST_WAVE_SCALE_MIN)
                data->wave_scale = TEXT_TEST_WAVE_SCALE_MIN;

            text_block_set_wave_scale(&data->block, data->wave_scale);

            LOGLNC(LOGCAT_TEXT, "[state:text_test] wave scale -> %.2f",
                data->wave_scale);

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