#include "sprite_test_state.h"

#include "engine/logging/log.h"
#include "engine/gfx/gfx2d.h"
#include "engine/streaming/texture_assets.h"
#include "engine/input/input.h"
#include <math.h>

#ifndef TEST_SPRITE_PATH
#define TEST_SPRITE_PATH "yellow.png"
#endif

#ifndef TEST_SPRITE_PATH1
#define TEST_SPRITE_PATH1 "scaryguy1.png"
#endif

#ifndef TEST_SPRITE_PATH2
#define TEST_SPRITE_PATH2 "cryingguy0.png"
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

static sprite_test_data_t s_sprite;
static sprite_test_data_t s_sprite1;
static sprite_test_data_t s_sprite2;

static texture_handle_t sprite_test_invalid_texture(void)
{
    texture_handle_t h;
    h.index = 0xffffu;
    h.generation = 0;
    return h;
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
        LOGLN("[state:sprite_test] texture failed tag=%s path=%s",
              tag ? tag : "?",
              texture_path(s->texture));
        return -1;
    }

    if (st != TEXTURE_STATUS_READY)
        return 1;

    s->tex_id = texture_tex_id(s->texture);
    if (s->tex_id < 0) {
        LOGLN("[state:sprite_test] ready but no tex_id tag=%s", tag ? tag : "?");
        return -1;
    }

    if (!s->prewarmed) {
        int warm = texture_prewarm(s->texture);
        LOGLN("[state:sprite_test] prewarm tag=%s tex_id=%d result=%d",
              tag ? tag : "?",
              s->tex_id,
              warm);
        s->prewarmed = 1;
    }

    if (gfx2d_add_sprite(s->tex_id, &s->draw_params, &s->sprite_id) < 0) {
        LOGLN("[state:sprite_test] failed to add sprite tag=%s tex_id=%d",
              tag ? tag : "?",
              s->tex_id);
        return -1;
    }

    s->sprite_created = 1;
    LOGLN("[state:sprite_test] sprite ready tag=%s tex_id=%d sprite_id=%d",
          tag ? tag : "?",
          s->tex_id,
          s->sprite_id);
    return 0;
}

static int sprite_test_enter(game_app_t *app, void *userdata)
{
    (void)app;
    (void)userdata;

    sprite_test_reset_sprite(&s_sprite);
    sprite_test_reset_sprite(&s_sprite1);
    sprite_test_reset_sprite(&s_sprite2);

    s_sprite.draw_params = gfx2d_sprite_params(226.0f, 140.0f, 16.0f, 16.0f);
    s_sprite.draw_params.layer = 5;
    s_sprite.speed = 240.0f;

    s_sprite1.draw_params = gfx2d_sprite_params(306.0f, 168.0f, 206.0f, 168.0f);
    s_sprite1.draw_params.layer = 10;
    s_sprite1.draw_params.color.r = 0x80;
    s_sprite1.draw_params.color.g = 0x80;
    s_sprite1.draw_params.color.b = 0x80;
    s_sprite1.draw_params.color.a = 0x80;

    s_sprite2.draw_params = gfx2d_sprite_params(100.0f, 50.0f, 110.0f, 84.0f);
    s_sprite2.draw_params.layer = 0;

    s_sprite.texture  = texture_load_png(TEST_SPRITE_PATH,  STREAM_PRIORITY_NORMAL);
    s_sprite1.texture = texture_load_png(TEST_SPRITE_PATH1, STREAM_PRIORITY_NORMAL);
    s_sprite2.texture = texture_load_png(TEST_SPRITE_PATH2, STREAM_PRIORITY_NORMAL);

    if (!texture_is_valid(s_sprite.texture)
        || !texture_is_valid(s_sprite1.texture)
        || !texture_is_valid(s_sprite2.texture)) {
        LOGLN("[state:sprite_test] failed to request one or more textures");
        return -1;
    }

    LOGLN("[state:sprite_test] enter requested textures");
    return 0;
}

static void sprite_test_exit(game_app_t *app)
{
    (void)app;

    texture_release(s_sprite.texture);
    texture_release(s_sprite1.texture);
    texture_release(s_sprite2.texture);

    sprite_test_reset_sprite(&s_sprite);
    sprite_test_reset_sprite(&s_sprite1);
    sprite_test_reset_sprite(&s_sprite2);

    LOGLN("[state:sprite_test] exit");
}

static void sprite_test_update(game_app_t *app, float dt)
{
    (void)app;

    float move;

    sprite_test_try_create(&s_sprite, TEST_SPRITE_PATH);
    sprite_test_try_create(&s_sprite1, TEST_SPRITE_PATH1);
    sprite_test_try_create(&s_sprite2, TEST_SPRITE_PATH2);

    if (!s_sprite.sprite_created)
        return;

    move = s_sprite.speed * dt;

    if (input_button_down(INPUT_BUTTON_LEFT))
        s_sprite.draw_params.x -= move;
    if (input_button_down(INPUT_BUTTON_RIGHT))
        s_sprite.draw_params.x += move;
    if (input_button_down(INPUT_BUTTON_UP))
        s_sprite.draw_params.y -= move;
    if (input_button_down(INPUT_BUTTON_DOWN))
        s_sprite.draw_params.y += move;

    gfx2d_update_sprite(s_sprite.sprite_id, &s_sprite.draw_params);
}

static float t;

static void sprite_test_draw(game_app_t *app)
{
    (void)app;

    if (s_sprite2.sprite_created) {
        s_sprite2.draw_params.skew_x_rad = cosf(t) * 0.7f;
        s_sprite2.draw_params.skew_y_rad = cosf(t * 2.0f) * 0.4f;
        gfx2d_update_sprite(s_sprite2.sprite_id, &s_sprite2.draw_params);
    }

    t += 0.7f;

    gfx2d_draw();
}

static const game_state_desc_t s_sprite_test_state = {
    "sprite_test",
    sprite_test_enter,
    sprite_test_exit,
    sprite_test_update,
    sprite_test_draw
};

const game_state_desc_t *sprite_test_state_desc(void)
{
    return &s_sprite_test_state;
}