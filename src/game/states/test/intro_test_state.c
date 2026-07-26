#include "engine/logging/log.h"
#include "engine/memory/memory_arena.h"
#include "engine/gfx/draw2d.h"
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

#ifndef TEXT_BOX_X
#define TEXT_BOX_X 32
#endif

#ifndef TEXT_BOX_Y
#define TEXT_BOX_Y 32
#endif

#ifndef TEXT_BOX_W
#define TEXT_BOX_W 400
#endif

#ifndef TEXT_BOX_H
#define TEXT_BOX_H 400
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
#define TEXT_REVEAL_SECONDS_PER_GLYPH 0.03f
#endif

#ifndef INTRO_SCREEN_W
#define INTRO_SCREEN_W 640.0f
#endif

#ifndef INTRO_SCREEN_H
#define INTRO_SCREEN_H 448.0f
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

#define ARRAY_COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

typedef struct intro_test_state_data intro_test_state_data_t;

static text_color_t text_color_white(void)
{
    return text_color_rgba(0xFF, 0xFF, 0xFF, 0xFF);
}

typedef enum state_stage {
    MENU_LANG_SELECT = 0,
    LOGO,
    INTRO
} state_stage_t;

typedef enum state_option {
    EN = 0,
    RU,
    EXIT
} state_option_t;

typedef struct intro_state_menu_option {
    const char *name;
    const char *text_utf8;
    state_option_t option_tag;
} intro_state_menu_option_t;

static const intro_state_menu_option_t menu_options[] = {
    { "en",   "English", EN },
    { "ru",   "Русский", RU },
    { "exit", "Exit",    EXIT }
};

typedef struct intro_slide {
    const char *name;
    const char **slides;
    int slide_count;
    const char **strings;
    int string_count;
    float showing_time_ms;
    void (*action)(game_app_t *app, intro_test_state_data_t *data, float dt);
} intro_slide_t;

static const intro_slide_t slides[] = {
    {
        "intro_slide_1",
        intro1_slides,
        ARRAY_COUNT(intro1_slides),
        intro1_text,
        ARRAY_COUNT(intro1_text),
        0.0f,
        NULL,
    },
    {
        "intro_slide_2",
        intro2_slides,
        ARRAY_COUNT(intro2_slides),
        intro2_text,
        ARRAY_COUNT(intro2_text),
        0.0f,
        NULL
    },
    {
        "intro_slide_3",
        intro3_slides,
        ARRAY_COUNT(intro3_slides),
        intro3_text,
        ARRAY_COUNT(intro3_text),
        0.0f,
        NULL
    },
    {
        "intro_slide_4",
        intro4_slides,
        ARRAY_COUNT(intro4_slides),
        intro4_text,
        ARRAY_COUNT(intro4_text),
        0.0f,
        NULL
    },
    {
        "intro_slide_5",
        intro5_slides,
        ARRAY_COUNT(intro5_slides),
        intro5_text,
        ARRAY_COUNT(intro5_text),
        0.0f,
        NULL
    },
    {
        "intro_slide_6",
        intro6_slides,
        ARRAY_COUNT(intro6_slides),
        intro6_text,
        ARRAY_COUNT(intro6_text),
        0.0f,
        NULL
    },
    {
        "intro_slide_7",
        intro7_slides,
        ARRAY_COUNT(intro7_slides),
        NULL,
        0,
        0.0f,
        NULL
    },
    {
        "intro_slide_8",
        intro8_slides,
        ARRAY_COUNT(intro8_slides),
        NULL,
        0,
        0.0f,
        NULL
    },
    {
        "intro_slide_9",
        intro9_slides,
        ARRAY_COUNT(intro9_slides),
        NULL,
        0,
        0.0f,
        NULL
    },
    {
        "intro_slide_10",
        intro10_slides,
        ARRAY_COUNT(intro10_slides),
        NULL,
        0,
        0.0f,
        NULL
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
        NULL,
    },
    {
        "intro_slide_2",
        intro2_slides_ru,
        ARRAY_COUNT(intro2_slides_ru),
        intro2_text_ru,
        ARRAY_COUNT(intro2_text_ru),
        0.0f,
        NULL
    },
    {
        "intro_slide_3",
        intro3_slides_ru,
        ARRAY_COUNT(intro3_slides_ru),
        intro3_text_ru,
        ARRAY_COUNT(intro3_text_ru),
        0.0f,
        NULL
    },
    {
        "intro_slide_4",
        intro4_slides_ru,
        ARRAY_COUNT(intro4_slides_ru),
        intro4_text_ru,
        ARRAY_COUNT(intro4_text_ru),
        0.0f,
        NULL
    },
    {
        "intro_slide_5",
        intro5_slides_ru,
        ARRAY_COUNT(intro5_slides_ru),
        intro5_text_ru,
        ARRAY_COUNT(intro5_text_ru),
        0.0f,
        NULL
    },
    {
        "intro_slide_6",
        intro6_slides_ru,
        ARRAY_COUNT(intro6_slides_ru),
        intro6_text_ru,
        ARRAY_COUNT(intro6_text_ru),
        0.0f,
        NULL
    },
    {
        "intro_slide_7",
        intro7_slides_ru,
        ARRAY_COUNT(intro7_slides_ru),
        NULL,
        0,
        0.0f,
        NULL
    },
    {
        "intro_slide_8",
        intro8_slides_ru,
        ARRAY_COUNT(intro8_slides_ru),
        NULL,
        0,
        0.0f,
        NULL
    },
    {
        "intro_slide_9",
        intro9_slides_ru,
        ARRAY_COUNT(intro9_slides_ru),
        NULL,
        0,
        0.0f,
        NULL
    },
    {
        "intro_slide_10",
        intro10_slides_ru,
        ARRAY_COUNT(intro10_slides_ru),
        NULL,
        0,
        0.0f,
        NULL
    },
};

typedef struct audio_descriptor {
    int asset;
    int ok;
} audio_descriptor_t;

static texture_handle_t intro_test_invalid_texture(void)
{
    texture_handle_t h;
    h.index = 0xffffu;
    h.generation = 0;
    return h;
}

typedef struct intro_visual {
    const char *path;
    texture_handle_t texture;
    gfx_texture_handle_t gfx_texture;
    gfx_sprite_id_t sprite_id;
    int requested;
    int prewarmed;
    int sprite_created;
    gfx_draw_params_t draw_params;
} intro_visual_t;

typedef struct intro_test_state_data {
    state_stage_t stage;
    state_option_t option;
    int current_slide;

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

    int font_bound_logged;
    char menu_markup[256];

    intro_visual_t logo_main;
    intro_visual_t logo_sub;
    intro_visual_t intro_current;
    float stage_timer;
} intro_test_state_data_t;

static intro_test_state_data_t *intro_test_data(game_app_t *app)
{
    return GAME_APP_STATE_DATA_AS(app, intro_test_state_data_t);
}

static int intro_test_menu_option_count(void)
{
    return ARRAY_COUNT(menu_options);
}

static int intro_test_slide_table_count(const intro_test_state_data_t *data)
{
    return data ? data->slide_count : 0;
}

static const intro_slide_t *intro_test_current_slide_desc(const intro_test_state_data_t *data)
{
    if (!data || !data->slides || data->slide_count <= 0)
        return NULL;

    if (data->current_slide < 0 || data->current_slide >= data->slide_count)
        return NULL;

    return &data->slides[data->current_slide];
}

static const char *intro_test_slide_primary_path(const intro_slide_t *slide)
{
    if (!slide || !slide->slides || slide->slide_count <= 0)
        return NULL;

    return slide->slides[0];
}

static void intro_test_reset_visual(intro_visual_t *v)
{
    if (!v)
        return;

    v->path = NULL;
    v->texture = intro_test_invalid_texture();
    v->gfx_texture = gfx_texture_invalid();
    v->sprite_id = -1;
    v->requested = 0;
    v->prewarmed = 0;
    v->sprite_created = 0;
    v->draw_params = gfx_draw_params_default(0.0f, 0.0f, 0.0f, 0.0f);
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

static int intro_test_request_current_slide_visual(intro_test_state_data_t *data)
{
    const intro_slide_t *slide;
    const char *path;
    float cx = INTRO_SCREEN_W * 0.5f;

    if (!data)
        return -1;

    slide = intro_test_current_slide_desc(data);
    if (!slide)
        return -1;

    path = intro_test_slide_primary_path(slide);
    if (!path)
        return -1;

    if (!data->intro_current.requested) {
        data->intro_current.draw_params =
            gfx_draw_params_default(
                cx - INTRO_SLIDE_W * 0.5f,
                36.0f,
                INTRO_SLIDE_W,
                INTRO_SLIDE_H);
        data->intro_current.draw_params.origin_v = GFX_VALIGN_TOP;
        data->intro_current.draw_params.origin_h = GFX_HALIGN_LEFT;
        data->intro_current.draw_params.layer = 0;
    }

    return intro_test_request_visual(&data->intro_current, path);
}

static int intro_test_try_create_visual(intro_visual_t *v, const char *tag)
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

static void intro_test_release_visual(intro_visual_t *v)
{
    if (!v)
        return;

    if (v->sprite_created)
        gfx_sprite_remove(v->sprite_id);

    if (texture_is_valid(v->texture))
        texture_release(v->texture);

    intro_test_reset_visual(v);
}

static void intro_test_apply_menu_text(intro_test_state_data_t *data)
{
    text_style_t style;
    const char *line0;
    const char *line1;
    const char *line2;

    if (!data)
        return;

    line0 = (data->option == EN)
        ? "[color=#FFFF00]English[/color]"
        : "English";

    line1 = (data->option == RU)
        ? "[color=#FFFF00]Русский[/color]"
        : "Русский";

    line2 = (data->option == EXIT)
        ? "[color=#FFFF00]Exit[/color]"
        : "Exit";

    snprintf(data->menu_markup,
             sizeof(data->menu_markup),
             "%s\n%s\n%s",
             line0,
             line1,
             line2);

    text_style_init(&style);
    style.layer = 100;
    style.color = text_color_white();

    text_block_set_style(&data->block, &style);
    text_block_set_wrap_mode(&data->block, TEXT_WRAP_WORD);
    text_block_set_reveal_mode(&data->block, TEXT_REVEAL_NONE);
    text_block_set_text(&data->block, data->menu_markup);
    text_block_refresh(&data->block);
}

static int open_music(audio_descriptor_t *descriptor, const char *path)
{
    if (!descriptor)
        return -1;

    descriptor->asset = audio_asset_load_stream(
        path,
        AUDIO_BUFFER_SIZE
    );

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

static int intro_test_trigger_sfx(const audio_descriptor_t *descriptor)
{
    int voice;

    if (!descriptor || !descriptor->ok || descriptor->asset < 0) {
        LOGLNC(LOGCAT_AUDIO, "[state:intro_test] unavailable");
        return -1;
    }

    voice = audio_play_ex(descriptor->asset,
                          100,
                          1.0f,
                          0,
                          NULL,
                          NULL,
                          NULL);

    if (voice < 0) {
        LOGLNC(LOGCAT_AUDIO, "[state:intro_test] play failed rc=%d", voice);
        return -1;
    }

    LOGLNC(LOGCAT_AUDIO, "[state:intro_test] voice=%d", voice);
    return voice;
}

static void intro_test_begin_logo(game_app_t *app, intro_test_state_data_t *data)
{
    float cx = INTRO_SCREEN_W * 0.5f;
    float cy = INTRO_SCREEN_H * 0.5f;

    (void)app;

    if (!data)
        return;

    data->stage = LOGO;
    data->stage_timer = 0.0f;

    intro_test_reset_visual(&data->logo_main);
    intro_test_reset_visual(&data->logo_sub);
    intro_test_reset_visual(&data->intro_current);

    data->logo_main.draw_params =
        gfx_draw_params_default(
            cx - LOGO_W * 0.5f,
            cy - LOGO_H - (LOGO_GAP * 0.5f),
            LOGO_W,
            LOGO_H
        );
    data->logo_main.draw_params.layer = 20;
    data->logo_main.draw_params.origin_v = GFX_VALIGN_TOP;
    data->logo_main.draw_params.origin_h = GFX_HALIGN_LEFT;

    data->logo_sub.draw_params =
        gfx_draw_params_default(
            cx - LOGO_YELLOW_W * 0.5f,
            cy + (LOGO_GAP * 0.5f),
            LOGO_YELLOW_W,
            LOGO_YELLOW_H
        );
    data->logo_sub.draw_params.layer = 19;
    data->logo_sub.draw_params.origin_v = GFX_VALIGN_TOP;
    data->logo_sub.draw_params.origin_h = GFX_HALIGN_LEFT;

    intro_test_request_visual(&data->logo_main, logo);
    intro_test_request_visual(&data->logo_sub, logo_yellow);
    intro_test_reset_visual(&data->intro_current);
    intro_test_request_current_slide_visual(data);
    data->stage_timer = 0.0f;

    LOGLNC(LOGCAT_STATE, "[state:intro_test] begin logo");
}

static int intro_test_visual_texture_ready(const intro_visual_t *v)
{
    if (!v)
        return 0;

    if (!v->requested || !texture_is_valid(v->texture))
        return 0;

    return texture_status(v->texture) == TEXTURE_STATUS_READY;
}

static int intro_test_activate_current_slide(intro_test_state_data_t *data)
{
    if (!data)
        return -1;

    if (!intro_test_visual_texture_ready(&data->intro_current))
        return -1;

    if (data->intro_current.sprite_created)
        return 0;

    return intro_test_try_create_visual(&data->intro_current, "intro_current");
}

static int intro_test_reset(game_app_t *app, intro_test_state_data_t *data)
{
    text_font_resource_desc_t font_desc;

    if (!data)
        return -1;

    data->stage = MENU_LANG_SELECT;
    data->option = EN;
    data->current_slide = 0;
    data->slides = slides;
    data->slide_count = ARRAY_COUNT(slides);
    data->font_bound_logged = 0;
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

    text_block_set_box(&data->block,
        TEXT_BOX_X,
        TEXT_BOX_Y,
        TEXT_BOX_W,
        TEXT_BOX_H);

    text_block_set_align_h(&data->block, TEXT_ALIGN_LEFT);
    text_block_set_align_v(&data->block, TEXT_ALIGN_TOP);

    intro_test_reset_visual(&data->logo_main);
    intro_test_reset_visual(&data->logo_sub);
    intro_test_reset_visual(&data->intro_current);
    data->stage_timer = 0.0f;

    if (!audio_is_available()) {
        LOGLNC(LOGCAT_AUDIO, "[state:intro_test] audio unavailable");
        return -1;
    }
    
    if (open_music(&data->mus_intro, mus_intro) != 0) {
        return -1;
    }

    if (load_sfx(&data->mus_intronoise, intronoise) != 0) {
        return -1;
    }

    if (load_sfx(&data->snd_confirm, snd_confirm) != 0) {
        return -1;
    }

    if (load_sfx(&data->snd_menu_select, snd_mainmenu_select) != 0) {
        return -1;
    }

    if (load_sfx(&data->sndfnt, sndfnt_default2) != 0) {
        return -1;
    }

    return 0;
}

static void intro_test_select_language(game_app_t *app, intro_test_state_data_t *data)
{
    if (!data)
        return;

    switch (data->option) {
        case EN:
            data->slides = slides;
            data->slide_count = ARRAY_COUNT(slides);
            data->current_slide = 0;
            intro_test_begin_logo(app, data);
            LOGLNC(LOGCAT_STATE, "[state:intro_test] language selected: EN slides=%d",
                  data->slide_count);
            break;

        case RU:
            data->slides = slides_ru;
            data->slide_count = ARRAY_COUNT(slides_ru);
            data->current_slide = 0;
            intro_test_begin_logo(app, data);
            LOGLNC(LOGCAT_STATE, "[state:intro_test] language selected: RU slides=%d",
                  data->slide_count);
            break;

        case EXIT:
            LOGLNC(LOGCAT_STATE, "[state:intro_test] language menu exit");
            game_app_request_state_change(debug_menu_state_desc(), NULL);
            break;

        default:
            break;
    }

    (void)app;
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
    intro_test_release_visual(&data->intro_current);

    text_font_resource_shutdown(app, &data->font_res);
    game_app_set_state_userdata(app, NULL);

    LOGLNC(LOGCAT_STATE, "[state:intro_test] exit");
}

static void intro_test_fixed_update(game_app_t *app, float dt)
{
    (void)app;
    (void)dt;
}

static void intro_test_update_menu(game_app_t *app, intro_test_state_data_t *data)
{
    int count;

    if (!data)
        return;

    count = intro_test_menu_option_count();

    if (input_button_pressed(INPUT_BUTTON_UP)) {
        data->option = (state_option_t)((data->option + count - 1) % count);
        intro_test_apply_menu_text(data);
        intro_test_trigger_sfx(&data->snd_menu_select);
        input_consume();
    } else if (input_button_pressed(INPUT_BUTTON_DOWN)) {
        data->option = (state_option_t)((data->option + 1) % count);
        intro_test_apply_menu_text(data);
        intro_test_trigger_sfx(&data->snd_menu_select);
        input_consume();
    } else if (input_button_pressed(INPUT_BUTTON_CROSS)) {
        intro_test_trigger_sfx(&data->snd_confirm);
        intro_test_select_language(app, data);
        input_consume();
    }
}

static void intro_test_update_logo(game_app_t *app, intro_test_state_data_t *data, float dt)
{
    int intro_ready;

    (void)app;

    if (!data)
        return;

    data->stage_timer += dt;

    intro_test_try_create_visual(&data->logo_main, "logo");
    intro_test_try_create_visual(&data->logo_sub, "logo_yellow");

    if (data->logo_main.sprite_created)
        gfx_sprite_update(data->logo_main.sprite_id, &data->logo_main.draw_params);

    if (data->logo_sub.sprite_created)
        gfx_sprite_update(data->logo_sub.sprite_id, &data->logo_sub.draw_params);

    intro_ready = intro_test_visual_texture_ready(&data->intro_current);

    if (intro_ready && data->stage_timer >= INTRO_LOGO_MIN_SECONDS) {
        intro_test_release_visual(&data->logo_main);
        intro_test_release_visual(&data->logo_sub);

        if (intro_test_activate_current_slide(data) == 0) {
            data->stage = INTRO;
            LOGLNC(LOGCAT_STATE,
                  "[state:intro_test] logo done -> intro slide=%d",
                  data->current_slide);
        }
        return;
    }

    if (input_button_pressed(INPUT_BUTTON_CROSS) &&
        intro_ready &&
        data->stage_timer >= 0.2f) {
        intro_test_release_visual(&data->logo_main);
        intro_test_release_visual(&data->logo_sub);

        if (intro_test_activate_current_slide(data) == 0) {
            data->stage = INTRO;
            input_consume();
            LOGLNC(LOGCAT_STATE,
                  "[state:intro_test] logo skipped -> intro slide=%d",
                  data->current_slide);
        }
        return;
    }
}

static void intro_test_update_intro(game_app_t *app, intro_test_state_data_t *data, float dt)
{
    const intro_slide_t *slide;

    if (!data)
        return;

    slide = intro_test_current_slide_desc(data);

    if (!slide)
        return;

    if (data->intro_current.sprite_created)
        gfx_sprite_update(data->intro_current.sprite_id, &data->intro_current.draw_params);

    if (slide->action)
        slide->action(app, data, dt);
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

        if (!data->font_bound_logged) {
            LOGLNC(LOGCAT_TEXT,
                  "[state:intro_test] font bound slides=%d current_slide=%d box=(%d,%d,%d,%d)",
                  intro_test_slide_table_count(data),
                  data->current_slide,
                  (int)TEXT_BOX_X,
                  (int)TEXT_BOX_Y,
                  (int)TEXT_BOX_W,
                  (int)TEXT_BOX_H);
            data->font_bound_logged = 1;
        }
    }

    switch (data->stage) {
        case MENU_LANG_SELECT:
            intro_test_apply_menu_text(data);
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

static void intro_test_draw_menu(game_app_t *app, intro_test_state_data_t *data)
{
    (void)app;

    if (!data)
        return;

    text_block_draw(&data->block);
}

static void intro_test_draw_logo(game_app_t *app, intro_test_state_data_t *data, float alpha)
{
    (void)app;
    (void)data;
    (void)alpha;

    gfx_sprite_draw_all();
}

static void intro_test_draw_intro(game_app_t *app, intro_test_state_data_t *data, float alpha)
{
    (void)app;
    (void)alpha;

    gfx_sprite_draw_all();
    text_block_draw(&data->block);
}

static void intro_test_draw(game_app_t *app, float alpha)
{
    intro_test_state_data_t *data = intro_test_data(app);

    (void)alpha;

    if (!data)
        return;

    switch (data->stage) {
        case MENU_LANG_SELECT:
            intro_test_draw_menu(app, data);
            break;

        case LOGO:
            intro_test_draw_logo(app, data, alpha);
            break;

        case INTRO:
            intro_test_draw_intro(app, data, alpha);
            break;

        default:
            break;
    }
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