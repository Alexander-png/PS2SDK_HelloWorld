#include "game/states/test/debug_menu_state.h"

#include "engine/logging/log.h"
#include "engine/gfx/gfx2d.h"
#include "engine/streaming/texture_assets.h"
#include "engine/input/input.h"
#include "engine/memory/memory_arena.h"
#include "engine/debug/debug_overlay.h"

#include <math.h>

#ifndef TEST_SPRITE_PATH
#define TEST_SPRITE_PATH "yellow.png"
#endif

#ifndef TEST_SPRITE_PATH1
#define TEST_SPRITE_PATH1 "scaryguy.png"
#endif

#ifndef TEST_SPRITE_PATH2
#define TEST_SPRITE_PATH2 "cryingguy0.png"
#endif

#ifndef SPRITE_TEST_ANIM_SPEED
#define SPRITE_TEST_ANIM_SPEED 4.2f
#endif

typedef struct sprite_test_data {
    texture_handle_t texture;
    int tex_id;
    int sprite_id;
    int sprite_created;
    int prewarmed;
    gfx2d_draw_params_t draw_params;
    float speed;
} sprite_test_data_t;

typedef struct sprite_test_state_data {
    sprite_test_data_t sprite;
    sprite_test_data_t sprite1;
    sprite_test_data_t sprite2;
    float t;
    debug_overlay_t overlay;
} sprite_test_state_data_t;

static texture_handle_t sprite_test_invalid_texture(void)
{
    texture_handle_t h;
    h.index = 0xffffu;
    h.generation = 0;
    return h;
}

static const char *sprite_test_texture_status_name(texture_status_t st)
{
    switch (st) {
        case TEXTURE_STATUS_UNUSED: return "UNUSED";
        case TEXTURE_STATUS_LOADING: return "LOADING";
        case TEXTURE_STATUS_READY:   return "READY";
        case TEXTURE_STATUS_FAILED:  return "FAILED";
        default:                     return "?";
    }
}

static void sprite_test_rebuild_overlay(sprite_test_state_data_t *data)
{
    texture_status_t st0 = TEXTURE_STATUS_FAILED;
    texture_status_t st1 = TEXTURE_STATUS_FAILED;
    texture_status_t st2 = TEXTURE_STATUS_FAILED;

    if (!data)
        return;

    if (texture_is_valid(data->sprite.texture))
        st0 = texture_status(data->sprite.texture);
    if (texture_is_valid(data->sprite1.texture))
        st1 = texture_status(data->sprite1.texture);
    if (texture_is_valid(data->sprite2.texture))
        st2 = texture_status(data->sprite2.texture);

    debug_overlay_printf(
        &data->overlay,
        "Sprite Test\n"
        "START: return to menu\n"
        "D-PAD: move yellow sprite\n"
        "\n"
        "yellow   tex=%s sprite=%d pos=(%.1f, %.1f)\n"
        "scaryguy tex=%s sprite=%d\n"
        "crying   tex=%s sprite=%d t=%.2f",
        sprite_test_texture_status_name(st0),
        data->sprite.sprite_created,
        data->sprite.draw_params.x,
        data->sprite.draw_params.y,
        sprite_test_texture_status_name(st1),
        data->sprite1.sprite_created,
        sprite_test_texture_status_name(st2),
        data->sprite2.sprite_created,
        data->t
    );
}

static sprite_test_state_data_t *sprite_test_data(game_app_t *app)
{
    return GAME_APP_STATE_DATA_AS(app, sprite_test_state_data_t);
}

static void sprite_test_reset_sprite(sprite_test_data_t *s)
{
    if (!s)
        return;

    s->texture = sprite_test_invalid_texture();
    s->tex_id = -1;
    s->sprite_id = -1;
    s->sprite_created = 0;
    s->prewarmed = 0;
    s->speed = 0.0f;
}

static int sprite_test_try_create(sprite_test_data_t *s, const char *tag)
{
    texture_status_t st;

    if (!s || s->sprite_created)
        return 0;

    if (!texture_is_valid(s->texture))
        return -1;

    st = texture_status(s->texture);
    if (st == TEXTURE_STATUS_FAILED) {
        LOGLNC(LOGCAT_RESOURCES, "[state:sprite_test] texture failed tag=%s path=%s",
              tag ? tag : "?",
              texture_path(s->texture));
        return -1;
    }

    if (st != TEXTURE_STATUS_READY)
        return 1;

    s->tex_id = texture_tex_id(s->texture);
    if (s->tex_id < 0) {
        LOGLNC(LOGCAT_RESOURCES, "[state:sprite_test] ready but no tex_id tag=%s", tag ? tag : "?");
        return -1;
    }

    if (!s->prewarmed) {
        int warm = texture_prewarm(s->texture);
        LOGLNC(LOGCAT_RESOURCES, "[state:sprite_test] prewarm tag=%s tex_id=%d result=%d",
              tag ? tag : "?",
              s->tex_id,
              warm);
        s->prewarmed = 1;
    }

    if (gfx2d_add_sprite(s->tex_id, &s->draw_params, &s->sprite_id) < 0) {
        LOGLNC(LOGCAT_GFX, "[state:sprite_test] failed to add sprite tag=%s tex_id=%d",
              tag ? tag : "?",
              s->tex_id);
        return -1;
    }

    s->sprite_created = 1;
    LOGLNC(LOGCAT_GFX, "[state:sprite_test] sprite ready tag=%s tex_id=%d sprite_id=%d",
          tag ? tag : "?",
          s->tex_id,
          s->sprite_id);
    return 0;
}

static int sprite_test_enter(game_app_t *app, void *userdata)
{
    sprite_test_state_data_t *data;

    (void)userdata;

    data = (sprite_test_state_data_t *)mem_arena_calloc(
        game_app_state_arena(app),
        1,
        sizeof(*data),
        16
    );
    if (!data) {
        LOGLNC(LOGCAT_STATE, "[state:sprite_test] enter failed: no state arena memory");
        return -1;
    }

    game_app_set_state_userdata(app, data);

    sprite_test_reset_sprite(&data->sprite);
    sprite_test_reset_sprite(&data->sprite1);
    sprite_test_reset_sprite(&data->sprite2);
    data->t = 0.0f;

    data->sprite.draw_params = gfx2d_sprite_params(226.0f, 140.0f, 16.0f, 16.0f);
    data->sprite.draw_params.layer = 5;
    data->sprite.speed = 240.0f;

    data->sprite1.draw_params = gfx2d_sprite_params(306.0f, 168.0f, 206.0f, 168.0f);
    data->sprite1.draw_params.layer = 10;
    // data->sprite1.draw_params.color.r = 0xFF;
    // data->sprite1.draw_params.color.g = 0xFF;
    // data->sprite1.draw_params.color.b = 0xFF;
    // data->sprite1.draw_params.color.a = 0xFF;

    data->sprite2.draw_params = gfx2d_sprite_params(100.0f, 50.0f, 110.0f, 84.0f);
    data->sprite2.draw_params.layer = 0;

    data->sprite.texture  = texture_load_png(TEST_SPRITE_PATH,  STREAM_PRIORITY_NORMAL);
    data->sprite1.texture = texture_load_png(TEST_SPRITE_PATH1, STREAM_PRIORITY_NORMAL);
    data->sprite2.texture = texture_load_png(TEST_SPRITE_PATH2, STREAM_PRIORITY_NORMAL);

    if (!texture_is_valid(data->sprite.texture)
        || !texture_is_valid(data->sprite1.texture)
        || !texture_is_valid(data->sprite2.texture)) {
        LOGLNC(LOGCAT_RESOURCES, "[state:sprite_test] failed to request one or more textures");
        return -1;
    }

    debug_overlay_desc_t overlay_desc;
    debug_overlay_desc_init(&overlay_desc);
    overlay_desc.x = 16;
    overlay_desc.y = 16;
    overlay_desc.w = 620;
    overlay_desc.h = 140;

    if (debug_overlay_init(app, &data->overlay, &overlay_desc) != 0) {
        LOGLNC(LOGCAT_STATE, "[state:sprite_test] overlay init failed");
        return -1;
    }

    sprite_test_rebuild_overlay(data);

    LOGLNC(LOGCAT_STATE, "[state:sprite_test] enter requested textures");
    return 0;
}

static void sprite_test_exit(game_app_t *app)
{
    sprite_test_state_data_t *data = sprite_test_data(app);

    if (!data) {
        LOGLNC(LOGCAT_STATE, "[state:sprite_test] exit");
        return;
    }

    if (texture_is_valid(data->sprite.texture))
        texture_release(data->sprite.texture);
    if (texture_is_valid(data->sprite1.texture))
        texture_release(data->sprite1.texture);
    if (texture_is_valid(data->sprite2.texture))
        texture_release(data->sprite2.texture);

    sprite_test_reset_sprite(&data->sprite);
    sprite_test_reset_sprite(&data->sprite1);
    sprite_test_reset_sprite(&data->sprite2);
    data->t = 0.0f;

    debug_overlay_shutdown(app, &data->overlay);

    game_app_set_state_userdata(app, NULL);

    LOGLNC(LOGCAT_STATE, "[state:sprite_test] exit");
}

static void sprite_test_fixed_update(game_app_t *app, float dt)
{
    (void)app;
    (void)dt;
}

static void sprite_test_update(game_app_t *app, float dt)
{
    sprite_test_state_data_t *data = sprite_test_data(app);
    float move;

    if (input_button_pressed(INPUT_BUTTON_START)) {
        LOGLNC(LOGCAT_STATE, "[state:sprite_test] START pressed, return to menu");
        input_consume();
        game_app_request_state_change(debug_menu_state_desc(), NULL);
        return;
    }

    if (!data)
        return;

    debug_overlay_update(app, &data->overlay, dt);

    sprite_test_try_create(&data->sprite, TEST_SPRITE_PATH);
    sprite_test_try_create(&data->sprite1, TEST_SPRITE_PATH1);
    sprite_test_try_create(&data->sprite2, TEST_SPRITE_PATH2);

    data->t += SPRITE_TEST_ANIM_SPEED * dt;

    if (data->sprite2.sprite_created) {
        data->sprite2.draw_params.skew_x_rad = cosf(data->t) * 0.7f;
        data->sprite2.draw_params.skew_y_rad = cosf(data->t * 2.0f) * 0.4f;
        gfx2d_update_sprite(data->sprite2.sprite_id, &data->sprite2.draw_params);
    }

    if (!data->sprite.sprite_created)
        return;

    move = data->sprite.speed * dt;

    if (input_button_down(INPUT_BUTTON_LEFT))
        data->sprite.draw_params.x -= move;
    if (input_button_down(INPUT_BUTTON_RIGHT))
        data->sprite.draw_params.x += move;
    if (input_button_down(INPUT_BUTTON_UP))
        data->sprite.draw_params.y -= move;
    if (input_button_down(INPUT_BUTTON_DOWN))
        data->sprite.draw_params.y += move;

    gfx2d_update_sprite(data->sprite.sprite_id, &data->sprite.draw_params);
    sprite_test_rebuild_overlay(data);
}

static void sprite_test_draw(game_app_t *app, float alpha)
{
    sprite_test_state_data_t *data = sprite_test_data(app);

    (void)app;
    (void)alpha;

    gfx2d_draw();

    if (data)
        debug_overlay_draw(&data->overlay);
}

static const game_state_desc_t s_sprite_test_state = {
    "sprite_test",
    sprite_test_enter,
    sprite_test_exit,
    sprite_test_fixed_update,
    sprite_test_update,
    sprite_test_draw
};

const game_state_desc_t *sprite_test_state_desc(void)
{
    return &s_sprite_test_state;
}