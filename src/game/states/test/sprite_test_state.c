#include "sprite_test_state.h"

#include "engine/logging/log.h"
#include "engine/gfx/gfx2d.h"
#include "engine/input/input.h"
#include <math.h>

#ifndef TEST_SPRITE_PATH
#define TEST_SPRITE_PATH "yellow.png"
#endif

#ifndef TEST_SPRITE_PATH1
#define TEST_SPRITE_PATH1 "scaryguy.png"
#endif

typedef struct sprite_test_data {
    int tex_id;
    gfx2d_draw_params_t draw_params;
    float speed;
} sprite_test_data_t;

static sprite_test_data_t s_sprite;
static sprite_test_data_t s_sprite1;
static sprite_test_data_t s_sprite2;

static int sprite_test_enter(game_app_t *app, void *userdata)
{
    (void)app;
    (void)userdata;

    s_sprite.tex_id = -1;
    s_sprite.draw_params = gfx2d_sprite_params(206.0f, 168.0f, 16.0f, 16.0f);
    s_sprite.draw_params.z = 10;
    s_sprite.speed = 240.0f;

    if (gfx2d_load_texture(TEST_SPRITE_PATH, &s_sprite.tex_id) < 0) {
        LOGLN("[state:sprite_test] failed to load texture");
        return -1;
    }

    s_sprite1.tex_id = -1;
    s_sprite1.draw_params = gfx2d_sprite_params(206.0f, 168.0f, 206.0f, 168.0f);
    s_sprite1.speed = 0.0f;

    if (gfx2d_load_texture(TEST_SPRITE_PATH1, &s_sprite1.tex_id) < 0) {
        LOGLN("[state:sprite_test] failed to load texture");
        return -1;
    }

    s_sprite2.tex_id = -1;
    s_sprite2.draw_params = gfx2d_sprite_params(100.0f, 50.0f, 103.0f, 94.0f);
    s_sprite2.speed = 0.0f;

    if (gfx2d_load_texture(TEST_SPRITE_PATH1, &s_sprite2.tex_id) < 0) {
        LOGLN("[state:sprite_test] failed to load texture");
        return -1;
    }

    LOGLN("[state:sprite_test] enter tex=%d", s_sprite.tex_id);
    return 0;
}

static void sprite_test_exit(game_app_t *app)
{
    (void)app;
    LOGLN("[state:sprite_test] exit");
}

static void sprite_test_update(game_app_t *app, float dt)
{
    (void)app;

    float move = s_sprite.speed * dt;

    if (input_button_down(INPUT_BUTTON_LEFT))
        s_sprite.draw_params.x -= move;
    if (input_button_down(INPUT_BUTTON_RIGHT))
        s_sprite.draw_params.x += move;
    if (input_button_down(INPUT_BUTTON_UP))
        s_sprite.draw_params.y -= move;
    if (input_button_down(INPUT_BUTTON_DOWN))
        s_sprite.draw_params.y += move;
}

static float t = 0.0f;

static void sprite_test_draw(game_app_t *app)
{
    (void)app;

    gfx2d_draw(
        s_sprite.tex_id,
        &s_sprite.draw_params
    );

    gfx2d_draw(
        s_sprite1.tex_id,
        &s_sprite1.draw_params
    );

    float skew = sinf(t) * 30.0f;

    // gfx2d_draw(s_sprite2.tex_id,
    //     100.0f + skew,  50.0f,
    //     220.0f + skew,  50.0f,
    //     100.0f,        170.0f,
    //     220.0f,        170.0f);
    // t += 0.05f;
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