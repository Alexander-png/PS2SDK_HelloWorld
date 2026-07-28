#include "engine/logging/log.h"
#include "engine/memory/memory_arena.h"
#include "engine/gfx/draw2d.h"
#include "engine/gfx/renderer.h"
#include "engine/gfx/sprite.h"
#include "engine/resources/texture_assets.h"
#include "engine/audio/audio.h"
#include "engine/input/input.h"
#include "engine/text/text.h"
#include "engine/resources/text_font_resource.h"

#include "game/states/test/debug_menu_state.h"
#include "game/states/test/intro_test_state.h"
#include "game/text/text_block.h"

#include <stdio.h>

#ifndef AUDIO_BUFFER_SIZE
#define AUDIO_BUFFER_SIZE (128 * 1024)
#endif

#ifndef FONT_DESC_PATH
#define FONT_DESC_PATH "assets/test/fonts/8bitoperator_32.fnt"
#endif

#ifndef FONT_ATLAS_PATH
#define FONT_ATLAS_PATH "assets/test/textures/8bitoperator_32.png"
#endif

#ifndef TEXT_RICH_RUN_CAPACITY
#define TEXT_RICH_RUN_CAPACITY 128
#endif

#ifndef TEXT_GLYPH_CAPACITY
#define TEXT_GLYPH_CAPACITY 512
#endif

#ifndef TEXT_LINE_CAPACITY
#define TEXT_LINE_CAPACITY 32
#endif

#ifndef TEXT_REVEAL_SECONDS_PER_GLYPH
//#define TEXT_REVEAL_SECONDS_PER_GLYPH 0.08f
#define TEXT_REVEAL_SECONDS_PER_GLYPH 0.03f
#endif

#ifndef LOGO_W
#define LOGO_W 475.0f
#endif

#ifndef LOGO_H
#define LOGO_H 48.0f
#endif

#ifndef LOGO_YELLOW_W
#define LOGO_YELLOW_W 304.0f
#endif

#ifndef LOGO_YELLOW_H
#define LOGO_YELLOW_H 48.0f
#endif

#ifndef LOGO_GAP
#define LOGO_GAP 12.0f
#endif

#ifndef INTRO_LOGO_MIN_SECONDS
#define INTRO_LOGO_MIN_SECONDS 1.5f
#endif

#ifndef INTRO_SLIDE_W
#define INTRO_SLIDE_W 400.0f
#endif

#ifndef INTRO_SLIDE_H
#define INTRO_SLIDE_H 220.0f
#endif

#ifndef INTRO_SLIDE_DEFAULT_SECONDS
#define INTRO_SLIDE_DEFAULT_SECONDS 3.0f
#endif

#ifndef INTRO_MAX_SLOTS
#define INTRO_MAX_SLOTS 8
#endif

#define ARRAY_COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

typedef struct intro_test_state_data intro_test_state_data_t;

static unsigned char intro_test_alpha_to_u8(float alpha)
{
    int a;

    if (alpha <= 0.0f)
        return 0x00;
    if (alpha >= 1.0f)
        return 0xFF;

    a = (int)(alpha * 255.0f + 0.5f);

    if (a < 0)
        a = 0;
    else if (a > 255)
        a = 255;

    return (unsigned char)a;
}

typedef enum state_stage {
    MENU_LANG_SELECT = 0,
    LOGO,
    INTRO
} state_stage_t;

typedef enum intro_action_result {
    INTRO_ACTION_CONTINUE = 0,
    INTRO_ACTION_NEXT_SLIDE,
    INTRO_ACTION_EXIT_INTRO
} intro_action_result_t;

typedef struct intro_slide {
    const char *name;
    const intro_subslide_desc_t *slides;
    int slide_count;
    const char **strings;
    int string_count;
    float showing_time_sec;
    intro_action_result_t (*action)(game_app_t *app,
                                    intro_test_state_data_t *data,
                                    float dt);
} intro_slide_t;

typedef struct audio_descriptor {
    int asset;
    int ok;
} audio_descriptor_t;

typedef struct intro_visual {
    const char *path;
    texture_handle_t texture;
    gfx_texture_handle_t gfx_texture;
    gfx_sprite_id_t sprite_id;
    int requested;
    int prewarmed;
    int sprite_created;
    gfx_draw_params_t draw_params;

    float alpha;
    float pan_x;
    float pan_y;
    int use_region;
    gfx_rect_t src_rect;

    int tex_w;
    int tex_h;

    float anchor_x;
    float anchor_y;
    float box_w;
    float box_h;
    subslide_visual_mode_t mode;
    float scale;
} intro_visual_t;

typedef struct intro_test_state_data {
    int screen_width;
    int screen_height;

    state_stage_t stage;
    state_option_t option;
    int current_slide;
    int current_subslide;
    int current_text_line;
    int visual_dirty;
    int text_dirty;

    float stage_time;
    float logo_time;
    float slide_time;

    const intro_slide_t *slides;
    int slide_count;

    text_font_resource_t font_res;

    text_layout_item_t *rich_items;
    text_layout_line_t *lines;
    text_block_t block;
    text_rich_run_t rich_runs[TEXT_RICH_RUN_CAPACITY];

    int mus_intro_voice;
    audio_descriptor_t mus_intro;
    audio_descriptor_t mus_intronoise;
    audio_descriptor_t snd_confirm;
    audio_descriptor_t snd_menu_select;
    audio_descriptor_t sndfnt;

    char menu_markup[256];

    intro_visual_t logo_main;
    intro_visual_t logo_sub;

    intro_visual_t intro_slots[INTRO_MAX_SLOTS];

    float fade_alpha;
} intro_test_state_data_t;

static float intro_lerp01(float a, float b, float t)
{
    if (t <= a)
        return 0.0f;
    if (t >= b)
        return 1.0f;
    return (t - a) / (b - a);
}

static float intro_invlerp01(float a, float b, float t)
{
    return 1.0f - intro_lerp01(a, b, t);
}

static void intro_slot_set_alpha(intro_visual_t *v, float alpha)
{
    if (!v || !v->requested)
        return;

    if (alpha < 0.0f)
        alpha = 0.0f;
    if (alpha > 1.0f)
        alpha = 1.0f;

    v->alpha = alpha;
}

static const intro_slide_t *intro_test_slide_desc(const intro_test_state_data_t *data)
{
    if (!data || !data->slides || data->slide_count <= 0)
        return NULL;

    if (data->current_slide < 0 || data->current_slide >= data->slide_count)
        return NULL;

    return &data->slides[data->current_slide];
}

static void intro_test_set_text(intro_test_state_data_t *data, const char *text)
{
    if (!data)
        return;

    text_block_set_text(&data->block, text ? text : "");
    text_block_refresh(&data->block);
}

static void intro_test_set_text_line(intro_test_state_data_t *data, int line_index)
{
    const intro_slide_t *slide;

    if (!data)
        return;

    if (data->current_text_line == line_index)
        return;

    slide = intro_test_slide_desc(data);
    if (!slide || !slide->strings || line_index < 0 || line_index >= slide->string_count)
        return;

    data->current_text_line = line_index;
    intro_test_set_text(data, slide->strings[line_index]);
}

static void intro_test_apply_visual_region(intro_visual_t *v)
{
    if (!v || !v->sprite_created)
        return;

    if (v->use_region)
        gfx_sprite_set_region(v->sprite_id, &v->src_rect);
    else
        gfx_sprite_clear_region(v->sprite_id);
}

static void intro_slot_set_region(intro_visual_t *v,
                                  float x, float y,
                                  float w, float h)
{
    if (!v || !v->requested)
        return;

    if (w < 1.0f)
        w = 1.0f;
    if (h < 1.0f)
        h = 1.0f;

    v->use_region = 1;
    v->src_rect.x = x;
    v->src_rect.y = y;
    v->src_rect.w = w;
    v->src_rect.h = h;
}

static void intro_slot_clear_region(intro_visual_t *v)
{
    if (!v)
        return;

    v->use_region = 0;
}

static void intro_slot_set_scale(intro_visual_t *v, float scale)
{
    if (!v || !v->requested)
        return;

    if (scale < 0.01f)
        scale = 0.01f;

    v->scale = scale;
}

static intro_action_result_t intro_slide1_action(game_app_t *app,
                                                 intro_test_state_data_t *data,
                                                 float dt)
{
    float t;

    (void)app;
    (void)dt;

    t = data->slide_time;

    data->fade_alpha = 0.0f;
    intro_slot_set_alpha(&data->intro_slots[0], 0.0f);
    intro_slot_set_alpha(&data->intro_slots[1], 0.0f);
    intro_slot_set_alpha(&data->intro_slots[2], 0.0f);

    if (t < 2.0f) {
        intro_slot_set_alpha(&data->intro_slots[0], 1.0f);
        return INTRO_ACTION_CONTINUE;
    }

    if (t < 2.4f) {
        float k = intro_lerp01(2.0f, 2.4f, t);
        intro_slot_set_alpha(&data->intro_slots[0], 1.0f - k);
        intro_slot_set_alpha(&data->intro_slots[1], k);
        intro_slot_set_alpha(&data->intro_slots[2], k);
        return INTRO_ACTION_CONTINUE;
    }

    if (t < 4.4f) {
        intro_slot_set_alpha(&data->intro_slots[1], 1.0f);
        intro_slot_set_alpha(&data->intro_slots[2], 1.0f);
        return INTRO_ACTION_CONTINUE;
    }

    if (t < 4.8f) {
        float k = intro_lerp01(4.4f, 4.8f, t);
        intro_slot_set_alpha(&data->intro_slots[1], 1.0f - k);
        intro_slot_set_alpha(&data->intro_slots[2], 1.0f);

        return INTRO_ACTION_CONTINUE;
    }

    if (t < 6.8f) {
        intro_slot_set_alpha(&data->intro_slots[2], 1.0f);
        return INTRO_ACTION_CONTINUE;
    }

    if (t < 7.2f) {
        float k = intro_lerp01(6.8f, 7.2f, t);
        intro_slot_set_alpha(&data->intro_slots[2], 1.0f - k);
        return INTRO_ACTION_CONTINUE;
    }

    if (t < 7.5f)
        return INTRO_ACTION_CONTINUE;

    return INTRO_ACTION_NEXT_SLIDE;
}

static intro_action_result_t intro_slide2_action(game_app_t *app,
                                                 intro_test_state_data_t *data,
                                                 float dt)
{
    intro_visual_t *v;
    float t;
    float fade_in_k;
    float pan_k;
    float fade_out_k;
    float alpha;
    float region_w;
    float region_h;
    float max_x;
    float src_x;

    (void)app;
    (void)dt;

    t = data->slide_time;
    data->fade_alpha = 0.0f;

    v = &data->intro_slots[0];
    if (!v->requested)
        return INTRO_ACTION_CONTINUE;

    fade_in_k = intro_lerp01(0.0f, 0.8f, t);
    pan_k = intro_lerp01(0.3f, 2.8f, t);
    fade_out_k = intro_lerp01(3.2f, 3.8f, t);

    alpha = fade_in_k * (1.0f - fade_out_k);
    intro_slot_set_alpha(v, alpha);

    if (v->tex_w > 0 && v->tex_h > 0) {
        region_w = (float)v->tex_w * 0.5f;
        region_h = (float)v->tex_h;

        if (region_w < 1.0f)
            region_w = 1.0f;
        if (region_h < 1.0f)
            region_h = 1.0f;

        if (region_w > (float)v->tex_w)
            region_w = (float)v->tex_w;
        if (region_h > (float)v->tex_h)
            region_h = (float)v->tex_h;

        max_x = (float)v->tex_w - region_w;
        if (max_x < 0.0f)
            max_x = 0.0f;

        src_x = max_x * pan_k;

        intro_slot_set_region(v,
                              src_x,
                              0.0f,
                              region_w,
                              region_h);
    }

    if (t < 4.0f)
        return INTRO_ACTION_CONTINUE;

    return INTRO_ACTION_NEXT_SLIDE;
}

static intro_action_result_t intro_slide3_action(game_app_t *app,
                                                 intro_test_state_data_t *data,
                                                 float dt)
{
    intro_visual_t *v;
    float t;
    float alpha;

    (void)app;
    (void)dt;

    t = data->slide_time;
    data->fade_alpha = 0.0f;

    v = &data->intro_slots[0];
    if (!v->requested)
        return INTRO_ACTION_CONTINUE;

    if (t < 0.4f) {
        alpha = intro_lerp01(0.0f, 0.4f, t);
    } else if (t < 2.0f) {
        alpha = 1.0f;
    } else if (t < 2.4f) {
        alpha = 1.0f - intro_lerp01(2.0f, 2.4f, t);
    } else {
        alpha = 0.0f;
    }

    intro_slot_set_alpha(v, alpha);
    intro_slot_clear_region(v);

    if (t < 2.7f)
        return INTRO_ACTION_CONTINUE;

    return INTRO_ACTION_NEXT_SLIDE;
}

static intro_action_result_t intro_slide4_action(game_app_t *app,
                                                 intro_test_state_data_t *data,
                                                 float dt)
{
    float t;
    float k;

    (void)app;
    (void)dt;

    t = data->slide_time;
    data->fade_alpha = 0.0f;

    intro_slot_set_alpha(&data->intro_slots[0], 0.0f);
    intro_slot_set_alpha(&data->intro_slots[1], 0.0f);
    intro_slot_set_alpha(&data->intro_slots[2], 0.0f);
    intro_slot_set_alpha(&data->intro_slots[3], 0.0f);

    intro_slot_set_scale(&data->intro_slots[1], 2.0f);
    intro_slot_set_scale(&data->intro_slots[2], 2.0f);
    intro_slot_set_scale(&data->intro_slots[3], 2.0f);

    intro_slot_clear_region(&data->intro_slots[0]);
    intro_slot_clear_region(&data->intro_slots[1]);
    intro_slot_clear_region(&data->intro_slots[2]);
    intro_slot_clear_region(&data->intro_slots[3]);

    if (t < 0.4f) {
        k = intro_lerp01(0.0f, 0.4f, t);
        intro_slot_set_alpha(&data->intro_slots[0], k);
        intro_slot_set_alpha(&data->intro_slots[1], k);
        intro_slot_set_alpha(&data->intro_slots[2], k);
        return INTRO_ACTION_CONTINUE;
    }

    if (t < 0.8f) {
        intro_slot_set_alpha(&data->intro_slots[0], 1.0f);
        intro_slot_set_alpha(&data->intro_slots[1], 1.0f);
        intro_slot_set_alpha(&data->intro_slots[2], 1.0f);
        return INTRO_ACTION_CONTINUE;
    }

    if (t < 1.2f) {
        k = intro_lerp01(0.8f, 1.2f, t);
        intro_slot_set_alpha(&data->intro_slots[0], 1.0f - k);
        intro_slot_set_alpha(&data->intro_slots[1], 1.0f - k);
        intro_slot_set_alpha(&data->intro_slots[2], 1.0f);
        return INTRO_ACTION_CONTINUE;
    }

    if (t < 1.6f) {
        intro_slot_set_alpha(&data->intro_slots[2], 1.0f);
        return INTRO_ACTION_CONTINUE;
    }

    if (t < 2.0f) {
        k = intro_lerp01(1.6f, 2.0f, t);
        intro_slot_set_alpha(&data->intro_slots[2], 1.0f);
        intro_slot_set_alpha(&data->intro_slots[3], k);
        return INTRO_ACTION_CONTINUE;
    }

    if (t < 2.4f) {
        intro_slot_set_alpha(&data->intro_slots[2], 1.0f);
        intro_slot_set_alpha(&data->intro_slots[3], 1.0f);
        return INTRO_ACTION_CONTINUE;
    }

    if (t < 2.5f) {
        intro_slot_set_alpha(&data->intro_slots[2], 1.0f);
        intro_slot_set_alpha(&data->intro_slots[3], 1.0f);
        intro_test_set_text_line(data, 1);
        return INTRO_ACTION_CONTINUE;
    }

    if (t < 3.0f) {
        intro_slot_set_alpha(&data->intro_slots[2], 1.0f);
        intro_slot_set_alpha(&data->intro_slots[3], 1.0f);
        return INTRO_ACTION_CONTINUE;
    }

    if (t < 3.4f) {
        k = intro_lerp01(3.0f, 3.4f, t);
        intro_slot_set_alpha(&data->intro_slots[2], 1.0f - k);
        intro_slot_set_alpha(&data->intro_slots[3], 1.0f - k);
        return INTRO_ACTION_CONTINUE;
    }

    if (t < 3.7f)
        return INTRO_ACTION_CONTINUE;

    return INTRO_ACTION_NEXT_SLIDE;
}

static intro_action_result_t intro_slide5_action(game_app_t *app,
                                                 intro_test_state_data_t *data,
                                                 float dt)
{
    intro_visual_t *v;
    float t;
    float alpha;

    (void)app;
    (void)dt;

    t = data->slide_time;
    data->fade_alpha = 0.0f;

    v = &data->intro_slots[0];
    if (!v->requested)
        return INTRO_ACTION_CONTINUE;

    intro_slot_clear_region(v);

    if (t >= 1.4f)
        intro_test_set_text_line(data, 1);

    if (t < 0.4f) {
        alpha = intro_lerp01(0.0f, 0.4f, t);
    } else if (t < 2.5f) {
        alpha = 1.0f;
    } else if (t < 2.9f) {
        alpha = intro_invlerp01(2.5f, 2.9f, t);
    } else {
        alpha = 0.0f;
    }

    intro_slot_set_alpha(v, alpha);

    if (t < 3.2f)
        return INTRO_ACTION_CONTINUE;

    return INTRO_ACTION_NEXT_SLIDE;
}

static intro_action_result_t intro_slide10_action(game_app_t *app,
                                                  intro_test_state_data_t *data,
                                                  float dt)
{
    intro_visual_t *bg;
    intro_visual_t *fg;
    float t;
    float fade_k;
    float move_k;
    float out_k;
    float src_y;
    const float fg_start_y = 8.0f;
    const float fg_end_y = -25.0f;

    (void)app;
    (void)dt;

    t = data->slide_time;
    data->fade_alpha = 0.0f;

    bg = &data->intro_slots[0];
    fg = &data->intro_slots[1];

    if (!fg->requested || !bg->requested)
        return INTRO_ACTION_CONTINUE;

    intro_slot_set_alpha(fg, 0.0f);
    intro_slot_set_alpha(bg, 0.0f);

    intro_slot_set_scale(fg, 2.0f);

    intro_slot_set_region(bg, 0.0f, 0.0f, 200.0f, 110.0f);
    intro_slot_clear_region(fg);

    bg->pan_y = 8.0f;
    fg->pan_y = fg_start_y;

    if (t < 0.5f) {
        fade_k = intro_lerp01(0.0f, 0.5f, t);
        intro_slot_set_alpha(fg, fade_k);
        intro_slot_set_alpha(bg, fade_k);
        return INTRO_ACTION_CONTINUE;
    }

    if (t < 1.0f) {
        intro_slot_set_alpha(fg, 1.0f);
        intro_slot_set_alpha(bg, 1.0f);
        return INTRO_ACTION_CONTINUE;
    }

    if (t < 3.5f) {
        move_k = intro_lerp01(1.0f, 3.5f, t);

        src_y = 60.0f * move_k;
        intro_slot_set_region(bg, 0.0f, src_y, 200.0f, 110.0f);

        fg->pan_y = fg_start_y + (fg_end_y - fg_start_y) * move_k;

        intro_slot_set_alpha(fg, 1.0f);
        intro_slot_set_alpha(bg, 1.0f);
        return INTRO_ACTION_CONTINUE;
    }

    if (t < 4.0f) {
        intro_slot_set_region(bg, 0.0f, 60.0f, 200.0f, 110.0f);
        fg->pan_y = fg_end_y;

        intro_slot_set_alpha(fg, 1.0f);
        intro_slot_set_alpha(bg, 1.0f);
        return INTRO_ACTION_CONTINUE;
    }

    if (t < 4.4f) {
        out_k = intro_lerp01(4.0f, 4.4f, t);

        intro_slot_set_region(bg, 0.0f, 60.0f, 200.0f, 110.0f);
        fg->pan_y = fg_end_y;

        intro_slot_set_alpha(fg, 1.0f - out_k);
        intro_slot_set_alpha(bg, 1.0f - out_k);
        return INTRO_ACTION_CONTINUE;
    }

    if (t < 4.7f)
        return INTRO_ACTION_CONTINUE;

    return INTRO_ACTION_NEXT_SLIDE;
}

static const intro_slide_t slides[] = {
    {
        "intro_slide_1",
        intro1_slides,
        ARRAY_COUNT(intro1_slides),
        intro1_text,
        ARRAY_COUNT(intro1_text),
        0.0f,
        intro_slide1_action
    },
    {
        "intro_slide_2",
        intro2_slides,
        ARRAY_COUNT(intro2_slides),
        intro2_text,
        ARRAY_COUNT(intro2_text),
        0.0f,
        intro_slide2_action
    },
    {
        "intro_slide_3",
        intro3_slides,
        ARRAY_COUNT(intro3_slides),
        intro3_text,
        ARRAY_COUNT(intro3_text),
        0.0f,
        intro_slide3_action
    },
    {
        "intro_slide_4",
        intro4_slides,
        ARRAY_COUNT(intro4_slides),
        intro4_text,
        ARRAY_COUNT(intro4_text),
        0.0f,
        intro_slide4_action
    },
    {
        "intro_slide_5",
        intro5_slides,
        ARRAY_COUNT(intro5_slides),
        intro5_text,
        ARRAY_COUNT(intro5_text),
        0.0f,
        intro_slide5_action
    },
    {
        "intro_slide_6",
        intro6_slides,
        ARRAY_COUNT(intro6_slides),
        intro6_text,
        ARRAY_COUNT(intro6_text),
        0.0f,
        intro_slide3_action
    },
    {
        "intro_slide_7",
        intro7_slides,
        ARRAY_COUNT(intro7_slides),
        NULL,
        0,
        0.0f,
        intro_slide3_action
    },
    {
        "intro_slide_8",
        intro8_slides,
        ARRAY_COUNT(intro8_slides),
        NULL,
        0,
        0.0f,
        intro_slide3_action
    },
    {
        "intro_slide_9",
        intro9_slides,
        ARRAY_COUNT(intro9_slides),
        NULL,
        0,
        0.0f,
        intro_slide3_action
    },
    {
        "intro_slide_10",
        intro10_slides,
        ARRAY_COUNT(intro10_slides),
        NULL,
        0,
        0.0f,
        intro_slide10_action
    },
};

static const intro_slide_t slides_ru[] = {
    {
        "intro_slide_1",
        intro1_slides_ru,
        ARRAY_COUNT(intro1_slides_ru),
        intro1_text_ru,
        ARRAY_COUNT(intro1_text_ru),
        0.0f,
        intro_slide1_action
    },
    {
        "intro_slide_2",
        intro2_slides_ru,
        ARRAY_COUNT(intro2_slides_ru),
        intro2_text_ru,
        ARRAY_COUNT(intro2_text_ru),
        0.0f,
        intro_slide2_action
    },
    {
        "intro_slide_3",
        intro3_slides_ru,
        ARRAY_COUNT(intro3_slides_ru),
        intro3_text_ru,
        ARRAY_COUNT(intro3_text_ru),
        0.0f,
        intro_slide3_action
    },
    {
        "intro_slide_4",
        intro4_slides_ru,
        ARRAY_COUNT(intro4_slides_ru),
        intro4_text_ru,
        ARRAY_COUNT(intro4_text_ru),
        0.0f,
        intro_slide4_action
    },
    {
        "intro_slide_5",
        intro5_slides_ru,
        ARRAY_COUNT(intro5_slides_ru),
        intro5_text_ru,
        ARRAY_COUNT(intro5_text_ru),
        0.0f,
        intro_slide5_action
    },
    {
        "intro_slide_6",
        intro6_slides_ru,
        ARRAY_COUNT(intro6_slides_ru),
        intro6_text_ru,
        ARRAY_COUNT(intro6_text_ru),
        0.0f,
        intro_slide3_action
    },
    {
        "intro_slide_7",
        intro7_slides_ru,
        ARRAY_COUNT(intro7_slides_ru),
        NULL,
        0,
        0.0f,
        intro_slide3_action
    },
    {
        "intro_slide_8",
        intro8_slides_ru,
        ARRAY_COUNT(intro8_slides_ru),
        NULL,
        0,
        0.0f,
        intro_slide3_action
    },
    {
        "intro_slide_9",
        intro9_slides_ru,
        ARRAY_COUNT(intro9_slides_ru),
        NULL,
        0,
        0.0f,
        intro_slide3_action
    },
    {
        "intro_slide_10",
        intro10_slides_ru,
        ARRAY_COUNT(intro10_slides_ru),
        NULL,
        0,
        0.0f,
        intro_slide10_action
    },
};

static intro_test_state_data_t *intro_test_data(game_app_t *app)
{
    return GAME_APP_STATE_DATA_AS(app, intro_test_state_data_t);
}

static int open_music(audio_descriptor_t *descriptor, const char *path)
{
    descriptor->asset = audio_asset_load_stream(path, AUDIO_BUFFER_SIZE);

    if (descriptor->asset < 0) {
        LOGLNC(LOGCAT_AUDIO, "[state:intro_test] audio_asset_load_stream failed: %d",
              descriptor->asset);
        descriptor->asset = -1;
        return -1;
    }

    descriptor->ok = 1;

    if (audio_asset_preload(descriptor->asset) < 0) {
        LOGLNC(LOGCAT_AUDIO, "[state:audio_mix_test] audio_asset_preload failed: %d",
              descriptor->asset);
        audio_asset_unload(descriptor->asset);
        descriptor->asset = -1;
        descriptor->ok = 0;
        return -1;
    }
    return 0;
}

static int load_sfx(audio_descriptor_t *descriptor, const char *path)
{
    descriptor->asset = audio_asset_load_sfx(path);
    if (descriptor->asset < 0) {
        descriptor->ok = 0;
        LOGLNC(LOGCAT_AUDIO, "[state:intro_test] load failed path=%s rc=%d",
              path, descriptor->asset);
        return -1;
    }

    descriptor->ok = 1;
    LOGLNC(LOGCAT_AUDIO, "[state:intro_test] loaded handle=%d path=%s",
          descriptor->asset, path);
    return 0;
}

static void reset_visual(intro_visual_t *v)
{
    if (!v)
        return;

    v->path = NULL;
    v->texture = texture_invalid_handle();
    v->gfx_texture = gfx_texture_invalid();
    v->sprite_id = -1;
    v->requested = 0;
    v->prewarmed = 0;
    v->sprite_created = 0;
    v->draw_params = gfx_draw_params_default(0.0f, 0.0f, 0.0f, 0.0f);
    v->alpha = 1.0f;
    v->pan_x = 0.0f;
    v->pan_y = 0.0f;
    v->use_region = 0;
    v->src_rect.x = 0.0f;
    v->src_rect.y = 0.0f;
    v->src_rect.w = 0.0f;
    v->src_rect.h = 0.0f;
    v->tex_w = 0;
    v->tex_h = 0;
    v->anchor_x = 0.0f;
    v->anchor_y = 0.0f;
    v->box_w = 0.0f;
    v->box_h = 0.0f;
    v->mode = INTRO_VISUAL_STRETCH;
    v->scale = 1.0f;
}

static void intro_test_clear_intro_slots(intro_test_state_data_t *data)
{
    int i;

    if (!data)
        return;

    for (i = 0; i < INTRO_MAX_SLOTS; ++i)
        reset_visual(&data->intro_slots[i]);
}

static int intro_test_request_visual(intro_visual_t *v,
                                     const char *path)
{
    if (!v || !path)
        return -1;

    if (v->requested)
        return 0;

    v->texture = texture_load_png(path, STREAM_PRIORITY_NORMAL);
    if (!texture_is_valid(v->texture)) {
        LOGLNC(LOGCAT_RESOURCES,
              "[state:intro_test] failed to request texture path=%s",
              path);
        return -1;
    }

    v->path = path;
    v->requested = 1;

    LOGLNC(LOGCAT_RESOURCES,
          "[state:intro_test] requested texture path=%s",
          path);
    return 0;
}

static int intro_test_request_slot_visual(intro_visual_t *v,
                                          const char *path,
                                          float x,
                                          float y,
                                          float w,
                                          float h,
                                          int layer,
                                          subslide_visual_mode_t mode)
{
    if (!v || !path)
        return -1;

    if (!v->requested) {
        v->anchor_x = x;
        v->anchor_y = y;
        v->box_w = w;
        v->box_h = h;
        v->mode = mode;

        v->draw_params = gfx_draw_params_default(x, y, w, h);
        v->draw_params.origin_v = GFX_VALIGN_TOP;
        v->draw_params.origin_h = GFX_HALIGN_LEFT;
        v->draw_params.layer = layer;
    }

    return intro_test_request_visual(v, path);
}

static void intro_test_release_visual(intro_visual_t *v)
{
    if (!v)
        return;

    if (v->sprite_created)
        gfx_sprite_remove(v->sprite_id);

    if (texture_is_valid(v->texture))
        texture_release(v->texture);

    reset_visual(v);
}

static void intro_test_release_intro_slots(intro_test_state_data_t *data)
{
    int i;

    if (!data)
        return;

    for (i = 0; i < INTRO_MAX_SLOTS; ++i)
        intro_test_release_visual(&data->intro_slots[i]);
}

static void intro_test_resolve_visual_rect(intro_visual_t *v)
{
    float src_w;
    float src_h;

    if (!v)
        return;

    src_w = (v->use_region && v->src_rect.w > 0.0f) ? v->src_rect.w : (float)v->tex_w;
    src_h = (v->use_region && v->src_rect.h > 0.0f) ? v->src_rect.h : (float)v->tex_h;

    if (src_w <= 0.0f || src_h <= 0.0f)
        return;

    switch (v->mode) {
        case INTRO_VISUAL_NATIVE:
            v->draw_params.w = src_w;
            v->draw_params.h = src_h;
            v->draw_params.x = v->anchor_x + v->box_w * 0.5f;
            v->draw_params.y = v->anchor_y + v->box_h * 0.5f;
            v->draw_params.anchor_h = GFX_HALIGN_CENTER;
            v->draw_params.anchor_v = GFX_VALIGN_CENTER;
            v->draw_params.origin_h = GFX_HALIGN_CENTER;
            v->draw_params.origin_v = GFX_VALIGN_CENTER;
            break;

        case INTRO_VISUAL_FIT: {
            float sx = v->box_w / src_w;
            float sy = v->box_h / src_h;
            float s = (sx < sy) ? sx : sy;

            v->draw_params.w = src_w * s;
            v->draw_params.h = src_h * s;
            v->draw_params.x = v->anchor_x + v->box_w * 0.5f;
            v->draw_params.y = v->anchor_y + v->box_h * 0.5f;
            v->draw_params.anchor_h = GFX_HALIGN_CENTER;
            v->draw_params.anchor_v = GFX_VALIGN_CENTER;
            v->draw_params.origin_h = GFX_HALIGN_CENTER;
            v->draw_params.origin_v = GFX_VALIGN_CENTER;
            break;
        }

        case INTRO_VISUAL_STRETCH:
        default:
            v->draw_params.w = v->box_w;
            v->draw_params.h = v->box_h;
            v->draw_params.x = v->anchor_x;
            v->draw_params.y = v->anchor_y;
            v->draw_params.anchor_h = GFX_HALIGN_LEFT;
            v->draw_params.anchor_v = GFX_VALIGN_TOP;
            v->draw_params.origin_h = GFX_HALIGN_LEFT;
            v->draw_params.origin_v = GFX_VALIGN_TOP;
            break;
    }

    v->draw_params.scale_x = v->scale;
    v->draw_params.scale_y = v->scale;
    v->draw_params.x += v->pan_x;
    v->draw_params.y += v->pan_y;
}

static int try_create_visual(intro_visual_t *v, const char *tag)
{
    texture_status_t st;

    if (!v || v->sprite_created)
        return 0;

    if (!texture_is_valid(v->texture))
        return -1;

    st = texture_status(v->texture);
    if (st == TEXTURE_STATUS_FAILED) {
        LOGLNC(LOGCAT_RESOURCES,
              "[state:intro_test] texture failed tag=%s path=%s",
              tag ? tag : "?",
              v->path ? v->path : "?");
        return -1;
    }

    if (st != TEXTURE_STATUS_READY)
        return 1;

    if (texture_get_gfx_handle(v->texture, &v->gfx_texture) < 0) {
        LOGLNC(LOGCAT_RESOURCES,
              "[state:intro_test] ready but no gfx texture tag=%s",
              tag ? tag : "?");
        return -1;
    }

    if (gfx_texture_get_size(v->gfx_texture, &v->tex_w, &v->tex_h) < 0) {
        LOGLNC(LOGCAT_RESOURCES,
            "[state:intro_test] failed to get texture size tag=%s",
            tag ? tag : "?");
        v->tex_w = 0;
        v->tex_h = 0;
    }

    intro_test_resolve_visual_rect(v);

    if (!v->prewarmed) {
        int warm = texture_touch(v->texture);
        LOGLNC(LOGCAT_RESOURCES,
              "[state:intro_test] touch tag=%s result=%d",
              tag ? tag : "?",
              warm);
        v->prewarmed = 1;
    }

    if (gfx_sprite_create(v->gfx_texture, &v->draw_params, &v->sprite_id) < 0) {
        LOGLNC(LOGCAT_GFX,
              "[state:intro_test] failed to create sprite tag=%s",
              tag ? tag : "?");
        return -1;
    }

    v->sprite_created = 1;

    LOGLNC(LOGCAT_GFX,
          "[state:intro_test] sprite ready tag=%s sprite_id=%d",
          tag ? tag : "?",
          v->sprite_id);
    return 0;
}

static void init_text_block_menu(text_block_t *block) {
    text_style_t style;
    text_style_init(&style);
    style.layer = 100;
    style.color = text_color_rgba(0xFF, 0xFF, 0xFF, 0xFF);

    text_block_set_box(block, 32, 32, 200, 300);
    text_block_set_align_h(block, TEXT_ALIGN_LEFT);
    text_block_set_align_v(block, TEXT_ALIGN_TOP);

    text_block_set_style(block, &style);
    text_block_set_wrap_mode(block, TEXT_WRAP_WORD);
    text_block_set_reveal_mode(block, TEXT_REVEAL_NONE);
}

static void init_text_block_intro(text_block_t *block, int screen_width, int screen_height) {
    text_style_t style;
    text_style_init(&style);
    style.layer = 100;
    style.color = text_color_rgba(0xFF, 0xFF, 0xFF, 0xFF);

    text_block_set_box(block, (screen_width - 410) * 0.5f, (screen_height - 120) * 0.5f + 120, 410, 120);
    text_block_set_align_h(block, TEXT_ALIGN_LEFT);
    text_block_set_align_v(block, TEXT_ALIGN_TOP);

    text_block_set_style(block, &style);
    text_block_set_wrap_mode(block, TEXT_WRAP_WORD);
    text_block_set_reveal_mode(block, TEXT_REVEAL_GLYPH);
}

static void intro_test_apply_menu_text(intro_test_state_data_t *data)
{
    const char *line0;
    const char *line1;
    const char *line2;

    line0 = (data->option == EN) ? "[color=#FFFF00]English[/color]" : "English";
    line1 = (data->option == RU) ? "[color=#FFFF00]Русский[/color]" : "Русский";
    line2 = (data->option == EXIT) ? "[color=#FFFF00]Exit[/color]" : "Exit";

    snprintf(data->menu_markup,
             sizeof(data->menu_markup),
             "%s\n%s\n%s",
             line0,
             line1,
             line2);

    text_block_set_text(&data->block, data->menu_markup);
    text_block_refresh(&data->block);
}

static int intro_test_build_scene_for_slide(intro_test_state_data_t *data,
                                            int slide_index)
{
    const intro_slide_t *slide;
    int i;
    int count;

    if (!data)
        return -1;

    if (!data->slides || data->slide_count <= 0)
        return -1;

    if (slide_index < 0 || slide_index >= data->slide_count)
        return -1;

    slide = &data->slides[slide_index];

    intro_test_release_intro_slots(data);
    intro_test_clear_intro_slots(data);

    if (!slide->slides || slide->slide_count <= 0)
        return 0;

    count = slide->slide_count;
    if (count > INTRO_MAX_SLOTS)
        count = INTRO_MAX_SLOTS;

    for (i = 0; i < count; ++i) {
        const intro_subslide_desc_t *sub = &slide->slides[i];

        if (intro_test_request_slot_visual(&data->intro_slots[i],
                                           sub->path,
                                           (data->screen_width - INTRO_SLIDE_W) * 0.5f,
                                           36.0f,
                                           INTRO_SLIDE_W,
                                           INTRO_SLIDE_H,
                                           10 + i,
                                           sub->mode) != 0)
            return -1;
    }

    return 0;
}

static void intro_test_begin_logo(intro_test_state_data_t *data)
{
    float cx = data->screen_width * 0.5f;
    float cy = data->screen_height * 0.5f;
    float main_x = cx - LOGO_W * 0.5f;
    float main_y = cy - LOGO_H - (LOGO_GAP * 0.5f);
    float sub_x  = cx - LOGO_YELLOW_W * 0.5f;
    float sub_y  = cy + (LOGO_GAP * 0.5f);

    data->stage = LOGO;
    data->stage_time = 0.0f;
    data->logo_time = 0.0f;
    data->slide_time = 0.0f;

    reset_visual(&data->logo_main);
    data->logo_main.mode = INTRO_VISUAL_NATIVE;
    data->logo_main.anchor_x = main_x;
    data->logo_main.anchor_y = main_y;
    data->logo_main.box_w = LOGO_W;
    data->logo_main.box_h = LOGO_H;

    reset_visual(&data->logo_sub);
    data->logo_sub.mode = INTRO_VISUAL_NATIVE;
    data->logo_sub.anchor_x = sub_x;
    data->logo_sub.anchor_y = sub_y;
    data->logo_sub.box_w = LOGO_YELLOW_W;
    data->logo_sub.box_h = LOGO_YELLOW_H;

    intro_test_release_intro_slots(data);
    intro_test_clear_intro_slots(data);

    data->logo_main.draw_params = gfx_draw_params_default(main_x, main_y, LOGO_W, LOGO_H);
    data->logo_main.draw_params.layer = 20;
    data->logo_main.draw_params.origin_v = GFX_VALIGN_TOP;
    data->logo_main.draw_params.origin_h = GFX_HALIGN_LEFT;

    data->logo_sub.draw_params = gfx_draw_params_default(sub_x, sub_y, LOGO_YELLOW_W, LOGO_YELLOW_H);
    data->logo_sub.draw_params.layer = 20;
    data->logo_sub.draw_params.origin_v = GFX_VALIGN_TOP;
    data->logo_sub.draw_params.origin_h = GFX_HALIGN_LEFT;

    intro_test_request_visual(&data->logo_main, logo);
    intro_test_request_visual(&data->logo_sub, logo_yellow);

    LOGLNC(LOGCAT_STATE, "[state:intro_test] begin logo");
}

static void intro_test_start_slide(game_app_t *app,
                                   intro_test_state_data_t *data,
                                   int slide_index)
{
    const intro_slide_t *slide;

    (void)app;

    if (!data)
        return;

    if (slide_index < 0 || slide_index >= data->slide_count)
        return;

    if (intro_test_build_scene_for_slide(data, slide_index) != 0)
        return;

    slide = &data->slides[slide_index];

    data->stage = INTRO;
    data->stage_time = 0.0f;
    data->slide_time = 0.0f;
    data->current_slide = slide_index;
    data->current_subslide = 0;
    data->current_text_line = 0;
    data->fade_alpha = 0.0f;

    if (slide->string_count > 0 && slide->strings)
        intro_test_set_text(data, slide->strings[0]);
    else
        intro_test_set_text(data, "");

    LOGLNC(LOGCAT_STATE, "[state:intro_test] start slide=%d name=%s",
           slide_index,
           slide->name ? slide->name : "?");
}

static void intro_test_begin_intro(game_app_t *app, intro_test_state_data_t *data)
{
    if (!data)
        return;

    init_text_block_intro(&data->block, data->screen_width, data->screen_height);
    text_block_set_text(&data->block, "");
    text_block_refresh(&data->block);
    
    switch (data->option) {
        case EN:
            data->slides = slides;
            data->slide_count = ARRAY_COUNT(slides);
            LOGLNC(LOGCAT_STATE, "[state:intro_test] language selected: EN slides=%d",
                data->slide_count);
            break;
        case RU:
            data->slides = slides_ru;
            data->slide_count = ARRAY_COUNT(slides_ru);
            LOGLNC(LOGCAT_STATE, "[state:intro_test] language selected: RU slides=%d",
                data->slide_count);
            break;
        default:
            return;
    }
    data->current_slide = 0;
    intro_test_begin_logo(data);
}

static void intro_test_update_menu(game_app_t *app, intro_test_state_data_t *data)
{
    int count;

    if (!data)
        return;

    count = ARRAY_COUNT(menu_options);

    if (input_button_pressed(INPUT_BUTTON_UP)) {
        data->option = (state_option_t)((data->option + count - 1) % count);
        audio_play_ex(data->snd_menu_select.asset, 100, 1.0f, 0, NULL, NULL, NULL);
        input_consume();
    } else if (input_button_pressed(INPUT_BUTTON_DOWN)) {
        data->option = (state_option_t)((data->option + 1) % count);
        audio_play_ex(data->snd_menu_select.asset, 100, 1.0f, 0, NULL, NULL, NULL);
        input_consume();
    } else if (input_button_pressed(INPUT_BUTTON_CROSS)) {
        audio_play_ex(data->snd_confirm.asset, 100, 1.0f, 0, NULL, NULL, NULL);
        if (data->option != EXIT) {
            intro_test_begin_intro(app, data);
        } else {
            LOGLNC(LOGCAT_STATE, "[state:intro_test] language menu exit");
            game_app_request_state_change(debug_menu_state_desc(), NULL);
        }
        input_consume();
    }

    if (data->stage == MENU_LANG_SELECT){
        intro_test_apply_menu_text(data);
    }
}

static int intro_test_reset(game_app_t *app, intro_test_state_data_t *data)
{
    text_font_resource_desc_t font_desc;

    if (!data)
        return -1;

    data->screen_width = renderer_get_screen_width();
    data->screen_height = renderer_get_screen_height();

    data->stage = MENU_LANG_SELECT;
    data->option = EN;
    data->current_slide = 0;
    data->current_subslide = 0;
    data->current_text_line = -1;
    data->visual_dirty = 0;
    data->text_dirty = 0;

    data->stage_time = 0.0f;
    data->logo_time = 0.0f;
    data->slide_time = 0.0f;

    data->slides = slides;
    data->slide_count = ARRAY_COUNT(slides);

    data->mus_intro_voice = -1;

    data->mus_intro.asset = -1;
    data->mus_intro.ok = 0;

    data->mus_intronoise.asset = -1;
    data->mus_intronoise.ok = 0;

    data->snd_confirm.asset = -1;
    data->snd_confirm.ok = 0;

    data->snd_menu_select.asset = -1;
    data->snd_menu_select.ok = 0;

    data->sndfnt.asset = -1;
    data->sndfnt.ok = 0;

    data->rich_items = (text_layout_item_t *)mem_arena_calloc(
        game_app_state_arena(app),
        TEXT_GLYPH_CAPACITY,
        sizeof(text_layout_item_t),
        16
    );
    if (!data->rich_items) {
        LOGLNC(LOGCAT_TEXT, "[state:intro_test] failed to allocate rich layout item buffer");
        return -1;
    }

    data->lines = (text_layout_line_t *)mem_arena_calloc(
        game_app_state_arena(app),
        TEXT_LINE_CAPACITY,
        sizeof(text_layout_line_t),
        16
    );
    if (!data->lines) {
        LOGLNC(LOGCAT_TEXT, "[state:intro_test] failed to allocate line layout buffer");
        return -1;
    }

    font_desc.fnt_path = FONT_DESC_PATH;
    font_desc.atlas_path = FONT_ATLAS_PATH;

    if (text_font_resource_request(app, &data->font_res, &font_desc) != 0) {
        LOGLNC(LOGCAT_TEXT, "[state:intro_test] failed to request font resource");
        return -1;
    }

    text_block_init(&data->block,
        data->rich_runs,
        TEXT_RICH_RUN_CAPACITY,
        data->rich_items,
        TEXT_GLYPH_CAPACITY,
        data->lines,
        TEXT_LINE_CAPACITY,
        TEXT_REVEAL_SECONDS_PER_GLYPH);

    init_text_block_menu(&data->block);

    reset_visual(&data->logo_main);
    reset_visual(&data->logo_sub);
    intro_test_clear_intro_slots(data);

    data->fade_alpha = 0.0f;

    if (!audio_is_available()) {
        LOGLNC(LOGCAT_AUDIO, "[state:intro_test] audio unavailable");
        return -1;
    }
    
    if (open_music(&data->mus_intro, mus_intro) != 0)
        return -1;

    if (load_sfx(&data->mus_intronoise, intronoise) != 0)
        return -1;

    if (load_sfx(&data->snd_confirm, snd_confirm) != 0)
        return -1;

    if (load_sfx(&data->snd_menu_select, snd_mainmenu_select) != 0)
        return -1;

    if (load_sfx(&data->sndfnt, sndfnt_default2) != 0)
        return -1;

    return 0;
}

static int intro_test_enter(game_app_t *app, void *userdata)
{
    intro_test_state_data_t *data;

    (void)userdata;

    data = (intro_test_state_data_t *)mem_arena_calloc(
        game_app_state_arena(app),
        1,
        sizeof(*data),
        16
    );

    if (!data) {
        LOGLNC(LOGCAT_STATE, "[state:intro_test] enter failed: no state arena memory");
        return -1;
    }

    LOGLNC(LOGCAT_STATE, "[state:intro_test] enter");

    game_app_set_state_userdata(app, data);

    if (intro_test_reset(app, data) != 0) {
        LOGLNC(LOGCAT_STATE, "[state:intro_test] enter failed: failed to reset data");
        return -1;
    }

    return 0;
}

static void intro_test_exit(game_app_t *app)
{
    intro_test_state_data_t *data = intro_test_data(app);

    if (!data) {
        LOGLNC(LOGCAT_STATE, "[state:intro_test] exit");
        return;
    }
    
    if (data->mus_intro_voice >= 0)
        audio_voice_stop(data->mus_intro_voice);

    if (data->mus_intro.ok && data->mus_intro.asset >= 0)
        audio_asset_unload(data->mus_intro.asset);

    if (data->mus_intronoise.ok && data->mus_intronoise.asset >= 0)
        audio_asset_unload(data->mus_intronoise.asset);

    if (data->snd_confirm.ok && data->snd_confirm.asset >= 0)
        audio_asset_unload(data->snd_confirm.asset);

    if (data->snd_menu_select.ok && data->snd_menu_select.asset >= 0)
        audio_asset_unload(data->snd_menu_select.asset);

    if (data->sndfnt.ok && data->sndfnt.asset >= 0)
        audio_asset_unload(data->sndfnt.asset);

    intro_test_release_visual(&data->logo_main);
    intro_test_release_visual(&data->logo_sub);
    intro_test_release_intro_slots(data);

    text_font_resource_shutdown(app, &data->font_res);
    game_app_set_state_userdata(app, NULL);

    LOGLNC(LOGCAT_STATE, "[state:intro_test] exit");
}

static void intro_test_fixed_update(game_app_t *app, float dt)
{
    (void)app;
    (void)dt;
}

static void intro_test_update_logo(game_app_t *app, intro_test_state_data_t *data, float dt)
{
    int logo_ready;

    data->stage_time += dt;
    data->logo_time += dt;

    try_create_visual(&data->logo_main, "logo");
    try_create_visual(&data->logo_sub, "logo_yellow");

    if (data->logo_main.sprite_created)
        gfx_sprite_update(data->logo_main.sprite_id, &data->logo_main.draw_params);

    if (data->logo_sub.sprite_created)
        gfx_sprite_update(data->logo_sub.sprite_id, &data->logo_sub.draw_params);

    logo_ready = data->logo_main.sprite_created && data->logo_sub.sprite_created;

    if (logo_ready && data->logo_time >= INTRO_LOGO_MIN_SECONDS) {
        intro_test_release_visual(&data->logo_main);
        intro_test_release_visual(&data->logo_sub);
        intro_test_start_slide(app, data, 0);
    }
}

static void intro_test_apply_visual_alpha(intro_visual_t *v)
{
    if (!v || !v->sprite_created)
        return;

    v->draw_params.color.r = 0xFF;
    v->draw_params.color.g = 0xFF;
    v->draw_params.color.b = 0xFF;
    v->draw_params.color.a = intro_test_alpha_to_u8(v->alpha);
}

static void intro_test_update_slot_group(intro_visual_t *slots, int count, const char *tag)
{
    int i;

    for (i = 0; i < count; ++i) {
        if (!slots[i].requested)
            continue;

        try_create_visual(&slots[i], tag);

        if (slots[i].sprite_created) {
            intro_test_resolve_visual_rect(&slots[i]);
            intro_test_apply_visual_alpha(&slots[i]);
            gfx_sprite_update(slots[i].sprite_id, &slots[i].draw_params);
            intro_test_apply_visual_region(&slots[i]);
        }
    }
}

static intro_action_result_t intro_test_default_slide_action(game_app_t *app,
                                                             intro_test_state_data_t *data,
                                                             float dt)
{
    const intro_slide_t *slide;
    float duration;

    (void)app;
    (void)dt;

    slide = intro_test_slide_desc(data);
    if (!slide)
        return INTRO_ACTION_EXIT_INTRO;

    duration = slide->showing_time_sec;
    if (duration <= 0.0f)
        duration = INTRO_SLIDE_DEFAULT_SECONDS;

    data->fade_alpha = 0.0f;

    if (data->slide_time >= duration)
        return INTRO_ACTION_NEXT_SLIDE;

    return INTRO_ACTION_CONTINUE;
}

static void intro_test_update_intro(game_app_t *app,
                                    intro_test_state_data_t *data,
                                    float dt)
{
    const intro_slide_t *slide;
    intro_action_result_t action_rc;

    if (!data)
        return;

    data->stage_time += dt;
    data->slide_time += dt;

    slide = intro_test_slide_desc(data);
    if (!slide)
        return;

    if (slide->action)
        action_rc = slide->action(app, data, dt);
    else
        action_rc = intro_test_default_slide_action(app, data, dt);

    intro_test_update_slot_group(data->intro_slots, INTRO_MAX_SLOTS, "intro");

    switch (action_rc) {
        case INTRO_ACTION_CONTINUE:
            break;

        case INTRO_ACTION_NEXT_SLIDE:
            if (data->current_slide + 1 < data->slide_count)
                intro_test_start_slide(app, data, data->current_slide + 1);
            else
                game_app_request_state_change(debug_menu_state_desc(), NULL);
            break;

        case INTRO_ACTION_EXIT_INTRO:
            game_app_request_state_change(debug_menu_state_desc(), NULL);
            break;

        default:
            break;
    }
}

static void intro_test_update(game_app_t *app, float dt)
{
    intro_test_state_data_t *data = intro_test_data(app);

    if (input_button_pressed(INPUT_BUTTON_START)) {
        LOGLNC(LOGCAT_STATE, "[state:intro_test] START pressed, return to menu");
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
    }

    switch (data->stage) {
        case MENU_LANG_SELECT:
            intro_test_update_menu(app, data);
            break;

        case LOGO:
            intro_test_update_logo(app, data, dt);
            break;

        case INTRO:
            intro_test_update_intro(app, data, dt);
            break;

        default:
            break;
    }

    text_block_update(&data->block, dt);
}

static void intro_test_draw(game_app_t *app, float alpha)
{
    intro_test_state_data_t *data = intro_test_data(app);

    (void)alpha;

    if (!data)
        return;

    gfx_sprite_draw_all();
    text_block_draw(&data->block);
}

static const game_state_desc_t s_intro_test_state = {
    "intro_test",
    intro_test_enter,
    intro_test_exit,
    intro_test_fixed_update,
    intro_test_update,
    intro_test_draw
};

const game_state_desc_t *intro_test_state_desc(void)
{
    return &s_intro_test_state;
}