#include "engine/logging/log.h"
#include "engine/gfx/sprite.h"
#include "engine/gfx/sprite_anim.h"
#include "engine/resources/texture_assets.h"
#include "engine/input/input.h"
#include "engine/memory/memory_arena.h"

#include "game/states/test/debug_menu_state.h"
#include "game/states/test/sprite_animation_test_state.h"
#include "game/debug/debug_overlay.h"

#ifndef SPRITE_ANIMATION_PATH
#define SPRITE_ANIMATION_PATH "tv_jumpscare.png"
#endif

#ifndef SPRITE_ANIMATION_FRAME_COUNT
#define SPRITE_ANIMATION_FRAME_COUNT 6
#endif

#ifndef SPRITE_ANIMATION_FRAME_W
#define SPRITE_ANIMATION_FRAME_W 59.0f
#endif

#ifndef SPRITE_ANIMATION_FRAME_H
#define SPRITE_ANIMATION_FRAME_H 139.0f
#endif

#ifndef SPRITE_ANIMATION_FRAME_DURATION
#define SPRITE_ANIMATION_FRAME_DURATION 0.10f
#endif

typedef struct sprite_animation_state_data {
    texture_handle_t texture;
    gfx_texture_handle_t gfx_texture;

    gfx_sprite_id_t sprite_id;
    int sprite_created;
    int prewarmed;

    gfx_draw_params_t draw_params;

    gfx_sprite_anim_frame_t anim_frames[SPRITE_ANIMATION_FRAME_COUNT];
    gfx_sprite_anim_clip_t anim_clips[1];
    gfx_sprite_anim_asset_t anim_asset;
    gfx_sprite_anim_player_t anim_player;
    int anim_ready;

    debug_overlay_t overlay;
} sprite_animation_state_data_t;

static texture_handle_t sprite_animation_invalid_texture(void)
{
    texture_handle_t h;
    h.index = 0xffffu;
    h.generation = 0;
    return h;
}

static sprite_animation_state_data_t *sprite_animation_data(game_app_t *app)
{
    return GAME_APP_STATE_DATA_AS(app, sprite_animation_state_data_t);
}

static const char *sprite_animation_texture_status_name(texture_status_t st)
{
    switch (st) {
        case TEXTURE_STATUS_UNUSED:  return "UNUSED";
        case TEXTURE_STATUS_LOADING: return "LOADING";
        case TEXTURE_STATUS_READY:   return "READY";
        case TEXTURE_STATUS_FAILED:  return "FAILED";
        default:                     return "?";
    }
}

static void sprite_animation_rebuild_overlay(sprite_animation_state_data_t *data)
{
    texture_status_t st = TEXTURE_STATUS_FAILED;

    if (!data)
        return;

    if (texture_is_valid(data->texture))
        st = texture_status(data->texture);

    debug_overlay_printf(
        &data->overlay,
        "Sprite Animation Test\n"
        "START: return to menu\n"
        "\n"
        "texture  : %s\n"
        "sprite   : %d\n"
        "anim     : %s\n"
        "frame    : %d / %d\n"
        "sheet    : 354x139\n"
        "frame    : 59x139",
        sprite_animation_texture_status_name(st),
        data->sprite_created,
        data->anim_ready ? "READY" : "WAIT",
        (data->anim_player.current_clip >= 0)
            ? (data->anim_player.current_frame_in_clip + 1)
            : 0,
        SPRITE_ANIMATION_FRAME_COUNT
    );
}

static void sprite_animation_reset(sprite_animation_state_data_t *data)
{
    if (!data)
        return;

    data->texture = sprite_animation_invalid_texture();
    data->gfx_texture = gfx_texture_invalid();
    data->sprite_id = -1;
    data->sprite_created = 0;
    data->prewarmed = 0;
    data->anim_ready = 0;
    data->draw_params = gfx_draw_params_default(320.0f, 180.0f,
                                                SPRITE_ANIMATION_FRAME_W * 2.0f,
                                                SPRITE_ANIMATION_FRAME_H * 2.0f);

    gfx_sprite_anim_asset_init(&data->anim_asset,
                               gfx_texture_invalid(),
                               data->anim_frames,
                               SPRITE_ANIMATION_FRAME_COUNT,
                               data->anim_clips,
                               1);

    gfx_sprite_anim_player_init(&data->anim_player, NULL, -1);
}

static int sprite_animation_try_create(sprite_animation_state_data_t *data)
{
    texture_status_t st;
    gfx_rect_t first_rect;

    if (!data || data->sprite_created)
        return 0;

    if (!texture_is_valid(data->texture))
        return -1;

    st = texture_status(data->texture);
    if (st == TEXTURE_STATUS_FAILED) {
        LOGLNC(LOGCAT_RESOURCES, "[state:sprite_animation_test] texture failed path=%s",
              texture_path(data->texture));
        return -1;
    }

    if (st != TEXTURE_STATUS_READY)
        return 1;

    if (texture_get_gfx_handle(data->texture, &data->gfx_texture) < 0) {
        LOGLNC(LOGCAT_RESOURCES, "[state:sprite_animation_test] ready but no gfx texture");
        return -1;
    }

    if (!data->prewarmed) {
        int warm = texture_touch(data->texture);
        LOGLNC(LOGCAT_RESOURCES, "[state:sprite_animation_test] touch result=%d", warm);
        data->prewarmed = 1;
    }

    first_rect.x = 0.0f;
    first_rect.y = 0.0f;
    first_rect.w = SPRITE_ANIMATION_FRAME_W;
    first_rect.h = SPRITE_ANIMATION_FRAME_H;

    if (gfx_sprite_create_region(data->gfx_texture,
                                 &data->draw_params,
                                 &first_rect,
                                 &data->sprite_id) < 0) {
        LOGLNC(LOGCAT_GFX, "[state:sprite_animation_test] failed to create sprite");
        return -1;
    }

    data->sprite_created = 1;

    gfx_sprite_anim_asset_init(&data->anim_asset,
                               data->gfx_texture,
                               data->anim_frames,
                               SPRITE_ANIMATION_FRAME_COUNT,
                               data->anim_clips,
                               1);

    if (gfx_sprite_anim_asset_add_grid_strip(&data->anim_asset,
                                             0.0f,
                                             0.0f,
                                             SPRITE_ANIMATION_FRAME_W,
                                             SPRITE_ANIMATION_FRAME_H,
                                             SPRITE_ANIMATION_FRAME_COUNT,
                                             SPRITE_ANIMATION_FRAME_DURATION) < 0) {
        LOGLNC(LOGCAT_GFX, "[state:sprite_animation_test] failed to build frames");
        return -1;
    }

    if (gfx_sprite_anim_asset_add_clip(&data->anim_asset,
                                       "loop",
                                       0,
                                       SPRITE_ANIMATION_FRAME_COUNT,
                                       GFX_SPRITE_ANIM_PLAY_LOOP) < 0) {
        LOGLNC(LOGCAT_GFX, "[state:sprite_animation_test] failed to add clip");
        return -1;
    }

    gfx_sprite_anim_player_init(&data->anim_player,
                                &data->anim_asset,
                                data->sprite_id);

    if (gfx_sprite_anim_player_play(&data->anim_player, "loop", 1) < 0) {
        LOGLNC(LOGCAT_GFX, "[state:sprite_animation_test] failed to start animation");
        return -1;
    }

    data->anim_ready = 1;

    LOGLNC(LOGCAT_GFX, "[state:sprite_animation_test] sprite+anim ready sprite_id=%d",
          data->sprite_id);
    return 0;
}

static int sprite_animation_test_enter(game_app_t *app, void *userdata)
{
    sprite_animation_state_data_t *data;
    debug_overlay_desc_t overlay_desc;

    (void)userdata;

    data = (sprite_animation_state_data_t *)mem_arena_calloc(
        game_app_state_arena(app),
        1,
        sizeof(*data),
        16
    );
    if (!data) {
        LOGLNC(LOGCAT_STATE, "[state:sprite_animation_test] enter failed: no state arena memory");
        return -1;
    }

    game_app_set_state_userdata(app, data);

    sprite_animation_reset(data);

    debug_overlay_desc_init(&overlay_desc);
    overlay_desc.x = 16;
    overlay_desc.y = 16;
    overlay_desc.w = 620;
    overlay_desc.h = 120;

    if (debug_overlay_init(app, &data->overlay, &overlay_desc) != 0) {
        LOGLNC(LOGCAT_STATE, "[state:sprite_animation_test] overlay init failed");
        return -1;
    }

    data->texture = texture_load_png(SPRITE_ANIMATION_PATH, STREAM_PRIORITY_NORMAL);
    if (!texture_is_valid(data->texture)) {
        LOGLNC(LOGCAT_RESOURCES, "[state:sprite_animation_test] failed to request texture");
        return -1;
    }

    sprite_animation_rebuild_overlay(data);

    LOGLNC(LOGCAT_STATE, "[state:sprite_animation_test] enter requested texture");
    return 0;
}

static void sprite_animation_test_exit(game_app_t *app)
{
    sprite_animation_state_data_t *data = sprite_animation_data(app);

    if (!data) {
        LOGLNC(LOGCAT_STATE, "[state:sprite_animation_test] exit");
        return;
    }

    if (data->sprite_created)
        gfx_sprite_remove(data->sprite_id);

    if (texture_is_valid(data->texture))
        texture_release(data->texture);

    debug_overlay_shutdown(app, &data->overlay);

    game_app_set_state_userdata(app, NULL);

    LOGLNC(LOGCAT_STATE, "[state:sprite_animation_test] exit");
}

static void sprite_animation_test_fixed_update(game_app_t *app, float dt)
{
    (void)app;
    (void)dt;
}

static void sprite_animation_test_update(game_app_t *app, float dt)
{
    sprite_animation_state_data_t *data = sprite_animation_data(app);

    if (input_button_pressed(INPUT_BUTTON_START)) {
        LOGLNC(LOGCAT_STATE, "[state:sprite_animation_test] START pressed, return to menu");
        input_consume();
        game_app_request_state_change(debug_menu_state_desc(), NULL);
        return;
    }

    if (!data)
        return;

    debug_overlay_update(app, &data->overlay, dt);

    sprite_animation_try_create(data);

    if (data->anim_ready)
        gfx_sprite_anim_player_update(&data->anim_player, dt);

    sprite_animation_rebuild_overlay(data);
}

static void sprite_animation_test_draw(game_app_t *app, float alpha)
{
    sprite_animation_state_data_t *data = sprite_animation_data(app);

    (void)app;
    (void)alpha;

    gfx_sprite_draw_all();

    if (data)
        debug_overlay_draw(&data->overlay);
}

static const game_state_desc_t s_sprite_animation_test_state = {
    "sprite_animation_test",
    sprite_animation_test_enter,
    sprite_animation_test_exit,
    sprite_animation_test_fixed_update,
    sprite_animation_test_update,
    sprite_animation_test_draw
};

const game_state_desc_t *sprite_animation_test_state_desc(void)
{
    return &s_sprite_animation_test_state;
}